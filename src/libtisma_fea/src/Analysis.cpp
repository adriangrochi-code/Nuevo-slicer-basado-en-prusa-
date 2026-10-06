///|/ Tisma Slicer: linear static structural analysis of a part on a voxel grid (phase 5).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Method (see docs/FEA.md):
// - The part is voxelized by slicing the mesh at the centers of the voxel layers; a voxel is solid when its center is
//   inside the slice.
// - Every voxel is a trilinear hexahedron (8 nodes, 2x2x2 Gauss points) with a transversely isotropic material whose
//   axis is Z (the build direction): the layers are weaker and softer across than along.
// - The system K u = f is solved with a conjugate gradient with Jacobi preconditioner, without assembling K: the
//   product K p is done element by element (the element matrix is the same for all the voxels), in parallel over 8
//   groups of voxels which do not share nodes.
// - Stresses are evaluated at the center of each voxel.
#include "tisma_fea/Analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

#include <Eigen/Dense>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>

namespace Slic3r {
namespace Fea {

namespace {

using Matrix6d  = Eigen::Matrix<double, 6, 6>;
using Matrix24d = Eigen::Matrix<double, 24, 24>;
using Matrix6x24d = Eigen::Matrix<double, 6, 24>;

// Local nodes of the hexahedron (corner offsets).
constexpr int NODE_OFFSETS[8][3] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1} };

// Constitutive matrix (strain order xx, yy, zz, yz, xz, xy, engineering shear) of a transversely isotropic material
// with axis Z, scaled by factor.
Matrix6d constitutive(const Material &m, double factor)
{
    const double ex = m.E_xy * factor, ez = m.E_z * factor, nu = m.poisson;
    const double gxy = ex / (2. * (1. + nu));
    const double gz  = ez / (2. * (1. + nu));
    // The Poisson ratio between the plane and the axis uses the mean modulus, which keeps the compliance symmetric
    // and positive definite also for the fiber filled materials.
    const double s13 = - nu / std::sqrt(ex * ez);
    Matrix6d s = Matrix6d::Zero();
    s(0, 0) = s(1, 1) = 1. / ex;
    s(2, 2) = 1. / ez;
    s(0, 1) = s(1, 0) = - nu / ex;
    s(0, 2) = s(2, 0) = s(1, 2) = s(2, 1) = s13;
    s(3, 3) = s(4, 4) = 1. / gz;
    s(5, 5) = 1. / gxy;
    return s.inverse();
}

// Rotation from the material frame (Z = build direction, across the layers) to the frame of the setup.
Eigen::Matrix3d material_rotation(const Vec3d &build_direction)
{
    const Vec3d z = build_direction.norm() > 1e-9 ? build_direction.normalized() : Vec3d::UnitZ();
    return Eigen::Quaterniond::FromTwoVectors(Vec3d::UnitZ(), z).toRotationMatrix();
}

// Bond matrix: stress (xx, yy, zz, yz, xz, xy) in the frame rotated by r from the stress in the original frame;
// D' = M D M^T rotates a stiffness matrix with engineering shear strains.
Matrix6d bond_matrix(const Eigen::Matrix3d &a)
{
    Matrix6d m;
    const int p[3][2] = { { 1, 2 }, { 0, 2 }, { 0, 1 } };
    for (int i = 0; i < 3; ++ i) {
        for (int j = 0; j < 3; ++ j)
            m(i, j) = a(i, j) * a(i, j);
        for (int j = 0; j < 3; ++ j)
            m(i, 3 + j) = 2. * a(i, p[j][0]) * a(i, p[j][1]);
    }
    for (int i = 0; i < 3; ++ i) {
        const int r0 = p[i][0], r1 = p[i][1];
        for (int j = 0; j < 3; ++ j)
            m(3 + i, j) = a(r0, j) * a(r1, j);
        for (int j = 0; j < 3; ++ j) {
            const int c0 = p[j][0], c1 = p[j][1];
            m(3 + i, 3 + j) = a(r0, c0) * a(r1, c1) + a(r0, c1) * a(r1, c0);
        }
    }
    return m;
}

// Strain-displacement matrix of the unit cube at the local point (xi, eta, zeta) in [-1, 1]^3, for an edge h.
Matrix6x24d strain_matrix(double xi, double eta, double zeta, double h)
{
    Matrix6x24d b = Matrix6x24d::Zero();
    for (int a = 0; a < 8; ++ a) {
        const double sa = 2. * NODE_OFFSETS[a][0] - 1., ta = 2. * NODE_OFFSETS[a][1] - 1., ua = 2. * NODE_OFFSETS[a][2] - 1.;
        // dN/dx = dN/dxi * 2 / h
        const double dx = 0.125 * sa * (1. + ta * eta) * (1. + ua * zeta) * 2. / h;
        const double dy = 0.125 * ta * (1. + sa * xi) * (1. + ua * zeta) * 2. / h;
        const double dz = 0.125 * ua * (1. + sa * xi) * (1. + ta * eta) * 2. / h;
        const int c = 3 * a;
        b(0, c)     = dx;
        b(1, c + 1) = dy;
        b(2, c + 2) = dz;
        b(3, c + 1) = dz; b(3, c + 2) = dy;
        b(4, c)     = dz; b(4, c + 2) = dx;
        b(5, c)     = dy; b(5, c + 1) = dx;
    }
    return b;
}

Matrix24d element_stiffness(const Matrix6d &c, double h)
{
    const double g = 1. / std::sqrt(3.);
    const double det_j = h * h * h / 8.;
    Matrix24d k = Matrix24d::Zero();
    for (int i = 0; i < 8; ++ i) {
        const Matrix6x24d b = strain_matrix(NODE_OFFSETS[i][0] ? g : -g, NODE_OFFSETS[i][1] ? g : -g, NODE_OFFSETS[i][2] ? g : -g, h);
        k += b.transpose() * c * b * det_j;
    }
    return k;
}

struct Grid
{
    Vec3d origin;
    double h;
    Vec3i size;
    std::vector<int> voxels;                 // linear index of the solid voxels
    long long voxel_index(int i, int j, int k) const { return i + (long long)size.x() * (j + (long long)size.y() * k); }
    Vec3i voxel_ijk(int idx) const { return { idx % size.x(), (idx / size.x()) % size.y(), idx / (size.x() * size.y()) }; }
    long long node_index(int i, int j, int k) const { return i + (long long)(size.x() + 1) * (j + (long long)(size.y() + 1) * k); }
};

// Voxels of the grid whose center is inside the mesh (sorted linear indices).
bool voxelize_on_grid(const indexed_triangle_set &mesh, const Grid &grid, std::vector<int> &out, const std::function<bool()> &cancel)
{
    const double h = grid.h;
    out.clear();
    std::vector<float> zs(grid.size.z());
    for (int k = 0; k < grid.size.z(); ++ k)
        zs[k] = float(grid.origin.z() + (k + 0.5) * h);
    const std::vector<ExPolygons> slices = slice_mesh_ex(mesh, zs);
    for (int k = 0; k < grid.size.z(); ++ k) {
        if (cancel && cancel())
            return false;
        for (const ExPolygon &expoly : slices[k]) {
            const BoundingBox bb = get_extents(expoly);
            const int i0 = std::max(0, int(std::floor((unscaled(bb.min.x()) - grid.origin.x()) / h - 0.5)));
            const int i1 = std::min(grid.size.x() - 1, int(std::ceil((unscaled(bb.max.x()) - grid.origin.x()) / h - 0.5)));
            const int j0 = std::max(0, int(std::floor((unscaled(bb.min.y()) - grid.origin.y()) / h - 0.5)));
            const int j1 = std::min(grid.size.y() - 1, int(std::ceil((unscaled(bb.max.y()) - grid.origin.y()) / h - 0.5)));
            for (int j = j0; j <= j1; ++ j)
                for (int i = i0; i <= i1; ++ i) {
                    const Point p = Point::new_scale(grid.origin.x() + (i + 0.5) * h, grid.origin.y() + (j + 0.5) * h);
                    if (expoly.contains(p))
                        out.push_back(int(grid.voxel_index(i, j, k)));
                }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

bool voxelize(const indexed_triangle_set &mesh, const Setup &setup, Grid &grid, std::string &error, const std::function<bool()> &cancel)
{
    BoundingBoxf3 bbox;
    for (const Vec3f &v : mesh.vertices)
        bbox.merge(v.cast<double>());
    if (! bbox.defined || bbox.size().minCoeff() <= 0.) {
        error = "The part is empty or flat";
        return false;
    }
    double h = setup.voxel_size;
    if (h <= 0.) {
        const double volume = std::abs(its_volume(mesh));
        h = std::cbrt(std::max(volume, 1e-9) / double(std::max<size_t>(setup.target_voxels, 1)));
    }
    // Limit the number of nodes of the grid (thin and long parts have a big bounding box).
    const Vec3d extent = bbox.size();
    for (;;) {
        const Vec3d n = (extent / h).array().ceil();
        if ((n.x() + 1.) * (n.y() + 1.) * (n.z() + 1.) < 3e7)
            break;
        h *= 1.25;
    }
    grid.h = h;
    grid.size = Vec3i(int(std::ceil(extent.x() / h)), int(std::ceil(extent.y() / h)), int(std::ceil(extent.z() / h)));
    grid.size = grid.size.cwiseMax(Vec3i(1, 1, 1));
    // Center the grid on the part.
    grid.origin = bbox.min - 0.5 * (grid.size.cast<double>() * h - extent);

    if (! voxelize_on_grid(mesh, grid, grid.voxels, cancel)) {
        error = "Cancelled";
        return false;
    }
    if (grid.voxels.empty()) {
        error = "The part is too thin for the voxel size";
        return false;
    }
    return true;
}

// Nodes of the solid voxels closer than max_dist to the triangles.
std::vector<int> nodes_near_triangles(const indexed_triangle_set &mesh, const std::vector<int> &triangles,
                                      const std::vector<Vec3d> &node_pos, double max_dist)
{
    indexed_triangle_set sub;
    for (int t : triangles)
        if (t >= 0 && t < int(mesh.indices.size())) {
            const int base = int(sub.vertices.size());
            for (int c = 0; c < 3; ++ c)
                sub.vertices.emplace_back(mesh.vertices[mesh.indices[t][c]]);
            sub.indices.emplace_back(base, base + 1, base + 2);
        }
    std::vector<int> out;
    if (sub.indices.empty())
        return out;
    const auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(sub.vertices, sub.indices);
    const double max_dist2 = max_dist * max_dist;
    for (size_t n = 0; n < node_pos.size(); ++ n) {
        size_t      hit_idx;
        Vec3f       hit_point;
        const Vec3f pt = node_pos[n].cast<float>();
        const float d2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(sub.vertices, sub.indices, tree, pt, hit_idx, hit_point);
        if (d2 >= 0.f && d2 <= max_dist2)
            out.push_back(int(n));
    }
    return out;
}

// Fraction of material, stiffness factor and strength factor of every voxel with the printed structure.
struct VoxelMaterial
{
    std::vector<float> density;     // fraction of material
    std::vector<float> stiffness;   // factor of the stiffness of the solid material
    std::vector<float> strength;    // factor of the strength
    std::vector<char>  interior;
};

bool voxel_materials(const indexed_triangle_set &mesh, const InfillModel &infill, const Grid &grid, VoxelMaterial &out,
                     const std::function<bool()> &cancel)
{
    const size_t n_el = grid.voxels.size();
    out.density.assign(n_el, 1.f);
    out.stiffness.assign(n_el, 1.f);
    out.strength.assign(n_el, 1.f);
    out.interior.assign(n_el, 0);
    if (! infill.enabled)
        return true;
    const double h = grid.h;
    auto voxel_center = [&](size_t e) {
        const Vec3i ijk = grid.voxel_ijk(grid.voxels[e]);
        return Vec3d(grid.origin + h * (ijk.cast<double>() + Vec3d(0.5, 0.5, 0.5)));
    };

    // Infill of every voxel: density, stiffness and strength factors and the wall thickness. The part, then the
    // zones in order, or the field.
    const double base = std::clamp(infill.density, 0., 1.);
    auto stiff = [](double d, double n) { return std::pow(std::max(d, 0.02), n); };
    std::vector<double> rho(n_el, base), s_inf(n_el, stiff(base, infill.stiffness_exponent)),
                        t_inf(n_el, stiff(base, infill.strength_exponent)), wall(n_el, infill.wall_thickness);
    auto index_of = [&](int v) -> long long {
        const auto it = std::lower_bound(grid.voxels.begin(), grid.voxels.end(), v);
        return it != grid.voxels.end() && *it == v ? (long long)(it - grid.voxels.begin()) : -1;
    };
    if (infill.density_field) {
        for (size_t e = 0; e < n_el; ++ e) {
            rho[e]   = std::clamp(infill.density_field(voxel_center(e)), 0., 1.);
            s_inf[e] = stiff(rho[e], infill.stiffness_exponent);
            t_inf[e] = stiff(rho[e], infill.strength_exponent);
        }
    }
    for (const InfillZone &zone : infill.zones) {
        const double d  = std::clamp(zone.density, 0., 1.);
        const double nE = zone.stiffness_exponent > 0. ? zone.stiffness_exponent : infill.stiffness_exponent;
        const double nS = zone.strength_exponent  > 0. ? zone.strength_exponent  : infill.strength_exponent;
        if (zone.volume_fraction) {
            // Fraction of each voxel inside the mesh, from a grid 3 times finer.
            constexpr int sub = 3;
            Grid fine;
            fine.h      = h / sub;
            fine.origin = grid.origin;
            fine.size   = grid.size * sub;
            std::vector<int> inside;
            if (! voxelize_on_grid(zone.mesh, fine, inside, cancel))
                return false;
            std::vector<int> count(n_el, 0);
            for (int v : inside) {
                const Vec3i f = fine.voxel_ijk(v);
                const long long e = index_of(int(grid.voxel_index(f.x() / sub, f.y() / sub, f.z() / sub)));
                if (e >= 0)
                    ++ count[e];
            }
            std::vector<double> fraction(n_el, 0.);
            for (size_t e = 0; e < n_el; ++ e)
                fraction[e] = double(count[e]) / double(sub * sub * sub);
            if (zone.homogenize_cell > 0.) {
                // Average over the blocks (cells of the lattice), counting only the voxels of the part.
                BoundingBoxf3 zb;
                for (const Vec3f &v : zone.mesh.vertices)
                    zb.merge(v.cast<double>());
                std::map<long long, std::pair<double, int>> blocks;
                auto block_of = [&](size_t e) {
                    const Vec3d q = ((voxel_center(e) - zb.min) / zone.homogenize_cell).array().floor();
                    return (long long)(q.x() + 4096) + 8192LL * ((long long)(q.y() + 4096) + 8192LL * (long long)(q.z() + 4096));
                };
                for (size_t e = 0; e < n_el; ++ e) {
                    auto &b = blocks[block_of(e)];
                    b.first += fraction[e];
                    ++ b.second;
                }
                for (size_t e = 0; e < n_el; ++ e) {
                    const auto &b = blocks[block_of(e)];
                    fraction[e] = b.first / std::max(1, b.second);
                }
            }
            for (size_t e = 0; e < n_el; ++ e)
                if (fraction[e] > 0.) {
                    const double f = fraction[e];
                    rho[e]   = f * d + (1. - f) * rho[e];
                    s_inf[e] = f * stiff(d, nE) + (1. - f) * s_inf[e];
                    t_inf[e] = f * stiff(d, nS) + (1. - f) * t_inf[e];
                }
        } else {
            std::vector<int> inside;
            if (! voxelize_on_grid(zone.mesh, grid, inside, cancel))
                return false;
            for (int v : inside)
                if (const long long e = index_of(v); e >= 0) {
                    rho[e]   = d;
                    s_inf[e] = stiff(d, nE);
                    t_inf[e] = stiff(d, nS);
                    if (zone.wall_thickness >= 0.)
                        wall[e] = zone.wall_thickness;
                }
        }
    }

    // Solid shell: the part of the voxel closer to the surface than the shell thickness (walls or top / bottom,
    // after the normal of the closest surface).
    const auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(mesh.vertices, mesh.indices);
    for (size_t e = 0; e < n_el; ++ e) {
        if (e % 4096 == 0 && cancel && cancel())
            return false;
        const Vec3f c = voxel_center(e).cast<float>();
        size_t hit_idx = 0;
        Vec3f  hit_point;
        const float d2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(mesh.vertices, mesh.indices, tree, c, hit_idx, hit_point);
        const double d = d2 >= 0.f ? std::sqrt(double(d2)) : 0.;
        const double nz = std::abs(its_face_normal(mesh, int(hit_idx)).z());
        const double t = nz > 0.7 ? infill.top_bottom_thickness : wall[e];
        // Fraction of the voxel (along the normal) inside the shell.
        const double shell = std::clamp((t - (d - 0.5 * h)) / h, 0., 1.);
        out.interior[e]  = shell < 1.;
        out.density[e]   = float(shell + (1. - shell) * rho[e]);
        out.stiffness[e] = float(shell + (1. - shell) * s_inf[e]);
        out.strength[e]  = float(shell + (1. - shell) * t_inf[e]);
    }
    return true;
}

} // namespace

std::pair<double, double> infill_exponents(const std::string &pattern)
{
    // Stretch dominated patterns (straight lines crossing in the layer) are nearly linear with the density; bending
    // dominated ones lose stiffness faster. Approximate values, to calibrate with printed specimens.
    if (pattern == "rectilinear" || pattern == "alignedrectilinear" || pattern == "grid" || pattern == "line" ||
        pattern == "triangles" || pattern == "stars" || pattern == "monotonic" || pattern == "monotoniclines")
        return { 1.2, 1.2 };
    if (pattern == "honeycomb" || pattern == "3dhoneycomb")
        return { 1.4, 1.4 };
    if (pattern == "gyroid")
        return { 1.6, 1.5 };
    if (pattern == "cubic" || pattern == "adaptivecubic" || pattern == "supportcubic")
        return { 1.5, 1.4 };
    if (pattern == "lightning")
        // Lightning only supports the top surfaces: it is not structural.
        return { 2.5, 2.5 };
    return { 1.5, 1.5 };
}

const char* verdict_name(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Holds:            return "The part holds";
    case Verdict::LowMargin:        return "The part holds with a low safety margin";
    case Verdict::OutOfLoad:        return "Out of load: the stresses exceed the strength of the material";
    case Verdict::TooFlexible:      return "Too flexible: a load moves more than the allowed deformation";
    case Verdict::OutOfTemperature: return "Out of temperature: the material is not structural at this temperature";
    case Verdict::NotApplicable:    return "The linear analysis does not describe this material";
    }
    return "";
}

Result analyze(const indexed_triangle_set &mesh, const Setup &setup, std::function<bool()> cancel, std::function<void(int)> progress)
{
    Result res;
    auto report = [&](int p) { if (progress) progress(p); };
    auto cancelled = [&]() { return cancel && cancel(); };

    const Material *material = find_material(setup.material);
    if (! material) {
        res.error = "Unknown material " + setup.material;
        return res;
    }
    if (setup.fixtures.empty()) {
        res.error = "The part has no fixed faces";
        return res;
    }
    if (setup.loads.empty()) {
        res.error = "The part has no loads";
        return res;
    }

    // 1) Voxels.
    Grid grid;
    if (! voxelize(mesh, setup, grid, res.error, cancel))
        return res;
    report(10);
    const double h = grid.h;
    res.origin = grid.origin;
    res.h      = h;
    res.size   = grid.size;

    // 2) Nodes used by the solid voxels.
    std::vector<int> node_id(size_t(grid.size.x() + 1) * size_t(grid.size.y() + 1) * size_t(grid.size.z() + 1), -1);
    std::vector<Vec3d> node_pos;
    std::vector<std::array<int, 8>> elements(grid.voxels.size());
    for (size_t e = 0; e < grid.voxels.size(); ++ e) {
        const Vec3i ijk = grid.voxel_ijk(grid.voxels[e]);
        for (int a = 0; a < 8; ++ a) {
            const int i = ijk.x() + NODE_OFFSETS[a][0], j = ijk.y() + NODE_OFFSETS[a][1], k = ijk.z() + NODE_OFFSETS[a][2];
            int &id = node_id[grid.node_index(i, j, k)];
            if (id < 0) {
                id = int(node_pos.size());
                node_pos.emplace_back(grid.origin + h * Vec3d(i, j, k));
            }
            elements[e][a] = id;
        }
    }
    const size_t n_dofs = 3 * node_pos.size();

    // Printed structure: shell and infill.
    VoxelMaterial vm_mat;
    if (! voxel_materials(mesh, setup.infill, grid, vm_mat, cancel)) {
        res.error = "Cancelled";
        return res;
    }

    // 3) Boundary conditions. The surface of the mesh is within a voxel of the boundary nodes.
    const double surface_dist = 0.9 * h;
    std::vector<char> fixed(n_dofs, 0);
    size_t n_fixed_nodes = 0;
    for (const Fixture &fixture : setup.fixtures)
        for (int n : nodes_near_triangles(mesh, fixture.triangles, node_pos, surface_dist)) {
            if (! fixed[3 * n])
                ++ n_fixed_nodes;
            fixed[3 * n] = fixed[3 * n + 1] = fixed[3 * n + 2] = 1;
        }
    if (n_fixed_nodes < 3) {
        res.error = "The fixed faces do not touch the part (or are too small for the voxel size)";
        return res;
    }
    std::vector<double> f(n_dofs, 0.);
    std::vector<std::vector<int>> load_nodes;
    for (const Load &load : setup.loads) {
        std::vector<int> nodes;
        if (load.type == Load::Type::Faces)
            nodes = nodes_near_triangles(mesh, load.triangles, node_pos, surface_dist);
        else {
            const double r = load.radius > 0. ? std::max(load.radius, 0.5 * h) : 1.5 * h;
            int    nearest = -1;
            double nearest_d2 = std::numeric_limits<double>::max();
            for (size_t n = 0; n < node_pos.size(); ++ n) {
                const double d2 = (node_pos[n] - load.point).squaredNorm();
                if (d2 <= r * r)
                    nodes.push_back(int(n));
                if (d2 < nearest_d2) {
                    nearest_d2 = d2;
                    nearest = int(n);
                }
            }
            if (nodes.empty() && nearest >= 0 && nearest_d2 <= 9. * h * h)
                nodes.push_back(nearest);
        }
        if (nodes.empty()) {
            res.error = "A load is not applied on the part";
            return res;
        }
        const Vec3d per_node = load.force / double(nodes.size());
        for (int n : nodes)
            for (int c = 0; c < 3; ++ c)
                f[3 * n + c] += per_node[c];
        load_nodes.emplace_back(std::move(nodes));
    }
    for (size_t d = 0; d < n_dofs; ++ d)
        if (fixed[d])
            f[d] = 0.;

    // 4) Element matrix: the same for all the voxels.
    res.temperature_factor = temperature_factor(*material, setup.temperature);
    res.layer_adhesion_factor = layer_adhesion_factor(*material, setup.print_temperature);
    // Transversely isotropic material with its axis along the build direction, or along the normal of the curved
    // layer at each voxel (grouped in directions, each with its own element matrix).
    struct Orientation {
        Matrix6d  c;          // constitutive matrix in the frame of the setup
        Matrix6d  to_layers;  // stress in the frame of the layers
        Matrix24d ke;
    };
    std::vector<Orientation> orientations;
    std::vector<uint16_t>    orientation_of(grid.voxels.size(), 0);
    const Matrix6d c_material = constitutive(*material, res.temperature_factor);
    auto add_orientation = [&](const Vec3d &normal) {
        const Eigen::Matrix3d to_setup   = material_rotation(normal);
        const Matrix6d        to_setup_m = bond_matrix(to_setup);
        Orientation o;
        o.c         = to_setup_m * c_material * to_setup_m.transpose();
        o.to_layers = bond_matrix(to_setup.transpose());
        o.ke        = element_stiffness(o.c, h);
        orientations.emplace_back(std::move(o));
    };
    if (! setup.layer_normal) {
        add_orientation(setup.build_direction);
    } else {
        // Groups: polar angle in steps of LAYER_NORMAL_STEP_DEG, azimuth in steps of about the same arc. A layer
        // normal and its opposite are the same material direction.
        const double step = LAYER_NORMAL_STEP_DEG * M_PI / 180.;
        std::map<std::pair<int, int>, uint16_t> group_of;
        for (size_t e = 0; e < grid.voxels.size(); ++ e) {
            const Vec3i ijk    = grid.voxel_ijk(grid.voxels[e]);
            Vec3d       normal = setup.layer_normal(grid.origin + h * (ijk.cast<double>() + Vec3d(0.5, 0.5, 0.5)));
            if (! (normal.norm() > 1e-9))
                normal = setup.build_direction;
            normal.normalize();
            if (normal.z() < 0.)
                normal = - normal;
            const double theta = std::acos(std::clamp(normal.z(), -1., 1.));
            const int    it    = int(std::lround(theta / step));
            const double theta_q = it * step;
            const int    n_phi = std::max(1, int(std::lround(2. * M_PI * std::sin(theta_q) / step)));
            const int    ip    = it == 0 ? 0 : int(std::lround((std::atan2(normal.y(), normal.x()) + M_PI) / (2. * M_PI) * n_phi)) % n_phi;
            auto [found, added] = group_of.try_emplace({ it, ip }, uint16_t(orientations.size()));
            if (added) {
                if (orientations.size() >= 65535) {
                    res.error = "Too many layer orientations";
                    return res;
                }
                const double phi = 2. * M_PI * ip / n_phi - M_PI;
                add_orientation(Vec3d(std::sin(theta_q) * std::cos(phi), std::sin(theta_q) * std::sin(phi), std::cos(theta_q)));
            }
            orientation_of[e] = found->second;
        }
    }
    res.layer_orientations = orientations.size();

    // Groups of voxels without shared nodes, for the parallel product.
    std::array<std::vector<int>, 8> colors;
    for (size_t e = 0; e < grid.voxels.size(); ++ e) {
        const Vec3i ijk = grid.voxel_ijk(grid.voxels[e]);
        colors[(ijk.x() & 1) | ((ijk.y() & 1) << 1) | ((ijk.z() & 1) << 2)].push_back(int(e));
    }
    auto multiply = [&](const std::vector<double> &x, std::vector<double> &y) {
        std::fill(y.begin(), y.end(), 0.);
        for (const std::vector<int> &color : colors)
            tbb::parallel_for(tbb::blocked_range<size_t>(0, color.size(), 256), [&](const tbb::blocked_range<size_t> &range) {
                Eigen::Matrix<double, 24, 1> xe, ye;
                for (size_t ci = range.begin(); ci < range.end(); ++ ci) {
                    const std::array<int, 8> &nodes = elements[color[ci]];
                    for (int a = 0; a < 8; ++ a)
                        for (int k = 0; k < 3; ++ k)
                            xe[3 * a + k] = x[3 * nodes[a] + k];
                    ye.noalias() = (double(vm_mat.stiffness[color[ci]]) * orientations[orientation_of[color[ci]]].ke) * xe;
                    for (int a = 0; a < 8; ++ a)
                        for (int k = 0; k < 3; ++ k)
                            y[3 * nodes[a] + k] += ye[3 * a + k];
                }
            });
        for (size_t d = 0; d < n_dofs; ++ d)
            if (fixed[d])
                y[d] = 0.;
    };
    std::vector<double> diag(n_dofs, 0.);
    for (size_t e = 0; e < elements.size(); ++ e)
        for (int a = 0; a < 8; ++ a)
            for (int k = 0; k < 3; ++ k)
                diag[3 * elements[e][a] + k] += double(vm_mat.stiffness[e]) * orientations[orientation_of[e]].ke(3 * a + k, 3 * a + k);
    report(15);

    // 5) Preconditioned conjugate gradient.
    auto dot = [&](const std::vector<double> &a, const std::vector<double> &b) {
        double s = 0.;
        for (size_t i = 0; i < a.size(); ++ i)
            s += a[i] * b[i];
        return s;
    };
    std::vector<double> u(n_dofs, 0.), r = f, z(n_dofs), p(n_dofs), q(n_dofs);
    for (size_t d = 0; d < n_dofs; ++ d)
        z[d] = fixed[d] ? 0. : r[d] / diag[d];
    p = z;
    double rz = dot(r, z);
    const double f_norm = std::sqrt(dot(f, f));
    if (f_norm == 0.) {
        res.error = "The loads are zero";
        return res;
    }
    const int max_iterations = int(std::max<size_t>(2000, 20 * size_t(std::cbrt(double(n_dofs)) * 10.)));
    const double log_start = 0., log_end = std::log10(setup.tolerance);
    double rel = 1.;
    int it = 0;
    for (; it < max_iterations; ++ it) {
        if (it % 20 == 0) {
            if (cancelled()) {
                res.error = "Cancelled";
                return res;
            }
            const double done = std::clamp((std::log10(std::max(rel, 1e-30)) - log_start) / (log_end - log_start), 0., 1.);
            report(15 + int(80. * done));
        }
        multiply(p, q);
        const double pq = dot(p, q);
        if (pq <= 0.) {
            res.error = "The part is not held enough: it can move or rotate freely";
            return res;
        }
        const double alpha = rz / pq;
        for (size_t d = 0; d < n_dofs; ++ d) {
            u[d] += alpha * p[d];
            r[d] -= alpha * q[d];
        }
        rel = std::sqrt(dot(r, r)) / f_norm;
        if (rel < setup.tolerance)
            break;
        for (size_t d = 0; d < n_dofs; ++ d)
            z[d] = fixed[d] ? 0. : r[d] / diag[d];
        const double rz_new = dot(r, z);
        const double beta = rz_new / rz;
        rz = rz_new;
        for (size_t d = 0; d < n_dofs; ++ d)
            p[d] = z[d] + beta * p[d];
    }
    res.iterations = it;
    res.residual   = rel;
    if (rel >= setup.tolerance * 100.) {
        res.error = "The solver did not converge: the part is probably not held enough";
        return res;
    }

    // 6) Stresses at the centers of the voxels.
    const Matrix6x24d b0 = strain_matrix(0., 0., 0., h);
    const size_t n_el = grid.voxels.size();
    res.voxels = grid.voxels;
    res.von_mises.resize(n_el);
    res.failure_index.resize(n_el);
    res.displacement.resize(n_el);
    res.density  = vm_mat.density;
    res.interior = vm_mat.interior;
    // Stress measures for the safety factors of the other materials.
    std::vector<double> sz_tension(n_el), tau_z(n_el), vm_eff(n_el);
    const double s_xy = material->strength_xy * res.temperature_factor;
    const double s_z  = material->strength_z  * res.temperature_factor * res.layer_adhesion_factor;
    for (size_t e = 0; e < n_el; ++ e) {
        Eigen::Matrix<double, 24, 1> ue;
        Vec3d disp = Vec3d::Zero();
        for (int a = 0; a < 8; ++ a)
            for (int k = 0; k < 3; ++ k) {
                ue[3 * a + k] = u[3 * elements[e][a] + k];
                disp[k] += 0.125 * ue[3 * a + k];
            }
        // Homogenized stress of the voxel; the strength of the voxel is scaled the same way as the material.
        // Stress in the frame of the layers: z across the layers, yz and xz the shear between the layers.
        const Orientation &o = orientations[orientation_of[e]];
        const Eigen::Matrix<double, 6, 1> s = o.to_layers * (double(vm_mat.stiffness[e]) * (o.c * (b0 * ue)));
        const double sf_e = std::max(double(vm_mat.strength[e]), 1e-6);
        const double vm = std::sqrt(0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) + (s[2] - s[0]) * (s[2] - s[0]))
                                    + 3. * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
        sz_tension[e] = std::max(0., s[2]) / sf_e;
        tau_z[e]      = std::sqrt(s[3] * s[3] + s[4] * s[4]) / sf_e;
        // Simplified criterion: von Mises against the strength along the layers, tension and shear across the layers
        // against the layer adhesion (shear strength between layers taken as 0.6 of the tensile one).
        const double fi = std::max({ vm / sf_e / s_xy, sz_tension[e] / s_z, tau_z[e] / (0.6 * s_z) });
        vm_eff[e] = vm / sf_e;
        res.von_mises[e]     = float(vm);
        res.failure_index[e] = float(fi);
        res.displacement[e]  = disp.cast<float>();
        res.max_von_mises    = std::max(res.max_von_mises, vm);
        res.max_displacement = std::max(res.max_displacement, disp.norm());
        if (fi > res.max_failure_index) {
            res.max_failure_index = fi;
            const Vec3i ijk = grid.voxel_ijk(grid.voxels[e]);
            res.critical_point = grid.origin + h * (ijk.cast<double>() + Vec3d(0.5, 0.5, 0.5));
        }
    }
    res.safety_factor = res.max_failure_index > 0. ? 1. / res.max_failure_index : std::numeric_limits<double>::infinity();

    // Mass [g]: voxel volume [mm³] / 1000 * density [g/cm³].
    const double voxel_mass = h * h * h * 1e-3 * material->density;
    for (size_t e = 0; e < n_el; ++ e)
        res.mass += voxel_mass * vm_mat.density[e];
    res.solid_mass = voxel_mass * double(n_el);

    // Displacement where each load is applied, and the stiffness needed to keep it within its limit.
    bool   too_flexible = false;
    // Factor by which the stiffness must grow to meet all the limits (1 = they are met).
    double stiffness_needed = 1.;
    for (size_t l = 0; l < setup.loads.size(); ++ l) {
        double d = 0.;
        for (int n : load_nodes[l])
            d = std::max(d, Vec3d(u[3 * n], u[3 * n + 1], u[3 * n + 2]).norm());
        res.load_displacement.push_back(d);
        if (setup.loads[l].max_displacement > 0.) {
            stiffness_needed = std::max(stiffness_needed, d / setup.loads[l].max_displacement);
            too_flexible |= d > setup.loads[l].max_displacement;
        }
    }

    // 7) Verdict.
    if (! material->linear_analysis_valid)
        res.verdict = Verdict::NotApplicable;
    else if (out_of_temperature(*material, setup.temperature))
        res.verdict = Verdict::OutOfTemperature;
    else if (res.max_failure_index > 1.)
        res.verdict = Verdict::OutOfLoad;
    else if (too_flexible)
        res.verdict = Verdict::TooFlexible;
    else if (res.safety_factor < setup.required_safety_factor)
        res.verdict = Verdict::LowMargin;
    else
        res.verdict = Verdict::Holds;

    // Other materials with the same stresses (exact for isotropic materials, approximate for the others).
    if (res.verdict != Verdict::Holds) {
        for (const Material &m : materials()) {
            if (m.key == material->key || ! m.linear_analysis_valid || out_of_temperature(m, setup.temperature))
                continue;
            const double tf = temperature_factor(m, setup.temperature);
            double fi_max = 0.;
            for (size_t e = 0; e < n_el; ++ e)
                fi_max = std::max({ fi_max, vm_eff[e] / (m.strength_xy * tf), sz_tension[e] / (m.strength_z * tf),
                                    tau_z[e] / (0.6 * m.strength_z * tf) });
            const double sf = fi_max > 0. ? 1. / fi_max : std::numeric_limits<double>::infinity();
            const double stiffness_ratio = (m.E_xy * tf) / (material->E_xy * res.temperature_factor);
            if (sf >= setup.required_safety_factor && stiffness_ratio >= stiffness_needed)
                res.alternatives.emplace_back(m.key, sf);
        }
        std::sort(res.alternatives.begin(), res.alternatives.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    }

    report(100);
    res.ok = true;
    return res;
}

} // namespace Fea
} // namespace Slic3r
