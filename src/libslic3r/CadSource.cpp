///|/ Tisma Slicer: B-Rep origin of meshes imported from STEP files (phase 4 of docs/IMPLEMENTATION_ROADMAP.md).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CadSource.hpp"

#include <cstdio>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <Eigen/Dense>

#include "AABBTreeIndirect.hpp"
#include "Format/STEP.hpp"
#include "Model.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

// ---------------------------------------------------------------------------------------------------------------------
// Registry of STEP files.

static std::mutex                                                     s_step_files_mutex;
static std::map<std::string, std::shared_ptr<const CadStepFile>>      s_step_files;

// FNV-1a 64 bit hash of the contents plus their size. Only used to find equal files, not for security.
static std::string step_file_key(const std::string &data)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%016llx-%llu", (unsigned long long)hash, (unsigned long long)data.size());
    return buf;
}

std::shared_ptr<const CadStepFile> cad_step_file_register(std::string name, std::string data)
{
    std::string key = step_file_key(data);
    std::lock_guard<std::mutex> lock(s_step_files_mutex);
    if (auto it = s_step_files.find(key); it != s_step_files.end() && it->second->data == data)
        return it->second;
    auto file = std::make_shared<CadStepFile>();
    file->name = std::move(name);
    file->data = std::move(data);
    file->key  = key;
    s_step_files[key] = file;
    return file;
}

std::shared_ptr<const CadStepFile> cad_step_file_find(const std::string &key)
{
    std::lock_guard<std::mutex> lock(s_step_files_mutex);
    auto it = s_step_files.find(key);
    return it == s_step_files.end() ? nullptr : it->second;
}

bool CadSource::matches(const TriangleMesh &mesh) const
{
    return this->step && this->face_ids.size() == mesh.its.indices.size();
}

// ---------------------------------------------------------------------------------------------------------------------
// Face ids.

std::string cad_face_ids_to_string(const std::vector<int> &face_ids)
{
    std::ostringstream out;
    for (size_t i = 0; i < face_ids.size();) {
        size_t j = i + 1;
        while (j < face_ids.size() && face_ids[j] == face_ids[i])
            ++ j;
        if (i > 0)
            out << ' ';
        out << face_ids[i] << '*' << (j - i);
        i = j;
    }
    return out.str();
}

bool cad_face_ids_from_string(const std::string &str, std::vector<int> &face_ids)
{
    face_ids.clear();
    std::istringstream in(str);
    std::string run;
    while (in >> run) {
        const size_t star = run.find('*');
        if (star == std::string::npos)
            return false;
        try {
            const int    face  = std::stoi(run.substr(0, star));
            const long   count = std::stol(run.substr(star + 1));
            if (face < -1 || count <= 0 || count > 100000000)
                return false;
            face_ids.insert(face_ids.end(), size_t(count), face);
        } catch (const std::exception &) {
            return false;
        }
    }
    return true;
}

std::vector<int> cad_face_ids_from_tags(const std::vector<uint16_t> &tags)
{
    std::vector<int> out(tags.size());
    for (size_t i = 0; i < tags.size(); ++ i)
        out[i] = tags[i] == 0xFFFF ? -1 : int(tags[i]);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Reprojection of the painting.

namespace {

// Leaf triangles of the old painting, with an AABB tree to find the closest one to a point.
struct LeafSet
{
    std::vector<Vec3f>                     vertices;
    std::vector<Vec3i32>                   indices;
    std::vector<TriangleStateType>         states;
    AABBTreeIndirect::Tree<3, float>       tree;

    void add(const TriangleSelector::LeafTriangle &leaf)
    {
        const int idx = int(vertices.size());
        vertices.insert(vertices.end(), leaf.vertices.begin(), leaf.vertices.end());
        indices.emplace_back(idx, idx + 1, idx + 2);
        states.emplace_back(leaf.state);
    }
    void build() { tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(vertices, indices); }
    TriangleStateType closest_state(const Vec3f &pt) const
    {
        size_t hit_idx = 0;
        Vec3f  hit_pt;
        if (indices.empty() || AABBTreeIndirect::squared_distance_to_indexed_triangle_set(vertices, indices, tree, pt, hit_idx, hit_pt) < 0.f)
            return TriangleStateType::NONE;
        return states[hit_idx];
    }
};

} // namespace

void cad_remap_painting(const TriangleMesh &old_mesh, const std::vector<int> &old_face_ids,
                        const TriangleSelector::TriangleSplittingData &old_data,
                        const TriangleMesh &new_mesh, const std::vector<int> &new_face_ids,
                        TriangleSelector &new_selector, CadRemapStats *stats)
{
    new_selector.reset();
    if (old_data.triangles_to_split.empty())
        return;

    TriangleSelector old_selector(old_mesh);
    old_selector.deserialize(old_data);
    const std::vector<TriangleSelector::LeafTriangle> leaves = old_selector.get_leaf_triangles();

    // States of the leaves grouped by face.
    std::unordered_map<int, LeafSet> faces;
    LeafSet                          all;
    for (const TriangleSelector::LeafTriangle &leaf : leaves) {
        const int face = leaf.source_triangle >= 0 && leaf.source_triangle < int(old_face_ids.size()) ?
            old_face_ids[leaf.source_triangle] : -1;
        faces[face].add(leaf);
        all.add(leaf);
    }

    // Uniform faces keep a single state, the others need the AABB tree.
    std::unordered_map<int, TriangleStateType> uniform;
    for (auto &[face, set] : faces) {
        bool is_uniform = true;
        for (TriangleStateType st : set.states)
            if (st != set.states.front()) {
                is_uniform = false;
                break;
            }
        if (is_uniform)
            uniform[face] = set.states.front();
        else
            set.build();
        if (stats && face >= 0) {
            if (is_uniform && set.states.front() != TriangleStateType::NONE)
                ++ stats->faces_exact;
            else if (! is_uniform)
                ++ stats->faces_approximate;
        }
    }
    bool all_built = false;

    const indexed_triangle_set &its = new_mesh.its;
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        const int face = i < new_face_ids.size() ? new_face_ids[i] : -1;
        TriangleStateType state = TriangleStateType::NONE;
        const Vec3f centroid = (its.vertices[its.indices[i][0]] + its.vertices[its.indices[i][1]] + its.vertices[its.indices[i][2]]) / 3.f;
        if (auto it = uniform.find(face); face >= 0 && it != uniform.end()) {
            state = it->second;
        } else if (auto it = faces.find(face); face >= 0 && it != faces.end()) {
            state = it->second.closest_state(centroid);
        } else {
            if (! all_built) {
                all.build();
                all_built = true;
            }
            state = all.closest_state(centroid);
            if (stats)
                ++ stats->triangles_unmatched;
        }
        if (state != TriangleStateType::NONE)
            new_selector.set_facet(int(i), state);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Tessellation.

bool cad_tessellate_solid(const CadStepFile &step, int solid_index, double linear_deflection, double angular_deflection,
                          TriangleMesh &mesh_out, std::vector<int> &face_ids_out, std::string &error)
{
    namespace fs = boost::filesystem;
    // The OCCT reader needs a file: write the stored contents to a temporary one.
    fs::path path;
    try {
        path = fs::temp_directory_path() / fs::unique_path("tisma-cad-%%%%-%%%%-%%%%.step");
        boost::nowide::ofstream out(path.string(), std::ios::binary);
        out.write(step.data.data(), std::streamsize(step.data.size()));
        if (! out.good()) {
            error = "Cannot write a temporary file for the STEP model";
            return false;
        }
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }

    std::vector<uint16_t> tags;
    bool ok = false;
    try {
        ok = step_tessellate_solid(path.string().c_str(), solid_index, linear_deflection, angular_deflection, mesh_out, tags, error);
    } catch (const std::exception &ex) {
        error = ex.what();
        ok = false;
    }
    boost::system::error_code ec;
    fs::remove(path, ec);
    if (ok)
        face_ids_out = cad_face_ids_from_tags(tags);
    return ok;
}

// Fits the affine transformation which maps the vertices of the triangles of src to the vertices of the same
// triangles of dst. Returns false when the meshes do not correspond (the mesh was edited).
static bool fit_affine(const indexed_triangle_set &src, const indexed_triangle_set &dst, Transform3d &trafo)
{
    if (src.indices.size() != dst.indices.size() || src.indices.empty())
        return false;
    // Pairs of corresponding vertices, sampled to keep the fit fast on big meshes.
    const size_t step = std::max<size_t>(1, src.indices.size() / 4000);
    std::vector<std::pair<Vec3d, Vec3d>> pairs;
    for (size_t i = 0; i < src.indices.size(); i += step)
        for (int k = 0; k < 3; ++ k)
            pairs.emplace_back(src.vertices[src.indices[i][k]].cast<double>(), dst.vertices[dst.indices[i][k]].cast<double>());

    // The repair on import may reverse the orientation of a triangle (rotating its vertices), so the fit is done
    // on the centroids, which do not depend on the order of the vertices.
    std::vector<std::pair<Vec3d, Vec3d>> centroids;
    for (size_t i = 0; i < pairs.size(); i += 3)
        centroids.emplace_back((pairs[i].first + pairs[i + 1].first + pairs[i + 2].first) / 3.,
                               (pairs[i].second + pairs[i + 1].second + pairs[i + 2].second) / 3.);
    if (centroids.size() < 4)
        return false;

    Eigen::MatrixXd A(centroids.size(), 4);
    Eigen::MatrixXd B(centroids.size(), 3);
    BoundingBoxf3   bbox;
    for (size_t i = 0; i < centroids.size(); ++ i) {
        A.row(i) << centroids[i].first.x(), centroids[i].first.y(), centroids[i].first.z(), 1.;
        B.row(i) = centroids[i].second.transpose();
        bbox.merge(centroids[i].second);
    }
    const Eigen::MatrixXd X = A.colPivHouseholderQr().solve(B);
    Eigen::Matrix4d m = Eigen::Matrix4d::Identity();
    m.block<3, 4>(0, 0) = X.transpose();
    trafo.matrix() = m;

    // All the triangles must land on their counterpart.
    const double tolerance = std::max(1e-4, 1e-5 * bbox.size().norm());
    for (size_t i = 0; i < src.indices.size(); ++ i) {
        Vec3d cs = Vec3d::Zero(), cd = Vec3d::Zero();
        for (int k = 0; k < 3; ++ k) {
            cs += src.vertices[src.indices[i][k]].cast<double>();
            cd += dst.vertices[dst.indices[i][k]].cast<double>();
        }
        if ((trafo * (cs / 3.) - cd / 3.).norm() > tolerance)
            return false;
    }
    return true;
}

bool cad_retessellate_volume(ModelVolume &volume, double linear_deflection, double angular_deflection,
                             CadRemapStats *stats, std::string &error)
{
    const std::shared_ptr<const CadSource> source = volume.cad_source;
    if (! source || ! source->step) {
        error = "The part was not imported from a STEP file";
        return false;
    }
    if (! source->matches(volume.mesh())) {
        error = "The mesh of the part was modified after the import";
        return false;
    }

    // The current mesh may have been moved, mirrored or scaled since the import. The tessellation with the
    // parameters of the current mesh gives the same triangles, which tell the transformation.
    TriangleMesh     reference;
    std::vector<int> reference_ids;
    if (! cad_tessellate_solid(*source->step, source->solid_index, source->linear_deflection, source->angular_deflection,
                               reference, reference_ids, error))
        return false;
    Transform3d trafo;
    if (reference_ids != source->face_ids || ! fit_affine(reference.its, volume.mesh().its, trafo)) {
        error = "The mesh of the part was modified after the import";
        return false;
    }

    TriangleMesh     new_mesh;
    std::vector<int> new_ids;
    if (! cad_tessellate_solid(*source->step, source->solid_index, linear_deflection, angular_deflection, new_mesh, new_ids, error))
        return false;
    new_mesh.transform(trafo, true);

    // Painting of the old mesh projected on the new one.
    auto remap = [&](FacetsAnnotation &facets) {
        TriangleSelector selector(new_mesh);
        cad_remap_painting(volume.mesh(), source->face_ids, facets.get_data(), new_mesh, new_ids, selector, stats);
        facets.set(selector);
    };
    remap(volume.supported_facets);
    remap(volume.seam_facets);
    remap(volume.mm_segmentation_facets);
    remap(volume.fuzzy_skin_facets);

    auto new_source = std::make_shared<CadSource>(*source);
    new_source->linear_deflection  = linear_deflection;
    new_source->angular_deflection = angular_deflection;
    new_source->face_ids           = std::move(new_ids);

    // Supports and loads of the structural analysis on this volume follow their B-Rep faces.
    if (ModelObject *object = volume.get_object()) {
        const auto it = std::find(object->volumes.begin(), object->volumes.end(), &volume);
        const int  volume_idx = int(it - object->volumes.begin());
        auto remap_region = [&](EngineeringRegion &region) {
            if (region.volume != volume_idx || region.cad_faces.empty())
                return;
            region.triangles.clear();
            for (size_t i = 0; i < new_source->face_ids.size(); ++ i)
                if (std::find(region.cad_faces.begin(), region.cad_faces.end(), new_source->face_ids[i]) != region.cad_faces.end())
                    region.triangles.push_back(int(i));
        };
        for (EngineeringRegion &region : object->engineering.fixtures)
            remap_region(region);
        for (EngineeringLoad &load : object->engineering.loads)
            if (load.type == EngineeringLoad::Type::Faces)
                remap_region(load.faces);
    }

    volume.set_mesh(std::move(new_mesh));
    volume.calculate_convex_hull();
    volume.cad_source = std::move(new_source);
    return true;
}

} // namespace Slic3r
