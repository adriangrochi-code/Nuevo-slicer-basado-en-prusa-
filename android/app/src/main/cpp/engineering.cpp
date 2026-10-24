// Structural analysis of the Tisma Android app (see engineering.hpp).

#include "engineering.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <queue>
#include <sstream>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "libslic3r/Exception.hpp"
#include "libslic3r/FileReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "tisma_fea/Materials.hpp"
#include "tisma_fea/ModelSetup.hpp"
#include "tisma_fea/Optimize.hpp"

using namespace Slic3r;
namespace pt = boost::property_tree;

namespace TismaApp {

namespace {

// Voxels of the analysis: as the "Fast" and "Normal" qualities of the desktop program, and a finer one.
constexpr size_t MIN_VOXELS = 5000;
constexpr size_t MAX_VOXELS = 150000;

Vec3d vec3(const pt::ptree &tree, const char *key)
{
    Vec3d v = Vec3d::Zero();
    if (auto child = tree.get_child_optional(key)) {
        int i = 0;
        for (const auto &item : *child)
            if (i < 3)
                v[i++] = item.second.get_value<double>();
    }
    return v;
}

std::string number(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", std::isfinite(v) ? v : 0.);
    return buf;
}

std::string json_string(const std::string &s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out + "\"";
}

// Squared distance from p to the triangle abc (Ericson, "Real-Time Collision Detection", 5.1.5).
double distance2_to_triangle(const Vec3d &p, const Vec3d &a, const Vec3d &b, const Vec3d &c)
{
    const Vec3d ab = b - a, ac = c - a, ap = p - a;
    const double d1 = ab.dot(ap), d2 = ac.dot(ap);
    if (d1 <= 0. && d2 <= 0.) return (p - a).squaredNorm();
    const Vec3d bp = p - b;
    const double d3 = ab.dot(bp), d4 = ac.dot(bp);
    if (d3 >= 0. && d4 <= d3) return (p - b).squaredNorm();
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0. && d1 >= 0. && d3 <= 0.) return (p - (a + ab * (d1 / (d1 - d3)))).squaredNorm();
    const Vec3d cp = p - c;
    const double d5 = ab.dot(cp), d6 = ac.dot(cp);
    if (d6 >= 0. && d5 <= d6) return (p - c).squaredNorm();
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0. && d2 >= 0. && d6 <= 0.) return (p - (a + ac * (d2 / (d2 - d6)))).squaredNorm();
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0. && (d4 - d3) >= 0. && (d5 - d6) >= 0.)
        return (p - (b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))))).squaredNorm();
    const double denom = 1. / (va + vb + vc);
    return (p - (a + ab * (vb * denom) + ac * (vc * denom))).squaredNorm();
}

struct Hit
{
    int volume { -1 };
    int facet { -1 };
};

// The triangle of a model part under a point picked on the phone: the closest one facing the same way as the picked
// face (thin walls have two faces at the same place).
Hit find_facet(const ModelObject &object, const Transform3d &instance, const Vec3d &point, const Vec3d &normal)
{
    Hit best;
    double best_d2 = std::numeric_limits<double>::max();
    for (size_t v = 0; v < object.volumes.size(); ++ v) {
        const ModelVolume &volume = *object.volumes[v];
        if (! volume.is_model_part())
            continue;
        const Transform3d trafo = instance * volume.get_matrix();
        const indexed_triangle_set &its = volume.mesh().its;
        for (size_t f = 0; f < its.indices.size(); ++ f) {
            const Vec3d a = trafo * its.vertices[its.indices[f][0]].cast<double>();
            const Vec3d b = trafo * its.vertices[its.indices[f][1]].cast<double>();
            const Vec3d c = trafo * its.vertices[its.indices[f][2]].cast<double>();
            const Vec3d n = (b - a).cross(c - a);
            if (normal.squaredNorm() > 0. && n.dot(normal) <= 0.)
                continue;
            const double d2 = distance2_to_triangle(point, a, b, c);
            if (d2 < best_d2) {
                best_d2 = d2;
                best    = { int(v), int(f) };
            }
        }
    }
    return best;
}

// The flat region around a triangle, as GLGizmoEngineering::pick_face() of the desktop program (normals within 3°).
EngineeringRegion flat_region(const ModelVolume &volume, int volume_idx, int facet)
{
    EngineeringRegion region;
    region.volume = volume_idx;
    const indexed_triangle_set &its = volume.mesh().its;
    const std::vector<Vec3i> neighbors = its_face_neighbors(its);
    const Vec3f normal = its_face_normal(its, facet);
    const float limit  = std::cos(float(M_PI) * 3.f / 180.f);
    std::vector<char> visited(its.indices.size(), 0);
    std::queue<int> queue;
    queue.push(facet);
    visited[facet] = 1;
    while (! queue.empty()) {
        const int f = queue.front();
        queue.pop();
        region.triangles.push_back(f);
        for (int k = 0; k < 3; ++ k) {
            const int n = neighbors[f][k];
            if (n >= 0 && ! visited[n] && its_face_normal(its, n).dot(normal) > limit) {
                visited[n] = 1;
                queue.push(n);
            }
        }
    }
    std::sort(region.triangles.begin(), region.triangles.end());
    return region;
}

// The faces of the object resting on the bed (its lowest flat faces facing down), one region per part.
std::vector<EngineeringRegion> bed_regions(const ModelObject &object, const Transform3d &instance)
{
    double min_z = std::numeric_limits<double>::max();
    for (const ModelVolume *volume : object.volumes)
        if (volume->is_model_part()) {
            const Transform3d trafo = instance * volume->get_matrix();
            for (const stl_vertex &v : volume->mesh().its.vertices)
                min_z = std::min(min_z, (trafo * v.cast<double>()).z());
        }
    std::vector<EngineeringRegion> out;
    for (size_t v = 0; v < object.volumes.size(); ++ v) {
        const ModelVolume &volume = *object.volumes[v];
        if (! volume.is_model_part())
            continue;
        const Transform3d trafo = instance * volume.get_matrix();
        const indexed_triangle_set &its = volume.mesh().its;
        EngineeringRegion region;
        region.volume = int(v);
        for (size_t f = 0; f < its.indices.size(); ++ f) {
            const Vec3d a = trafo * its.vertices[its.indices[f][0]].cast<double>();
            const Vec3d b = trafo * its.vertices[its.indices[f][1]].cast<double>();
            const Vec3d c = trafo * its.vertices[its.indices[f][2]].cast<double>();
            const Vec3d n = (b - a).cross(c - a);
            if (n.norm() > 0. && n.z() < -0.999 * n.norm() && std::max({ a.z(), b.z(), c.z() }) < min_z + 0.02)
                region.triangles.push_back(int(f));
        }
        if (! region.triangles.empty())
            out.emplace_back(std::move(region));
    }
    return out;
}

const char *verdict_key(Fea::Verdict v)
{
    switch (v) {
    case Fea::Verdict::Holds:            return "holds";
    case Fea::Verdict::LowMargin:        return "low_margin";
    case Fea::Verdict::OutOfLoad:        return "out_of_load";
    case Fea::Verdict::TooFlexible:      return "too_flexible";
    case Fea::Verdict::OutOfTemperature: return "out_of_temperature";
    case Fea::Verdict::NotApplicable:    return "not_applicable";
    }
    return "holds";
}

std::string result_json(const Fea::Result &r)
{
    std::string alternatives;
    for (const auto &[key, sf] : r.alternatives)
        alternatives += (alternatives.empty() ? "" : ",") + std::string("[") + json_string(key) + "," + number(sf) + "]";
    return "{\"verdict\":\"" + std::string(verdict_key(r.verdict)) + "\",\"safety_factor\":" + number(r.safety_factor) +
           ",\"max_von_mises\":" + number(r.max_von_mises) + ",\"max_displacement\":" + number(r.max_displacement) +
           ",\"mass\":" + number(r.mass) + ",\"solid_mass\":" + number(r.solid_mass) +
           ",\"critical\":[" + number(r.critical_point.x()) + "," + number(r.critical_point.y()) + "," +
           number(r.critical_point.z()) + "],\"voxels\":" + std::to_string(r.voxels.size()) +
           ",\"voxel_size\":" + number(r.h) + ",\"alternatives\":[" + alternatives + "]}";
}

// The failure index of every voxel: "TFEA", version 1, origin (3 doubles), voxel edge (double), grid size (3 int32),
// count (int32), then count times the voxel index (int32) and its failure index (float). Little endian.
void write_field(const Fea::Result &r, const std::string &path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (! out)
        throw Slic3r::RuntimeError("Cannot write " + path);
    auto put = [&out](const auto &v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    out.write("TFEA", 4);
    put(int32_t(1));
    for (int i = 0; i < 3; ++ i)
        put(double(r.origin[i]));
    put(double(r.h));
    for (int i = 0; i < 3; ++ i)
        put(int32_t(r.size[i]));
    put(int32_t(r.voxels.size()));
    for (size_t i = 0; i < r.voxels.size(); ++ i) {
        put(int32_t(r.voxels[i]));
        put(float(i < r.failure_index.size() ? r.failure_index[i] : 0.f));
    }
    if (! out)
        throw Slic3r::RuntimeError("Cannot write " + path);
}

} // namespace

std::string analyze(const std::string &model_path, const DynamicPrintConfig &config, const std::string &request,
                    const std::string &field_path, std::function<void(int)> progress, std::function<bool()> cancel)
{
    pt::ptree req;
    {
        std::istringstream is(request);
        pt::read_json(is, req);
    }
    Model model = FileReader::load_model(model_path, FileReader::LoadAttribute::AddDefaultInstances);
    if (model.objects.empty())
        throw Slic3r::RuntimeError("The file has no objects");
    ModelObject &object = *model.objects.front();
    const Transform3d instance = object.instances.front()->get_matrix();

    EngineeringSetup &eng = object.engineering;
    eng.material      = req.get<std::string>("material", "");
    eng.temperature   = req.get<double>("temperature", 23.);
    eng.safety_factor = req.get<double>("safety_factor", 2.);
    if (req.get<bool>("bed_fixture", false))
        for (EngineeringRegion &region : bed_regions(object, instance))
            eng.fixtures.emplace_back(std::move(region));
    if (auto fixtures = req.get_child_optional("fixtures"))
        for (const auto &item : *fixtures) {
            const Hit hit = find_facet(object, instance, vec3(item.second, "point"), vec3(item.second, "normal"));
            if (hit.volume >= 0)
                eng.fixtures.emplace_back(flat_region(*object.volumes[hit.volume], hit.volume, hit.facet));
        }
    if (eng.fixtures.empty())
        throw Slic3r::RuntimeError("no_fixture");
    if (auto loads = req.get_child_optional("loads"))
        for (const auto &item : *loads) {
            const Vec3d point = vec3(item.second, "point");
            const Hit hit = find_facet(object, instance, point, vec3(item.second, "normal"));
            if (hit.volume < 0)
                continue;
            EngineeringLoad load;
            load.name  = "Load " + std::to_string(eng.loads.size() + 1);
            load.force = vec3(item.second, "force");
            if (item.second.get<bool>("face", false)) {
                load.type  = EngineeringLoad::Type::Faces;
                load.faces = flat_region(*object.volumes[hit.volume], hit.volume, hit.facet);
            } else {
                load.type   = EngineeringLoad::Type::Point;
                load.volume = hit.volume;
                // In the coordinates of the mesh of the part (build_analysis_input() places it again).
                load.point  = (instance * object.volumes[hit.volume]->get_matrix()).inverse() * point;
            }
            eng.loads.emplace_back(std::move(load));
        }
    if (eng.loads.empty())
        throw Slic3r::RuntimeError("no_load");

    const std::string filament_type = config.has("filament_type") ? config.opt_serialize("filament_type") : "PLA";
    Fea::ModelAnalysisInput input;
    std::string error;
    if (! Fea::build_analysis_input(object, 0, filament_type, input, error, &config))
        throw Slic3r::RuntimeError(error);
    // The voxels: about the number asked inside the part, as the "Fast" and "Normal" qualities of the desktop program.
    const size_t voxels = std::clamp<size_t>(req.get<size_t>("voxels", 20000), MIN_VOXELS, MAX_VOXELS);
    const double volume = std::max(std::abs(double(its_volume(input.mesh))), 1e-9);
    input.setup.voxel_size = std::cbrt(volume / double(voxels));

    std::string out = "{\"material\":" + json_string(input.material);
    if (req.get<std::string>("mode", "analyze") == "lightest") {
        Fea::OptimizeOptions options;
        // The phone applies one infill density to the whole part (no modifiers).
        options.zones = false;
        const Fea::OptimizeResult res = Fea::optimize_infill(input.mesh, input.setup, options, cancel, progress);
        if (! res.ok)
            throw Slic3r::RuntimeError(res.error.empty() ? "The analysis failed" : res.error);
        write_field(res.uniform, field_path);
        out += ",\"feasible\":" + std::string(res.feasible ? "true" : "false") + ",\"density\":" + number(res.uniform_density) +
               ",\"analyses\":" + std::to_string(res.analyses) + ",\"result\":" + result_json(res.uniform);
    } else {
        const Fea::Result res = Fea::analyze(input.mesh, input.setup, cancel, progress);
        if (! res.ok)
            throw Slic3r::RuntimeError(res.error.empty() ? "The analysis failed" : res.error);
        write_field(res, field_path);
        out += ",\"result\":" + result_json(res);
    }
    return out + "}";
}

std::string materials_json()
{
    std::string out = "[";
    for (const Fea::Material &m : Fea::materials())
        out += (out.size() > 1 ? "," : "") + std::string("{\"key\":") + json_string(m.key) + ",\"name\":" + json_string(m.name) +
               ",\"linear\":" + (m.linear_analysis_valid ? "true" : "false") + "}";
    return out + "]";
}

} // namespace TismaApp
