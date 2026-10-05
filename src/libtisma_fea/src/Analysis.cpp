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

    std::vector<float> zs(grid.size.z());
    for (int k = 0; k < grid.size.z(); ++ k)
        zs[k] = float(grid.origin.z() + (k + 0.5) * h);
    const std::vector<ExPolygons> slices = slice_mesh_ex(mesh, zs);

    for (int k = 0; k < grid.size.z(); ++ k) {
        if (cancel && cancel()) {
            error = "Cancelled";
            return false;
        }
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
                        grid.voxels.push_back(int(grid.voxel_index(i, j, k)));
                }
        }
    }
    std::sort(grid.voxels.begin(), grid.voxels.end());
    grid.voxels.erase(std::unique(grid.voxels.begin(), grid.voxels.end()), grid.voxels.end());
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

} // namespace

const char* verdict_name(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Holds:            return "The part holds";
    case Verdict::LowMargin:        return "The part holds with a low safety margin";
    case Verdict::OutOfLoad:        return "Out of load: the stresses exceed the strength of the material";
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
    }
    for (size_t d = 0; d < n_dofs; ++ d)
        if (fixed[d])
            f[d] = 0.;

    // 4) Element matrix: the same for all the voxels.
    res.temperature_factor = temperature_factor(*material, setup.temperature);
    const Matrix6d  c  = constitutive(*material, res.temperature_factor);
    const Matrix24d ke = element_stiffness(c, h);

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
                    ye.noalias() = ke * xe;
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
    for (const std::array<int, 8> &nodes : elements)
        for (int a = 0; a < 8; ++ a)
            for (int k = 0; k < 3; ++ k)
                diag[3 * nodes[a] + k] += ke(3 * a + k, 3 * a + k);
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
    // Stress measures for the safety factors of the other materials.
    std::vector<double> sz_tension(n_el), tau_z(n_el);
    const double s_xy = material->strength_xy * res.temperature_factor;
    const double s_z  = material->strength_z  * res.temperature_factor;
    for (size_t e = 0; e < n_el; ++ e) {
        Eigen::Matrix<double, 24, 1> ue;
        Vec3d disp = Vec3d::Zero();
        for (int a = 0; a < 8; ++ a)
            for (int k = 0; k < 3; ++ k) {
                ue[3 * a + k] = u[3 * elements[e][a] + k];
                disp[k] += 0.125 * ue[3 * a + k];
            }
        const Eigen::Matrix<double, 6, 1> s = c * (b0 * ue);
        const double vm = std::sqrt(0.5 * ((s[0] - s[1]) * (s[0] - s[1]) + (s[1] - s[2]) * (s[1] - s[2]) + (s[2] - s[0]) * (s[2] - s[0]))
                                    + 3. * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
        sz_tension[e] = std::max(0., s[2]);
        tau_z[e]      = std::sqrt(s[3] * s[3] + s[4] * s[4]);
        // Simplified criterion: von Mises against the strength along the layers, tension and shear across the layers
        // against the layer adhesion (shear strength between layers taken as 0.6 of the tensile one).
        const double fi = std::max({ vm / s_xy, sz_tension[e] / s_z, tau_z[e] / (0.6 * s_z) });
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

    // 7) Verdict.
    if (! material->linear_analysis_valid)
        res.verdict = Verdict::NotApplicable;
    else if (out_of_temperature(*material, setup.temperature))
        res.verdict = Verdict::OutOfTemperature;
    else if (res.max_failure_index > 1.)
        res.verdict = Verdict::OutOfLoad;
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
                fi_max = std::max({ fi_max, res.von_mises[e] / (m.strength_xy * tf), sz_tension[e] / (m.strength_z * tf),
                                    tau_z[e] / (0.6 * m.strength_z * tf) });
            const double sf = fi_max > 0. ? 1. / fi_max : std::numeric_limits<double>::infinity();
            if (sf >= setup.required_safety_factor)
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
