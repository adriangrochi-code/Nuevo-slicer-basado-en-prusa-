///|/ Tisma Slicer: approximate aerodynamic analysis of a printed part (see Aero.hpp).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/Aero.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

#include <Eigen/Geometry>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>

namespace Slic3r {
namespace Fea {

double printed_roughness_ra(double layer_height, const Vec3d &normal)
{
    if (layer_height <= 0.)
        return 0.;
    const double nz   = std::min(1., std::abs(normal.normalized().z()));
    const double bead = 0.05 * layer_height;
    // Nearly flat faces are a single layer (no steps); vertical walls have no steps either (nz = 0).
    const double steps = nz < 0.995 ? 0.25 * layer_height * nz : 0.;
    return std::max(bead, steps);
}

double local_skin_friction(double x, double ks, double speed, double nu)
{
    const double x_m    = std::max(x, 1e-3) * 1e-3;
    const double re_x   = std::max(speed * x_m / nu, 1.);
    const double re_k   = speed * ks * 1e-3 / nu;
    const bool   turbulent = re_x > 5e5 || re_k > 300.;
    if (!turbulent)
        return 0.664 / std::sqrt(re_x);
    const double smooth = 0.0592 / std::pow(re_x, 0.2);
    if (ks <= 0.)
        return smooth;
    // Fully rough plate (Schlichting), valid while x >> ks.
    const double rough = std::pow(2.87 + 1.58 * std::log10(std::max(x / ks, 10.)), -2.5);
    return std::max(smooth, rough);
}

namespace {

// D3Q19 lattice.
constexpr int Q = 19;
constexpr std::array<int, Q> CX { 0, 1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0 };
constexpr std::array<int, Q> CY { 0, 0, 0, 1, -1, 0, 0, 1, -1, -1, 1, 0, 0, 0, 0, 1, -1, 1, -1 };
constexpr std::array<int, Q> CZ { 0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, -1, -1, 1, 1, -1, -1, 1 };
constexpr std::array<int, Q> OPP { 0, 2, 1, 4, 3, 6, 5, 8, 7, 10, 9, 12, 11, 14, 13, 16, 15, 18, 17 };
constexpr std::array<float, Q> W { 1.f / 3.f,
    1.f / 18.f, 1.f / 18.f, 1.f / 18.f, 1.f / 18.f, 1.f / 18.f, 1.f / 18.f,
    1.f / 36.f, 1.f / 36.f, 1.f / 36.f, 1.f / 36.f, 1.f / 36.f, 1.f / 36.f,
    1.f / 36.f, 1.f / 36.f, 1.f / 36.f, 1.f / 36.f, 1.f / 36.f, 1.f / 36.f };

inline float feq(int q, float rho, float ux, float uy, float uz, float usq)
{
    const float cu = 3.f * (CX[q] * ux + CY[q] * uy + CZ[q] * uz);
    return W[q] * rho * (1.f + cu + 0.5f * cu * cu - 1.5f * usq);
}

// Face of a fluid cell touching the part: fluid cell, outward normal of the part (axis 0..5 as the first six
// directions of the lattice) and the solid voxel.
struct BoundaryFace
{
    size_t fluid;
    int    dir;
    size_t solid;
};

} // namespace

AeroResult analyze_aero(const indexed_triangle_set &mesh_in, const AeroSetup &setup)
{
    AeroResult res;
    if (mesh_in.indices.empty() || setup.flow_direction.norm() < 1e-9 || setup.speed <= 0.) {
        res.error = "Empty part or no air speed";
        return res;
    }
    auto cancelled = [&setup]() { return setup.cancel && setup.cancel(); };

    // Rotate the part so that the air moves along +X.
    const Eigen::Quaterniond rot = Eigen::Quaterniond::FromTwoVectors(setup.flow_direction.normalized(), Vec3d::UnitX());
    const Eigen::Matrix3d    R   = rot.toRotationMatrix();
    indexed_triangle_set mesh = mesh_in;
    for (Vec3f &v : mesh.vertices)
        v = (R * v.cast<double>()).cast<float>();

    BoundingBoxf3 bbox;
    for (const Vec3f &v : mesh.vertices)
        bbox.merge(v.cast<double>());
    const Vec3d extent = bbox.size();
    res.length       = extent.x();
    res.frontal_size = std::max(extent.y(), extent.z());
    if (res.frontal_size <= 0. || extent.x() <= 0.) {
        res.error = "The part is flat";
        return res;
    }

    // Lattice: 1.5 frontal sizes upstream, 3 downstream, lateral margin on the sides.
    const double D = res.frontal_size;
    double dx = D / double(std::max(setup.resolution, 4));
    const double up = 1.5 * D, down = 3. * D, side = std::max(setup.lateral_margin, 0.5) * D;
    int nx = 0, ny = 0, nz = 0;
    for (;;) {
        nx = int(std::ceil((extent.x() + up + down) / dx));
        ny = int(std::ceil((extent.y() + 2. * side) / dx));
        nz = int(std::ceil((extent.z() + 2. * side) / dx));
        if (double(nx) * ny * nz <= 8e6)
            break;
        dx *= 1.15;
    }
    const size_t N = size_t(nx) * ny * nz;
    const Vec3d origin(bbox.min.x() - up, bbox.min.y() - 0.5 * (ny * dx - extent.y()), bbox.min.z() - 0.5 * (nz * dx - extent.z()));
    auto idx = [nx, ny](int i, int j, int k) { return size_t(i) + size_t(nx) * (size_t(j) + size_t(ny) * size_t(k)); };

    // Voxelize: a cell is solid when its center is inside the part.
    std::vector<uint8_t> solid(N, 0);
    {
        std::vector<float> zs(nz);
        for (int k = 0; k < nz; ++k)
            zs[k] = float(origin.z() + (k + 0.5) * dx);
        const std::vector<ExPolygons> slices = slice_mesh_ex(mesh, zs);
        for (int k = 0; k < nz; ++k)
            for (const ExPolygon &expoly : slices[k]) {
                const BoundingBox bb = get_extents(expoly);
                const int i0 = std::max(0, int(std::floor((unscaled(bb.min.x()) - origin.x()) / dx - 0.5)));
                const int i1 = std::min(nx - 1, int(std::ceil((unscaled(bb.max.x()) - origin.x()) / dx - 0.5)));
                const int j0 = std::max(0, int(std::floor((unscaled(bb.min.y()) - origin.y()) / dx - 0.5)));
                const int j1 = std::min(ny - 1, int(std::ceil((unscaled(bb.max.y()) - origin.y()) / dx - 0.5)));
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i)
                        if (expoly.contains(Point::new_scale(origin.x() + (i + 0.5) * dx, origin.y() + (j + 0.5) * dx)))
                            solid[idx(i, j, k)] = 1;
            }
    }
    size_t frontal_cells = 0;
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                if (solid[idx(i, j, k)]) {
                    ++frontal_cells;
                    break;
                }
    if (frontal_cells == 0) {
        res.error = "The part is too thin for the resolution of the analysis";
        return res;
    }
    res.frontal_area = double(frontal_cells) * dx * dx;

    // Faces of the part seen from the fluid.
    std::vector<BoundaryFace> faces;
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const size_t c = idx(i, j, k);
                if (!solid[c])
                    continue;
                for (int d = 1; d <= 6; ++d) {
                    const int ii = i + CX[d], jj = j + CY[d], kk = k + CZ[d];
                    if (ii < 0 || jj < 0 || kk < 0 || ii >= nx || jj >= ny || kk >= nz)
                        continue;
                    const size_t f = idx(ii, jj, kk);
                    if (!solid[f])
                        faces.push_back({ f, d, c });
                }
            }

    // Reynolds numbers and lattice parameters.
    const double nu   = setup.air_viscosity / setup.air_density;
    res.reynolds      = setup.speed * D * 1e-3 / nu;
    // Lattice speed: low enough to stay nearly incompressible (Mach 0.17), high enough for few steps.
    const float  U    = 0.1f;
    const double D_lb = D / dx;
    const double re_cap = setup.max_sim_reynolds > 0. ? setup.max_sim_reynolds : 40. * D_lb;
    res.sim_reynolds  = std::min(res.reynolds, re_cap);
    const double nu_lb = U * D_lb / res.sim_reynolds;
    const float  tau0  = float(3. * nu_lb + 0.5);
    const float  cs2_smag = 0.17f * 0.17f;

    std::vector<float> f(setup.simulate_pressure ? N * Q : 0), g(setup.simulate_pressure ? N * Q : 0);
    if (setup.simulate_pressure) {
        const float usq = U * U;
        for (size_t c = 0; c < N; ++c)
            for (int q = 0; q < Q; ++q)
                f[q * N + c] = feq(q, 1.f, U, 0.f, 0.f, usq);
    }
    std::vector<float> rho_cell(N, 1.f);

    const int steps        = setup.simulate_pressure ? std::max(200, int(setup.flow_throughs * nx / U)) : 0;
    const int ramp         = std::max(1, steps / 10);
    const int average_from = steps - steps / 3;
    std::vector<double> face_rho_sum(faces.size(), 0.);
    int averaged = 0;

    // Linear offset of the neighbor in each direction of the lattice.
    std::array<std::ptrdiff_t, Q> offsets_signed;
    for (int q = 0; q < Q; ++q)
        offsets_signed[q] = CX[q] + std::ptrdiff_t(nx) * (CY[q] + std::ptrdiff_t(ny) * CZ[q]);
    std::array<size_t, Q> offsets;
    for (int q = 0; q < Q; ++q)
        offsets[q] = size_t(offsets_signed[q]);   // c - offsets[q] wraps correctly with unsigned arithmetic

    for (int step = 0; step < steps; ++step) {
        if (cancelled()) {
            res.error = "Cancelled";
            return res;
        }
        const float u_in = U * std::min(1.f, float(step + 1) / float(ramp));
        tbb::parallel_for(tbb::blocked_range<int>(0, nz), [&](const tbb::blocked_range<int> &range) {
            std::array<float, Q> fin;
            for (int k = range.begin(); k < range.end(); ++k)
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i) {
                        const size_t c = idx(i, j, k);
                        if (solid[c])
                            continue;
                        // Pull streaming. Interior cells (most of them): fixed offsets to the neighbors.
                        if (i > 0 && j > 0 && k > 0 && i < nx - 1 && j < ny - 1 && k < nz - 1) {
                            for (int q = 0; q < Q; ++q) {
                                const size_t s = c - offsets[q];
                                fin[q] = solid[s] ? f[OPP[q] * N + c] : f[q * N + s];   // halfway bounce-back
                            }
                        } else
                        for (int q = 0; q < Q; ++q) {
                            int si = i - CX[q];
                            int sj = j - CY[q];
                            int sk = k - CZ[q];
                            // Periodic sides.
                            if (sj < 0) sj += ny; else if (sj >= ny) sj -= ny;
                            if (sk < 0) sk += nz; else if (sk >= nz) sk -= nz;
                            if (si < 0) {
                                // Inlet: equilibrium at the free stream velocity.
                                fin[q] = feq(q, 1.f, u_in, 0.f, 0.f, u_in * u_in);
                                continue;
                            }
                            if (si >= nx)
                                si = nx - 1;   // outlet: zero gradient
                            const size_t s = idx(si, sj, sk);
                            fin[q] = solid[s] ? f[OPP[q] * N + c] : f[q * N + s];   // halfway bounce-back
                        }
                        float rho = 0.f, ux = 0.f, uy = 0.f, uz = 0.f;
                        for (int q = 0; q < Q; ++q) {
                            rho += fin[q];
                            ux += CX[q] * fin[q];
                            uy += CY[q] * fin[q];
                            uz += CZ[q] * fin[q];
                        }
                        ux /= rho; uy /= rho; uz /= rho;
                        const float usq = ux * ux + uy * uy + uz * uz;
                        std::array<float, Q> eq;
                        float pxx = 0, pyy = 0, pzz = 0, pxy = 0, pxz = 0, pyz = 0;
                        for (int q = 0; q < Q; ++q) {
                            eq[q] = feq(q, rho, ux, uy, uz, usq);
                            const float ne = fin[q] - eq[q];
                            pxx += CX[q] * CX[q] * ne; pyy += CY[q] * CY[q] * ne; pzz += CZ[q] * CZ[q] * ne;
                            pxy += CX[q] * CY[q] * ne; pxz += CX[q] * CZ[q] * ne; pyz += CY[q] * CZ[q] * ne;
                        }
                        // Smagorinsky: effective relaxation time from the non equilibrium stress.
                        const float qn  = std::sqrt(pxx * pxx + pyy * pyy + pzz * pzz + 2.f * (pxy * pxy + pxz * pxz + pyz * pyz));
                        const float tau = 0.5f * (tau0 + std::sqrt(tau0 * tau0 + 18.f * 1.41421356f * cs2_smag * qn / rho));
                        const float omega = 1.f / tau;
                        for (int q = 0; q < Q; ++q)
                            g[q * N + c] = fin[q] - omega * (fin[q] - eq[q]);
                        rho_cell[c] = rho;
                    }
        });
        std::swap(f, g);
        if (step >= average_from) {
            for (size_t b = 0; b < faces.size(); ++b)
                face_rho_sum[b] += rho_cell[faces[b].fluid];
            ++averaged;
        }
        if (setup.progress && (step % std::max(1, steps / 100)) == 0)
            setup.progress(int(100. * step / steps));
        if (step == steps / 2) {
            // Diverged (unstable at this resolution and Reynolds number)?
            float max_rho = 0.f;
            for (size_t c = 0; c < N; c += 97)
                if (!solid[c])
                    max_rho = std::max(max_rho, std::isfinite(rho_cell[c]) ? rho_cell[c] : 1e9f);
            if (max_rho > 2.f) {
                res.error = "The simulation diverged: lower the speed limit of the simulation or raise the resolution";
                return res;
            }
        }
    }
    res.iterations = steps;
    res.converged  = setup.simulate_pressure;

    // Pressure force on the part (lattice units), from the time averaged density on its faces.
    const double q_lb = 0.5 * U * U;
    Vec3d force_lb = Vec3d::Zero();
    std::vector<double> voxel_cp(N, 0.);
    std::vector<int>    voxel_cp_count(N, 0);
    for (size_t b = 0; b < faces.size() && averaged > 0; ++b) {
        const double rho_avg = face_rho_sum[b] / std::max(averaged, 1);
        const double p       = (rho_avg - 1.) / 3.;
        const Vec3d  n(CX[faces[b].dir], CY[faces[b].dir], CZ[faces[b].dir]);
        force_lb -= p * n;
        voxel_cp[faces[b].solid] += p / q_lb;
        ++voxel_cp_count[faces[b].solid];
    }
    const double area_lb = double(frontal_cells);
    res.cd_pressure = force_lb.x() / (q_lb * area_lb);
    res.cl          = std::hypot(force_lb.y(), force_lb.z()) / (q_lb * area_lb);

    // Skin friction with the roughness of the printed faces (in the coordinates of the input mesh for the normals).
    const double q_phys = 0.5 * setup.air_density * setup.speed * setup.speed;
    double friction = 0., friction_smooth = 0., ra_area = 0.;
    res.triangle_ra.assign(mesh.indices.size(), 0.f);
    for (size_t t = 0; t < mesh.indices.size(); ++t) {
        const Vec3d n_in = its_face_normal(mesh_in, int(t)).cast<double>();
        const Vec3f &a = mesh.vertices[mesh.indices[t][0]], &b = mesh.vertices[mesh.indices[t][1]], &c = mesh.vertices[mesh.indices[t][2]];
        const Vec3d cross = (b - a).cross(c - a).cast<double>();
        const double area = 0.5 * cross.norm();
        if (area <= 0.)
            continue;
        const Vec3d  n  = cross.normalized();
        const double ra = printed_roughness_ra(setup.layer_height, n_in);
        res.triangle_ra[t] = float(ra * 1000.);
        res.wetted_area += area;
        ra_area += ra * area;
        // Faces looking downstream are in the separated wake: no friction there.
        if (n.x() > 0.5)
            continue;
        const double along = std::sqrt(std::max(0., 1. - n.x() * n.x()));
        if (along <= 0.)
            continue;
        // The local friction changes along the flow: integrate over sub-triangles of about 2 mm.
        const double max_edge = std::max({ (b - a).norm(), (c - b).norm(), (a - c).norm() });
        const int    m        = std::clamp(int(std::ceil(max_edge / 2.)), 1, 32);
        const double sub_m2   = area * 1e-6 / double(m * m);
        const double ks       = equivalent_sand_roughness(ra);
        auto add = [&](double u, double v) {
            const double x = (1. - u - v) * a.x() + u * b.x() + v * c.x() - bbox.min.x();
            friction        += q_phys * local_skin_friction(x, ks, setup.speed, nu) * sub_m2 * along;
            friction_smooth += q_phys * local_skin_friction(x, 0., setup.speed, nu) * sub_m2 * along;
        };
        for (int i = 0; i < m; ++i)
            for (int j = 0; j + i < m; ++j) {
                add((i + 1. / 3.) / m, (j + 1. / 3.) / m);
                if (i + j + 2 <= m)
                    add((i + 2. / 3.) / m, (j + 2. / 3.) / m);
            }
    }
    res.mean_roughness_ra = res.wetted_area > 0. ? 1000. * ra_area / res.wetted_area : 0.;
    const double a_front_m2 = res.frontal_area * 1e-6;
    res.cd_friction = friction / (q_phys * a_front_m2);
    res.roughness_friction_share = friction > 0. ? std::max(0., (friction - friction_smooth) / friction) : 0.;
    res.cd = res.cd_pressure + res.cd_friction;

    // Forces in N, back in the coordinates of the mesh.
    const double scale = q_phys * a_front_m2 / area_lb / q_lb;   // lattice pressure force -> N
    const Vec3d  force_rot(force_lb.x() * scale + friction, force_lb.y() * scale, force_lb.z() * scale);
    res.force = R.transpose() * force_rot;
    res.drag  = force_rot.x();

    // Pressure coefficient at the vertices: the nearest voxel of the part with faces in the fluid.
    res.vertex_cp.assign(mesh.vertices.size(), 0.f);
    for (size_t v = 0; v < mesh.vertices.size(); ++v) {
        const Vec3d p  = (mesh.vertices[v].cast<double>() - origin) / dx;
        const int   ci = int(std::floor(p.x())), cj = int(std::floor(p.y())), ck = int(std::floor(p.z()));
        double best = std::numeric_limits<double>::max();
        for (int r = 1; r <= 3 && best == std::numeric_limits<double>::max(); ++r)
            for (int k = ck - r; k <= ck + r; ++k)
                for (int j = cj - r; j <= cj + r; ++j)
                    for (int i = ci - r; i <= ci + r; ++i) {
                        if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz)
                            continue;
                        const size_t c = idx(i, j, k);
                        if (voxel_cp_count[c] == 0)
                            continue;
                        const double d2 = (Vec3d(i + 0.5, j + 0.5, k + 0.5) - p).squaredNorm();
                        if (d2 < best) {
                            best = d2;
                            res.vertex_cp[v] = float(voxel_cp[c] / voxel_cp_count[c]);
                        }
                    }
    }
    if (setup.progress)
        setup.progress(100);
    res.ok = true;
    return res;
}

} // namespace Fea
} // namespace Slic3r
