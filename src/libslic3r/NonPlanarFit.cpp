///|/ Tisma Slicer: layer offsets fitted to the slopes asked by the stresses or by the top surfaces.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "NonPlanar.hpp"

#include <algorithm>
#include <cmath>

#include <Eigen/IterativeLinearSolvers>
#include <Eigen/Sparse>

#include <admesh/stl.h>

#include "libslic3r/Engineering.hpp"

namespace Slic3r {
namespace NonPlanar {

namespace {

// Weights of the terms of the least squares, relative to the targets (normalized to one per node).
constexpr double WEIGHT_SMALL     = 1e-3;  // small offsets where nothing else decides
constexpr double WEIGHT_FLAT      = 100.;  // flat first layers
// Depth of the skin under the top surfaces that follows them [mm] and the number of samples across it.
constexpr double SKIN_DEPTH   = 3.;
constexpr int    SKIN_SAMPLES = 5;

} // namespace

bool fit_layer_field(const std::vector<SlopeTarget> &targets, const BoundingBoxf3 &bbox, const LayerFitParams &params,
                     EngineeringLayerField &out)
{
    if (targets.empty() || ! bbox.defined)
        return false;
    const Vec3d size = bbox.size();

    // Grid of the offset with one cell of margin.
    double cell = std::max({ params.min_cell, 1e-3,
                             std::cbrt(size.x() * size.y() * size.z() / double(std::max<size_t>(params.max_nodes, 8))) });
    int nx, ny, nz;
    for (;;) {
        nx = int(std::ceil(size.x() / cell)) + 3;
        ny = int(std::ceil(size.y() / cell)) + 3;
        nz = int(std::ceil(size.z() / cell)) + 3;
        if (size_t(nx) * size_t(ny) * size_t(nz) <= std::max<size_t>(params.max_nodes, 27))
            break;
        cell *= 1.1;
    }
    const Vec3d origin = bbox.min - Vec3d(cell, cell, cell);
    const int   n      = nx * ny * nz;
    auto node = [nx, ny](int i, int j, int k) { return (k * ny + j) * nx + i; };

    // Rows of A x = b.
    std::vector<Eigen::Triplet<double>> triplets;
    std::vector<double> rhs;
    int row = 0;
    // The slopes along the directions of the targets (gradient of the trilinear interpolation).
    double sum_w2 = 0.;
    for (const SlopeTarget &t : targets)
        sum_w2 += t.weight * t.weight;
    if (sum_w2 <= 0.)
        return false;
    const double data_scale = std::sqrt(params.target_weight * double(n) / sum_w2);
    for (const SlopeTarget &t : targets) {
        const Vec3d f = (t.point - origin) / cell;
        const int i = std::clamp(int(f.x()), 0, nx - 2), j = std::clamp(int(f.y()), 0, ny - 2), k = std::clamp(int(f.z()), 0, nz - 2);
        const double tx = f.x() - i, ty = f.y() - j, tz = f.z() - k;
        const double w = t.weight * data_scale;
        for (int c = 0; c < 8; ++ c) {
            const int a = c & 1, b = (c >> 1) & 1, d = c >> 2;
            const double wz = d ? tz : 1. - tz;
            const double gx = (a ? 1. : -1.) * (b ? ty : 1. - ty) * wz / cell;
            const double gy = (a ? tx : 1. - tx) * (b ? 1. : -1.) * wz / cell;
            const double v  = w * (gx * t.dir.x() + gy * t.dir.y());
            if (v != 0.)
                triplets.emplace_back(row, node(i + a, j + b, k + d), v);
        }
        rhs.push_back(w * t.slope);
        ++ row;
    }
    const double w_thick = std::sqrt(params.thickness_weight) / cell, w_smooth = std::sqrt(params.smooth_weight) / cell,
                 w_small = std::sqrt(WEIGHT_SMALL) / cell, w_flat = std::sqrt(WEIGHT_FLAT) / cell;
    const double z_flat = bbox.min.z() + params.z_flat;
    for (int k = 0; k < nz; ++ k)
        for (int j = 0; j < ny; ++ j)
            for (int i = 0; i < nx; ++ i) {
                const int me = node(i, j, k);
                if (k + 1 < nz) {
                    triplets.emplace_back(row, node(i, j, k + 1), w_thick);
                    triplets.emplace_back(row, me, - w_thick);
                    rhs.push_back(0.);
                    ++ row;
                }
                if (i > 0 && i + 1 < nx) {
                    triplets.emplace_back(row, node(i - 1, j, k), w_smooth);
                    triplets.emplace_back(row, me, -2. * w_smooth);
                    triplets.emplace_back(row, node(i + 1, j, k), w_smooth);
                    rhs.push_back(0.);
                    ++ row;
                }
                if (j > 0 && j + 1 < ny) {
                    triplets.emplace_back(row, node(i, j - 1, k), w_smooth);
                    triplets.emplace_back(row, me, -2. * w_smooth);
                    triplets.emplace_back(row, node(i, j + 1, k), w_smooth);
                    rhs.push_back(0.);
                    ++ row;
                }
                const bool flat = origin.z() + k * cell <= z_flat;
                triplets.emplace_back(row, me, flat ? w_flat : w_small);
                rhs.push_back(0.);
                ++ row;
            }
    Eigen::SparseMatrix<double> a(row, n);
    a.setFromTriplets(triplets.begin(), triplets.end());
    const Eigen::VectorXd b = Eigen::Map<const Eigen::VectorXd>(rhs.data(), Eigen::Index(rhs.size()));
    const Eigen::SparseMatrix<double> ata = a.transpose() * a;
    const Eigen::VectorXd atb = a.transpose() * b;
    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper> solver;
    solver.setTolerance(1e-6);
    solver.setMaxIterations(5000);
    solver.compute(ata);
    const Eigen::VectorXd x = solver.solve(atb);
    if (solver.info() != Eigen::Success && solver.error() > 1e-3)
        return false;

    out = EngineeringLayerField();
    out.origin    = origin - bbox.min;
    out.cell      = cell;
    out.nx        = nx;
    out.ny        = ny;
    out.nz        = nz;
    out.part_size = size;
    out.offset.resize(n);
    for (int i = 0; i < n; ++ i)
        out.offset[i] = float(x[i]);
    return true;
}

std::vector<SlopeTarget> surface_slope_targets(const indexed_triangle_set &mesh, double tan_x, double tan_y, double z_flat)
{
    std::vector<SlopeTarget> out;
    float z_min = std::numeric_limits<float>::max();
    for (const stl_vertex &v : mesh.vertices)
        z_min = std::min(z_min, v.z());
    // The faces are sampled about every millimetre (a large face asks for its slope all over it, not only at its
    // center), with fewer samples on large meshes.
    double up_area = 0.;
    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const Vec3d cross = (mesh.vertices[f[1]] - mesh.vertices[f[0]]).cast<double>().cross((mesh.vertices[f[2]] - mesh.vertices[f[0]]).cast<double>());
        if (cross.z() > 0.)
            up_area += 0.5 * cross.norm();
    }
    constexpr double MAX_SAMPLES = 50000.;
    const double spacing = std::max(1., std::sqrt(up_area / MAX_SAMPLES));
    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const Vec3d a = mesh.vertices[f[0]].cast<double>(), b = mesh.vertices[f[1]].cast<double>(), c = mesh.vertices[f[2]].cast<double>();
        const Vec3d cross = (b - a).cross(c - a);
        const double area2 = cross.norm();
        if (area2 < 1e-12)
            continue;
        const Vec3d n = cross / area2;
        // Looking up (a face at least 2° off the vertical).
        if (n.z() < 0.035)
            continue;
        // Slope of the face, as steep as the head allows along its direction.
        Vec2d g(- n.x() / n.z(), - n.y() / n.z());
        if (tan_x > 0. && tan_y > 0.) {
            const double r = std::hypot(g.x() / tan_x, g.y() / tan_y);
            if (r > 1.)
                g /= r;
        }
        // k x k sub-triangles, one sample at the center of each.
        const int    k      = std::max(1, int(std::ceil(std::max({ (b - a).norm(), (c - b).norm(), (a - c).norm() }) / spacing)));
        const double weight = std::sqrt(0.5 * area2 / (double(k) * k)) * n.z();
        // The skin under the face (SKIN_DEPTH) follows it: the same slope at several depths, so that the layers of
        // the skin are parallel to the top instead of the offset bending only right at the surface.
        auto add = [&](double u, double v) {
            const Vec3d p = a + u * (b - a) + v * (c - a);
            for (int s = 0; s < SKIN_SAMPLES; ++ s) {
                const Vec3d q = p - Vec3d(0., 0., SKIN_DEPTH * s / std::max(1, SKIN_SAMPLES - 1));
                if (q.z() - z_min <= z_flat)
                    break;
                out.push_back({ q, Vec2d::UnitX(), g.x(), weight });
                out.push_back({ q, Vec2d::UnitY(), g.y(), weight });
            }
        };
        for (int i = 0; i < k; ++ i)
            for (int j = 0; i + j < k; ++ j) {
                add((i + 1. / 3.) / k, (j + 1. / 3.) / k);
                if (i + j < k - 1)
                    add((i + 2. / 3.) / k, (j + 2. / 3.) / k);
            }
    }
    return out;
}

} // namespace NonPlanar
} // namespace Slic3r
