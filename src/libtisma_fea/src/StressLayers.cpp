///|/ Tisma Slicer: curved layers that follow the stresses of the structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/StressLayers.hpp"

#include <algorithm>
#include <cmath>

#include <Eigen/Eigenvalues>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/Sparse>

#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/NonPlanar.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace Slic3r {
namespace Fea {

namespace {

// Weights of the terms of the least squares, relative to the slopes asked by the stresses (normalized to one per node).
constexpr double WEIGHT_THICKNESS = 1.;    // change of the offset along Z (layer thickness)
constexpr double WEIGHT_SMOOTH    = 0.3;   // change of the slope from a cell to the next one
constexpr double WEIGHT_SMALL     = 1e-3;  // small offsets where nothing else decides
constexpr double WEIGHT_FLAT      = 100.;  // flat first layers
// Pulls closer to the vertical than this (horizontal part of the unit direction) are left alone: the layers cannot
// tilt enough to help.
constexpr double MIN_HORIZONTAL = 0.05;

} // namespace

StressLayersResult stress_layers(const indexed_triangle_set &mesh, const Result &analysis, const StressLayersParams &params)
{
    StressLayersResult out;
    if (! analysis.ok || analysis.stress.size() != analysis.voxels.size() || analysis.voxels.empty()) {
        out.error = "Run the analysis first";
        return out;
    }
    BoundingBoxf3 bbox;
    for (const stl_vertex &v : mesh.vertices)
        bbox.merge(v.cast<double>());
    if (! bbox.defined) {
        out.error = "Empty part";
        return out;
    }
    const Vec3d size = bbox.size();

    // Grid of the offset: the voxels of the analysis or larger, with one cell of margin.
    double cell = std::max({ analysis.h, 1., std::cbrt(size.x() * size.y() * size.z() / double(std::max<size_t>(params.max_nodes, 8))) });
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

    // 1) Slopes asked by the stresses.
    struct Target { Vec3d p; Vec2d dir; double slope; double weight; };
    std::vector<Target> targets;
    double max_tension = 0.;
    for (const std::array<float, 6> &s : analysis.stress) {
        Eigen::Matrix3d m;
        m << s[0], s[5], s[4], s[5], s[1], s[3], s[4], s[3], s[2];
        max_tension = std::max(max_tension, Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d>(m, Eigen::EigenvaluesOnly).eigenvalues()(2));
    }
    if (max_tension <= 0.) {
        out.error = "The part is not pulled anywhere: flat layers are fine";
        return out;
    }
    const Vec3i &gs = analysis.size;
    for (size_t e = 0; e < analysis.voxels.size(); ++ e) {
        const std::array<float, 6> &s = analysis.stress[e];
        Eigen::Matrix3d m;
        m << s[0], s[5], s[4], s[5], s[1], s[3], s[4], s[3], s[2];
        const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eig(m);
        const double tension = eig.eigenvalues()(2);
        if (tension <= 0.01 * max_tension)
            continue;
        Vec3d pull = eig.eigenvectors().col(2);
        if (pull.z() < 0.)
            pull = - pull;
        const double horizontal = std::hypot(pull.x(), pull.y());
        if (horizontal < MIN_HORIZONTAL)
            continue;
        const Vec2d dir(pull.x() / horizontal, pull.y() / horizontal);
        // Steepest slope the head allows along dir (elliptic cone of the free angles along X and Y).
        const double head  = 1. / std::hypot(dir.x() / params.tan_x, dir.y() / params.tan_y);
        const double slope = std::min(pull.z() / horizontal, head);
        const int idx = analysis.voxels[e];
        const Vec3i ijk(idx % gs.x(), (idx / gs.x()) % gs.y(), idx / (gs.x() * gs.y()));
        const Vec3d p = analysis.origin + analysis.h * (ijk.cast<double>() + Vec3d(0.5, 0.5, 0.5));
        // The tension across the layers is about tension * (pull_z - slope * horizontal)^2: the error of the slope
        // costs as much as the tension times the horizontal part of the pull.
        targets.push_back({ p, dir, slope, (tension / max_tension) * horizontal });
        ++ out.tension_voxels;
    }
    if (targets.empty()) {
        out.error = "The part is pulled only vertically: tilting the layers would not help";
        return out;
    }

    // 2) Least squares: rows of A x = b.
    std::vector<Eigen::Triplet<double>> triplets;
    std::vector<double> rhs;
    int row = 0;
    // Slopes along the direction of the pull, at the voxel centers (gradient of the trilinear interpolation).
    double sum_w2 = 0.;
    for (const Target &t : targets)
        sum_w2 += t.weight * t.weight;
    const double data_scale = std::sqrt(double(n) / sum_w2);
    for (const Target &t : targets) {
        const Vec3d f = (t.p - origin) / cell;
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
    const double w_thick = std::sqrt(WEIGHT_THICKNESS) / cell, w_smooth = std::sqrt(WEIGHT_SMOOTH) / cell,
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
    if (solver.info() != Eigen::Success && solver.error() > 1e-3) {
        out.error = "The layer shape did not converge";
        return out;
    }

    EngineeringLayerField &field = out.field;
    field.origin    = origin - bbox.min;
    field.cell      = cell;
    field.nx        = nx;
    field.ny        = ny;
    field.nz        = nz;
    field.part_size = size;
    field.offset.resize(n);
    for (int i = 0; i < n; ++ i)
        field.offset[i] = float(x[i]);
    out.ok = true;
    return out;
}

CurvedLayersResult curved_layers_for_strength(const ModelAnalysisInput &input, const DynamicPrintConfig &print_config,
                                              std::function<bool()> cancel, std::function<void(int)> progress)
{
    CurvedLayersResult out;
    auto stage = [&progress](int from, int to) -> std::function<void(int)> {
        if (! progress)
            return nullptr;
        return [progress, from, to](int p) { progress(from + (to - from) * p / 100); };
    };
    // 1) Flat layers.
    Setup flat_setup = input.setup;
    flat_setup.layer_normal = nullptr;
    out.flat = analyze(input.mesh, flat_setup, cancel, stage(0, 45));
    if (! out.flat.ok) {
        out.error = out.flat.error;
        return out;
    }
    // 2) The layers, within the print head and above the flat first layers of the print settings.
    PrintConfig config;
    config.apply(print_config, true);
    const NonPlanar::HeadLimits head = NonPlanar::head_limits(config);
    StressLayersParams params;
    if (head.tan_x > 0. && head.tan_y > 0.) {
        params.tan_x = head.tan_x;
        params.tan_y = head.tan_y;
    }
    params.z_flat = config.nonplanar_flat_below.value;
    StressLayersResult layers = stress_layers(input.mesh, out.flat, params);
    if (! layers.ok) {
        out.error = layers.error;
        return out;
    }
    out.field = std::move(layers.field);
    if (cancel && cancel()) {
        out.error = "Cancelled";
        return out;
    }
    // 3) The layers as they will be sliced (transitions, limits of the layer thickness and of the head).
    BoundingBoxf3 bbox;
    for (const stl_vertex &v : input.mesh.vertices)
        bbox.merge(v.cast<double>());
    const double z0 = bbox.min.z();
    BoundingBoxf3 part_bbox = bbox;
    part_bbox.min.z() -= z0;
    part_bbox.max.z() -= z0;
    indexed_triangle_set part_mesh = input.mesh;
    for (stl_vertex &v : part_mesh.vertices)
        v.z() -= float(z0);
    config.nonplanar_mode.value = NonPlanarMode::Stress;
    auto deformation = std::make_shared<NonPlanar::Deformation>(NonPlanar::make_deformation(config, part_bbox, &part_mesh, &out.field));
    Setup curved_setup = input.setup;
    curved_setup.layer_normal = [deformation, z0](const Vec3d &p) { return deformation->layer_normal(p.x(), p.y(), p.z() - z0); };
    out.curved = analyze(input.mesh, curved_setup, cancel, stage(55, 100));
    if (! out.curved.ok) {
        out.error = out.curved.error;
        return out;
    }
    out.field.safety_flat   = out.flat.safety_factor;
    out.field.safety_curved = out.curved.safety_factor;
    out.ok = true;
    return out;
}

std::function<Vec3d(const Vec3d &point)> layer_field_normals(const EngineeringLayerField &field, const Vec3d &part_min)
{
    auto sampler = std::make_shared<NonPlanar::StressField>(field, part_min);
    return [sampler](const Vec3d &point) {
        const Vec2d  g  = sampler->grad(point.x(), point.y(), point.z());
        const double dz = sampler->dz(point.x(), point.y(), point.z());
        // The layer is the level set of z - offset(x, y, z).
        return Vec3d(- g.x(), - g.y(), 1. - dz).normalized();
    };
}

} // namespace Fea
} // namespace Slic3r
