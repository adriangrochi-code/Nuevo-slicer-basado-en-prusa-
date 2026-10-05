///|/ Tisma Slicer: lightest infill which meets the safety factor and the deformation limits (phase 6).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Method (see docs/FEA.md):
// 1) Uniform infill: the requirements are monotonic with the density (more infill, stiffer and stronger), so the
//    lowest density is found by bisection.
// 2) Infill by zones: from the analysis of the uniform infill, every point of the interior gets the density which
//    would bring its stress to the allowed one (the strength of the infill scales with density^strength_exponent).
//    The interior is divided in cells; the cells are grouped in a few density levels, and the levels are scaled
//    together by bisection until the requirements are met. The zones are the meshes of the cells of each level,
//    as they will be applied (infill modifiers), so the last analysis is the one of what is printed.
#include "tisma_fea/Optimize.hpp"
#include "CellsMesh.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace Slic3r {
namespace Fea {

namespace {

double round_up(double v, double step) { return std::ceil(v / step - 1e-9) * step; }

} // namespace

OptimizeResult optimize_infill(const indexed_triangle_set &mesh, const Setup &setup_in, const OptimizeOptions &options,
                               std::function<bool()> cancel, std::function<void(int)> progress)
{
    OptimizeResult out;
    Setup setup = setup_in;
    setup.infill.enabled = true;
    setup.infill.zones.clear();
    setup.infill.density_field = nullptr;
    const double lo_limit = std::clamp(options.min_density, 0.01, 1.);
    const double hi_limit = std::clamp(options.max_density, lo_limit, 1.);
    const double step     = std::max(options.tolerance, 0.005);

    // About 2 + log2(range / step) analyses for the uniform infill and as many for the zones.
    const int expected = 2 * (2 + int(std::ceil(std::log2((hi_limit - lo_limit) / step)))) + 2;
    auto run = [&](Setup &s) {
        Result r = analyze(mesh, s, cancel, [&](int p) {
            if (progress)
                progress(std::min(99, (100 * out.analyses + p) / std::max(expected, 1)));
        });
        ++ out.analyses;
        return r;
    };
    // The fixed zones never have less infill than the part around them.
    auto fixed_zones = [&](double base) {
        std::vector<InfillZone> zones = options.fixed_zones;
        for (InfillZone &z : zones)
            z.density = std::max(z.density, base);
        return zones;
    };
    auto uniform = [&](double density) {
        setup.infill.density = density;
        setup.infill.zones   = fixed_zones(density);
        return run(setup);
    };

    // 1) Uniform infill.
    Result hi = uniform(hi_limit);
    if (! hi.ok) {
        out.error = hi.error;
        return out;
    }
    if (! meets_requirements(hi)) {
        // Not even the densest infill is enough: more walls, another material or another shape.
        out.ok = true;
        out.uniform_density = hi_limit;
        out.uniform = std::move(hi);
        return out;
    }
    out.feasible = true;
    double hi_density = hi_limit;
    Result lo = uniform(lo_limit);
    if (! lo.ok) {
        out.error = lo.error;
        return out;
    }
    if (meets_requirements(lo)) {
        hi_density = lo_limit;
        hi = std::move(lo);
    } else {
        double lo_density = lo_limit;
        while (hi_density - lo_density > step) {
            const double mid = 0.5 * (lo_density + hi_density);
            Result r = uniform(mid);
            if (! r.ok) {
                out.error = r.error;
                return out;
            }
            if (meets_requirements(r)) {
                hi_density = mid;
                hi = std::move(r);
            } else
                lo_density = mid;
        }
        // Whole percents, as the print settings: the percent below if it still meets the requirements, else the one above.
        const double below = std::floor(hi_density * 100. + 1e-9) / 100.;
        bool done = false;
        if (below > lo_density + 1e-9 && below < hi_density - 1e-9) {
            Result r = uniform(below);
            if (r.ok && meets_requirements(r)) {
                hi_density = below;
                hi = std::move(r);
                done = true;
            }
        }
        const double above = std::min(hi_limit, round_up(hi_density, 0.01));
        if (! done && above != hi_density) {
            Result r = uniform(above);
            if (r.ok && meets_requirements(r)) {
                hi_density = above;
                hi = std::move(r);
            }
        }
    }
    out.uniform_density = hi_density;
    out.uniform = std::move(hi);
    out.ok = true;
    if (! options.zones || hi_density <= lo_limit + 1e-9) {
        if (progress)
            progress(100);
        return out;
    }

    // 2) Infill by zones, from the stresses of the uniform infill.
    const Result &ru = out.uniform;
    const double  n_s = std::max(setup.infill.strength_exponent, 0.5);
    const double  h = ru.h;
    const int     cells_per_edge = std::max(1, int(std::ceil(std::max(3. * h, options.zone_cell > 0. ? options.zone_cell : 3.) / h)));
    const double  c = cells_per_edge * h;
    const Vec3i   nc((ru.size.x() + cells_per_edge - 1) / cells_per_edge, (ru.size.y() + cells_per_edge - 1) / cells_per_edge,
                     (ru.size.z() + cells_per_edge - 1) / cells_per_edge);
    std::vector<double> cell_density(size_t(nc.x()) * nc.y() * nc.z(), -1.);
    const double target_fi = 1. / setup.required_safety_factor;
    for (size_t e = 0; e < ru.voxels.size(); ++ e) {
        if (! ru.interior[e])
            continue;
        const int idx = ru.voxels[e];
        const int i = idx % ru.size.x(), j = (idx / ru.size.x()) % ru.size.y(), k = idx / (ru.size.x() * ru.size.y());
        // Density which would bring the stress of the voxel to the allowed one.
        const double util = std::max(double(ru.failure_index[e]) / target_fi, 1e-6);
        const double rho  = std::clamp(hi_density * std::pow(util, 1. / n_s), lo_limit, 1.);
        double &cd = cell_density[(i / cells_per_edge) + size_t(nc.x()) * ((j / cells_per_edge) + size_t(nc.y()) * (k / cells_per_edge))];
        cd = std::max(cd, rho);
    }
    std::vector<double> values;
    for (double v : cell_density)
        if (v >= 0.)
            values.push_back(v);
    if (values.size() < 2) {
        if (progress)
            progress(100);
        return out;
    }
    std::sort(values.begin(), values.end());
    // Levels: the cells are split in groups of the same size; each level takes the highest density of its group.
    const int levels = std::clamp(options.zone_levels, 2, 6);
    std::vector<double> level_density(levels), level_upper(levels);
    for (int l = 0; l < levels; ++ l) {
        const size_t last = std::min(values.size() - 1, (values.size() * size_t(l + 1)) / size_t(levels) - 1);
        level_upper[l]   = values[last];
        level_density[l] = values[last];
    }
    std::vector<int> cell_level(cell_density.size(), -1);
    for (size_t ci = 0; ci < cell_density.size(); ++ ci)
        if (cell_density[ci] >= 0.)
            cell_level[ci] = int(std::lower_bound(level_upper.begin(), level_upper.end(), cell_density[ci] - 1e-12) - level_upper.begin());
    // Meshes of the cells of each level and above (the zones of the higher levels are applied later).
    const Vec3d cell_origin = ru.origin;
    std::vector<indexed_triangle_set> level_mesh(levels);
    for (int l = 1; l < levels; ++ l) {
        std::vector<char> sel(cell_level.size(), 0);
        for (size_t ci = 0; ci < cell_level.size(); ++ ci)
            sel[ci] = cell_level[ci] >= l;
        level_mesh[l] = cells_mesh(sel, nc, cell_origin, c);
    }

    auto zoned_setup = [&](double k, Setup &s, double &base, std::vector<InfillZone> &zones) {
        s = setup;
        zones.clear();
        base = std::clamp(round_up(k * level_density[0], 0.01), lo_limit, 1.);
        for (int l = 1; l < levels; ++ l) {
            if (level_mesh[l].indices.empty())
                continue;
            const double d = std::clamp(round_up(k * level_density[l], 0.01), lo_limit, 1.);
            if (d > base + 1e-9)
                zones.push_back({ level_mesh[l], d });
        }
        s.infill.density = base;
        s.infill.zones   = zones;
        // The fixed zones (local reinforcements) win over the levels.
        const std::vector<InfillZone> fixed = fixed_zones(base);
        s.infill.zones.insert(s.infill.zones.end(), fixed.begin(), fixed.end());
    };
    auto evaluate = [&](double k, Result &r, double &base, std::vector<InfillZone> &zones) {
        Setup s;
        zoned_setup(k, s, base, zones);
        r = run(s);
        return r.ok && meets_requirements(r);
    };

    // Find a scale which meets the requirements, then the lowest one.
    double k_hi = 1., base_hi = 0.;
    std::vector<InfillZone> zones_hi;
    Result r_hi;
    bool found = evaluate(k_hi, r_hi, base_hi, zones_hi);
    for (int i = 0; i < 6 && ! found && ! (cancel && cancel()); ++ i) {
        k_hi *= 1.5;
        found = evaluate(k_hi, r_hi, base_hi, zones_hi);
        if (k_hi * level_density[0] >= 1.)
            break;
    }
    if (found) {
        double k_lo = 0.;
        for (int it = 0; it < 10 && k_hi - k_lo > 0.05 * k_hi && ! (cancel && cancel()); ++ it) {
            const double k = 0.5 * (k_lo + k_hi);
            double base;
            std::vector<InfillZone> zones;
            Result r;
            if (evaluate(k, r, base, zones)) {
                k_hi = k; base_hi = base; zones_hi = std::move(zones); r_hi = std::move(r);
            } else
                k_lo = k;
        }
        // Worth it only when lighter than the uniform infill.
        if (r_hi.mass < out.uniform.mass * 0.99) {
            out.zones_found  = true;
            out.base_density = base_hi;
            out.zones        = std::move(zones_hi);
            out.zoned        = std::move(r_hi);
        }
    }
    if (cancel && cancel()) {
        out.ok = false;
        out.error = "Cancelled";
    }
    if (progress)
        progress(100);
    return out;
}

} // namespace Fea
} // namespace Slic3r
