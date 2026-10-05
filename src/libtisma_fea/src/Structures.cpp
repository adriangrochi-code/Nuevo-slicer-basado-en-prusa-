///|/ Tisma Slicer: local reinforcements and 3D lattice for the loads (phase 6).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Method (see docs/FEA.md):
// - Reinforcement: the cells of a grid closer than a radius to the fixed faces and to the loads form a zone with more
//   perimeters (thicker walls in the analysis) and more infill. The lightest uniform infill of the rest of the part
//   is then searched with the zone in place, and compared with the lightest infill without it.
// - Lattice: the infill of the part is set to 0 % and replaced by struts on a cubic grid (vertical struts and struts
//   at 45 degrees in the XZ and YZ planes: printable without supports). Each cell is a homogenized material with
//   the relative density of its struts, rho = LATTICE_DENSITY_FACTOR * (r / cell)^2 (strut length per cell
//   (1 + 4 sqrt(2)) * cell, overlaps at the nodes ignored), and Gibson-Ashby exponents LATTICE_EXPONENT, as the
//   infill. The voxels are too coarse to resolve the struts themselves. The thinnest uniform diameter is found by
//   bisection; then every cell gets a diameter following its stresses and all the diameters are scaled together
//   until the requirements are met.
#include "tisma_fea/Structures.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include "CellsMesh.hpp"

namespace Slic3r {
namespace Fea {

// ---------------------------------------------------------------------------------------------------------------------
// Reinforcements.

InfillZone reinforcement_zone(const indexed_triangle_set &mesh, const Setup &setup, const ReinforcementOptions &options)
{
    InfillZone zone;
    zone.density        = std::clamp(options.density, 0., 1.);
    zone.wall_thickness = setup.infill.wall_thickness + std::max(0, options.extra_perimeters) * setup.infill.perimeter_width;

    // Geometry of the supports and of the loads.
    indexed_triangle_set sub;
    std::vector<Vec3d>   points;
    double load_radius = 0.;
    auto add_triangles = [&](const std::vector<int> &triangles) {
        for (int t : triangles)
            if (t >= 0 && t < int(mesh.indices.size())) {
                const int base = int(sub.vertices.size());
                for (int c = 0; c < 3; ++ c)
                    sub.vertices.emplace_back(mesh.vertices[mesh.indices[t][c]]);
                sub.indices.emplace_back(base, base + 1, base + 2);
            }
    };
    for (const Fixture &f : setup.fixtures)
        add_triangles(f.triangles);
    for (const Load &l : setup.loads) {
        if (l.type == Load::Type::Faces)
            add_triangles(l.triangles);
        else {
            points.push_back(l.point);
            load_radius = std::max(load_radius, l.radius);
        }
    }
    if (sub.indices.empty() && points.empty())
        return zone;
    const double radius = options.radius > 0. ? options.radius : std::max(5., 2. * load_radius);
    const double c = std::max(0.5, options.cell);

    BoundingBoxf3 bbox;
    for (const Vec3f &v : sub.vertices)
        bbox.merge(v.cast<double>());
    for (const Vec3d &p : points)
        bbox.merge(p);
    const Vec3d origin = bbox.min - Vec3d(radius, radius, radius);
    const Vec3d extent = bbox.size() + 2. * Vec3d(radius, radius, radius);
    const Vec3i n(int(std::ceil(extent.x() / c)), int(std::ceil(extent.y() / c)), int(std::ceil(extent.z() / c)));

    const auto tree = sub.indices.empty() ? AABBTreeIndirect::Tree<3, float>() :
                                            AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(sub.vertices, sub.indices);
    std::vector<char> selected(size_t(n.x()) * n.y() * n.z(), 0);
    for (int k = 0; k < n.z(); ++ k)
        for (int j = 0; j < n.y(); ++ j)
            for (int i = 0; i < n.x(); ++ i) {
                const Vec3d center = origin + c * Vec3d(i + 0.5, j + 0.5, k + 0.5);
                double d2 = std::numeric_limits<double>::max();
                if (! sub.indices.empty()) {
                    size_t hit_idx;
                    Vec3f  hit_point;
                    const Vec3f pt = center.cast<float>();
                    const float dd = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(sub.vertices, sub.indices, tree, pt, hit_idx, hit_point);
                    if (dd >= 0.f)
                        d2 = dd;
                }
                for (const Vec3d &p : points)
                    d2 = std::min(d2, (center - p).squaredNorm());
                selected[i + size_t(n.x()) * (j + size_t(n.y()) * k)] = d2 <= radius * radius;
            }
    zone.mesh = cells_mesh(selected, n, origin, c);
    return zone;
}

ReinforcementResult optimize_reinforcement(const indexed_triangle_set &mesh, const Setup &setup, const ReinforcementOptions &options,
                                           std::function<bool()> cancel, std::function<void(int)> progress)
{
    ReinforcementResult out;
    out.zone = reinforcement_zone(mesh, setup, options);
    if (out.zone.mesh.indices.empty()) {
        out.error = "The part has no supports or loads to reinforce";
        return out;
    }
    OptimizeOptions opt;
    opt.zones = false;
    auto part = [&](int from, int to) {
        return [&progress, from, to](int p) { if (progress) progress(from + (to - from) * p / 100); };
    };
    out.without = optimize_infill(mesh, setup, opt, cancel, part(0, 50));
    if (! out.without.ok) {
        out.error = out.without.error;
        return out;
    }
    opt.fixed_zones = { out.zone };
    out.with = optimize_infill(mesh, setup, opt, cancel, part(50, 100));
    if (! out.with.ok) {
        out.error = out.with.error;
        return out;
    }
    out.ok = true;
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lattice.

// Relative density of a cell: pi * r^2 * (1 + 4 sqrt(2)) * a / a^3.
static constexpr double LATTICE_DENSITY_FACTOR = M_PI * (1. + 4. * M_SQRT2);

LatticeGrid LatticeGrid::over(const BoundingBoxf3 &bbox, double cell)
{
    LatticeGrid g;
    g.cell   = std::max(1., cell);
    g.origin = bbox.min;
    const Vec3d size = bbox.size();
    g.cells  = Vec3i(std::max(1, int(std::ceil(size.x() / g.cell))), std::max(1, int(std::ceil(size.y() / g.cell))),
                     std::max(1, int(std::ceil(size.z() / g.cell))));
    return g;
}

// Struts of the lattice as pairs of node coordinates (i, j, k).
static void for_each_strut(const LatticeGrid &g, const std::function<void(const Vec3i &, const Vec3i &)> &fn)
{
    const Vec3i n = g.cells;
    for (int k = 0; k < n.z(); ++ k) {
        // Vertical struts.
        for (int j = 0; j <= n.y(); ++ j)
            for (int i = 0; i <= n.x(); ++ i)
                fn(Vec3i(i, j, k), Vec3i(i, j, k + 1));
        // Crosses at 45 degrees in the XZ planes.
        for (int j = 0; j <= n.y(); ++ j)
            for (int i = 0; i < n.x(); ++ i) {
                fn(Vec3i(i, j, k), Vec3i(i + 1, j, k + 1));
                fn(Vec3i(i + 1, j, k), Vec3i(i, j, k + 1));
            }
        // Crosses at 45 degrees in the YZ planes.
        for (int i = 0; i <= n.x(); ++ i)
            for (int j = 0; j < n.y(); ++ j) {
                fn(Vec3i(i, j, k), Vec3i(i, j + 1, k + 1));
                fn(Vec3i(i, j + 1, k), Vec3i(i, j, k + 1));
            }
    }
}

// Closed prism of a strut, extended by half its radius at both ends so that the struts overlap at the nodes.
static void add_strut(indexed_triangle_set &its, Vec3d p0, Vec3d p1, double r, int segments)
{
    Vec3d u = p1 - p0;
    const double len = u.norm();
    if (len <= 0. || r <= 0.)
        return;
    u /= len;
    p0 -= 0.5 * r * u;
    p1 += 0.5 * r * u;
    const Vec3d a = std::abs(u.z()) < 0.9 ? Vec3d::UnitZ() : Vec3d::UnitX();
    const Vec3d v = u.cross(a).normalized();
    const Vec3d w = u.cross(v);
    const int base = int(its.vertices.size());
    for (int end = 0; end < 2; ++ end)
        for (int s = 0; s < segments; ++ s) {
            const double t = 2. * M_PI * s / segments;
            its.vertices.emplace_back(((end ? p1 : p0) + r * (std::cos(t) * v + std::sin(t) * w)).cast<float>());
        }
    const int c0 = int(its.vertices.size());
    its.vertices.emplace_back(p0.cast<float>());
    its.vertices.emplace_back(p1.cast<float>());
    for (int s = 0; s < segments; ++ s) {
        const int a0 = base + s, a1 = base + (s + 1) % segments;
        const int b0 = a0 + segments, b1 = a1 + segments;
        its.indices.emplace_back(a0, a1, b1);
        its.indices.emplace_back(a0, b1, b0);
        its.indices.emplace_back(c0, a1, a0);
        its.indices.emplace_back(c0 + 1, b0, b1);
    }
}

indexed_triangle_set lattice_mesh(const LatticeGrid &grid, const LatticeOptions &options,
                                  const std::function<double(const Vec3d &midpoint)> &diameter_at)
{
    indexed_triangle_set its;
    const int segments = std::clamp(options.segments, 3, 16);
    auto node = [&](const Vec3i &n) { return Vec3d(grid.origin + grid.cell * n.cast<double>()); };
    for_each_strut(grid, [&](const Vec3i &a, const Vec3i &b) {
        const Vec3d p0 = node(a), p1 = node(b);
        const double d = diameter_at(0.5 * (p0 + p1));
        if (d > 0.)
            add_strut(its, p0, p1, 0.5 * std::clamp(d, options.min_diameter, options.max_diameter), segments);
    });
    return its;
}

LatticeResult optimize_lattice(const indexed_triangle_set &mesh, const Setup &setup_in, const LatticeOptions &options,
                               std::function<bool()> cancel, std::function<void(int)> progress)
{
    LatticeResult out;
    Setup setup = setup_in;
    setup.infill.enabled = true;
    setup.infill.density = 0.;
    setup.infill.density_field = nullptr;
    setup.infill.zones.clear();
    setup.infill.stiffness_exponent = LATTICE_EXPONENT;
    setup.infill.strength_exponent  = LATTICE_EXPONENT;

    BoundingBoxf3 bbox;
    for (const Vec3f &v : mesh.vertices)
        bbox.merge(v.cast<double>());
    out.grid = LatticeGrid::over(bbox, options.cell);

    // Voxels of the part, to keep only the struts which touch it.
    Result shell = analyze(mesh, setup, cancel);
    ++ out.analyses;
    if (! shell.ok) {
        out.error = shell.error;
        return out;
    }
    std::vector<char> solid(size_t(shell.size.x()) * shell.size.y() * shell.size.z(), 0);
    for (int v : shell.voxels)
        solid[v] = 1;
    auto inside = [&](const Vec3d &p) {
        const Vec3d q = (p - shell.origin) / shell.h;
        const int i = int(std::floor(q.x())), j = int(std::floor(q.y())), k = int(std::floor(q.z()));
        if (i < 0 || j < 0 || k < 0 || i >= shell.size.x() || j >= shell.size.y() || k >= shell.size.z())
            return false;
        return solid[i + size_t(shell.size.x()) * (j + size_t(shell.size.y()) * k)] != 0;
    };
    auto strut_kept = [&](const Vec3d &mid) {
        // The node grid is regular: the ends are mid +- half a diagonal at most; testing the midpoint and points
        // around it keeps the struts which reach the walls.
        if (inside(mid))
            return true;
        const double r = 0.5 * out.grid.cell;
        for (const Vec3d &d : { Vec3d(r, 0, r), Vec3d(-r, 0, -r), Vec3d(r, 0, -r), Vec3d(-r, 0, r), Vec3d(0, r, r), Vec3d(0, -r, -r),
                                Vec3d(0, r, -r), Vec3d(0, -r, r), Vec3d(0, 0, r), Vec3d(0, 0, -r) })
            if (inside(mid + d))
                return true;
        return false;
    };

    const int expected = 16;
    const double a = out.grid.cell;
    auto run = [&](const std::function<double(const Vec3d &)> &diameter, indexed_triangle_set *mesh_out) {
        Setup s = setup;
        s.infill.density_field = [&](const Vec3d &p) {
            const double r = 0.5 * std::clamp(diameter(p), options.min_diameter, options.max_diameter);
            return std::clamp(LATTICE_DENSITY_FACTOR * (r / a) * (r / a), 0., 1.);
        };
        Result r = analyze(mesh, s, cancel, [&](int p) {
            if (progress)
                progress(std::min(99, (100 * out.analyses + p) / expected));
        });
        ++ out.analyses;
        if (mesh_out)
            *mesh_out = lattice_mesh(out.grid, options, [&](const Vec3d &mid) { return strut_kept(mid) ? diameter(mid) : 0.; });
        return r;
    };

    // 1) Uniform struts.
    const double dmin = std::max(0.1, options.min_diameter), dmax = std::max(dmin, options.max_diameter);
    Result hi = run([&](const Vec3d &) { return dmax; }, nullptr);
    if (! hi.ok) {
        out.error = hi.error;
        return out;
    }
    if (! meets_requirements(hi)) {
        out.ok = true;
        out.uniform_diameter = dmax;
        out.uniform = std::move(hi);
        return out;
    }
    out.feasible = true;
    double d_hi = dmax;
    Result lo = run([&](const Vec3d &) { return dmin; }, nullptr);
    if (lo.ok && meets_requirements(lo)) {
        d_hi = dmin;
        hi = std::move(lo);
    } else {
        double d_lo = dmin;
        while (d_hi - d_lo > 0.05 && ! (cancel && cancel())) {
            const double mid = 0.5 * (d_lo + d_hi);
            Result r = run([&](const Vec3d &) { return mid; }, nullptr);
            if (r.ok && meets_requirements(r)) {
                d_hi = mid;
                hi = std::move(r);
            } else
                d_lo = mid;
        }
    }
    out.uniform_diameter = d_hi;
    out.uniform = hi;
    out.mesh = lattice_mesh(out.grid, options, [&](const Vec3d &mid) { return strut_kept(mid) ? d_hi : 0.; });
    out.min_used_diameter = out.max_used_diameter = d_hi;
    if (d_hi <= dmin + 1e-9) {
        // The thinnest struts are enough: nothing to vary.
        out.ok = true;
        if (progress)
            progress(100);
        return out;
    }

    // 2) Variable struts: the diameter of every cell from the stresses of the uniform lattice. The strength and the
    //    stiffness of a strut grow with its section (diameter squared).
    const Vec3i nc = out.grid.cells;
    std::vector<double> util(size_t(nc.x()) * nc.y() * nc.z(), 0.);
    for (size_t e = 0; e < hi.voxels.size(); ++ e) {
        if (! hi.interior[e])
            continue;
        const int idx = hi.voxels[e];
        const Vec3d p = hi.origin + hi.h * Vec3d(idx % hi.size.x() + 0.5, (idx / hi.size.x()) % hi.size.y() + 0.5, idx / (hi.size.x() * hi.size.y()) + 0.5);
        const Vec3d q = (p - out.grid.origin) / out.grid.cell;
        const int i = std::clamp(int(q.x()), 0, nc.x() - 1), j = std::clamp(int(q.y()), 0, nc.y() - 1), k = std::clamp(int(q.z()), 0, nc.z() - 1);
        double &u = util[i + size_t(nc.x()) * (j + size_t(nc.y()) * k)];
        u = std::max(u, double(hi.failure_index[e]) * setup.required_safety_factor);
    }
    std::vector<double> d0(util.size());
    for (size_t c = 0; c < util.size(); ++ c)
        d0[c] = d_hi * std::sqrt(std::max(util[c], 1e-6));
    auto cell_of = [&](const Vec3d &mid) {
        const Vec3d q = (mid - out.grid.origin) / out.grid.cell;
        const int i = std::clamp(int(std::floor(q.x())), 0, nc.x() - 1), j = std::clamp(int(std::floor(q.y())), 0, nc.y() - 1),
                  k = std::clamp(int(std::floor(q.z())), 0, nc.z() - 1);
        return size_t(i + size_t(nc.x()) * (j + size_t(nc.y()) * k));
    };
    auto diameters = [&](double k) {
        std::vector<float> d(d0.size());
        for (size_t c = 0; c < d0.size(); ++ c)
            d[c] = float(std::clamp(k * d0[c], dmin, dmax));
        return d;
    };
    auto evaluate = [&](double k, Result &r, indexed_triangle_set &lattice) {
        const std::vector<float> d = diameters(k);
        r = run([&](const Vec3d &mid) { return double(d[cell_of(mid)]); }, &lattice);
        return r.ok && meets_requirements(r);
    };
    double k_hi = 1.;
    Result r_hi;
    indexed_triangle_set m_hi;
    bool found = evaluate(k_hi, r_hi, m_hi);
    for (int i = 0; i < 4 && ! found && ! (cancel && cancel()); ++ i) {
        k_hi *= 1.4;
        found = evaluate(k_hi, r_hi, m_hi);
    }
    if (found) {
        double k_lo = 0.;
        for (int it = 0; it < 10 && k_hi - k_lo > 0.05 * k_hi && ! (cancel && cancel()); ++ it) {
            const double k = 0.5 * (k_lo + k_hi);
            Result r;
            indexed_triangle_set m;
            if (evaluate(k, r, m)) {
                k_hi = k;
                r_hi = std::move(r);
                m_hi = std::move(m);
            } else
                k_lo = k;
        }
        if (r_hi.mass < out.uniform.mass * 0.99) {
            out.variable_found = true;
            out.cell_diameter  = diameters(k_hi);
            out.variable       = std::move(r_hi);
            out.mesh           = std::move(m_hi);
            out.min_used_diameter = *std::min_element(out.cell_diameter.begin(), out.cell_diameter.end());
            out.max_used_diameter = *std::max_element(out.cell_diameter.begin(), out.cell_diameter.end());
        }
    }
    if (cancel && cancel()) {
        out.error = "Cancelled";
        return out;
    }
    out.ok = true;
    if (progress)
        progress(100);
    return out;
}

} // namespace Fea
} // namespace Slic3r
