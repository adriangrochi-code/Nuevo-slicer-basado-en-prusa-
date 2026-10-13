///|/ Tisma Slicer: infill ribs along the stresses of the structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/Ribs.hpp"
#include "CellsMesh.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace Fea {

std::vector<RibZone> rib_zones(const Result &analysis, const RibOptions &options)
{
    std::vector<RibZone> out;
    if (! analysis.ok || analysis.stress.size() != analysis.voxels.size() || analysis.voxels.empty())
        return out;
    const double h = analysis.h;
    const int    per_edge = std::max(1, int(std::ceil(std::max(3. * h, options.cell > 0. ? options.cell : 3.) / h)));
    const double c = per_edge * h;
    const Vec3i  nc((analysis.size.x() + per_edge - 1) / per_edge, (analysis.size.y() + per_edge - 1) / per_edge,
                    (analysis.size.z() + per_edge - 1) / per_edge);
    const size_t n_cells = size_t(nc.x()) * nc.y() * nc.z();
    // Per cell: sum of w * a * (cos 2 theta, sin 2 theta), sum of w, highest failure index.
    std::vector<Vec2d>  dir_sum(n_cells, Vec2d::Zero());
    std::vector<double> weight(n_cells, 0.), load(n_cells, -1.);
    double max_load = 0.;
    for (size_t e = 0; e < analysis.voxels.size(); ++ e) {
        if (! analysis.interior.empty() && ! analysis.interior[e])
            continue;
        const int idx = analysis.voxels[e];
        const int i = idx % analysis.size.x(), j = (idx / analysis.size.x()) % analysis.size.y(),
                  k = idx / (analysis.size.x() * analysis.size.y());
        const size_t ci = size_t(i / per_edge) + size_t(nc.x()) * (size_t(j / per_edge) + size_t(nc.y()) * size_t(k / per_edge));
        const std::array<float, 6> &s = analysis.stress[e];
        // In-plane stress: xx, yy, xy (the order of Result::stress is xx, yy, zz, yz, xz, xy).
        const double sxx = s[0], syy = s[1], sxy = s[5];
        const double mean = 0.5 * (sxx + syy), radius = std::hypot(0.5 * (sxx - syy), sxy);
        const double s1 = mean + radius, s2 = mean - radius;
        const double a1 = std::abs(s1), a2 = std::abs(s2);
        if (a1 + a2 < 1e-12)
            continue;
        // Direction of the principal stress of s1; the larger in magnitude gives the direction of the ribs.
        double two_theta = std::atan2(2. * sxy, sxx - syy);
        if (a2 > a1)
            two_theta += M_PI;
        const double a = std::abs(a1 - a2) / (a1 + a2);
        const double w = std::max(0., double(analysis.failure_index.empty() ? 1.f : analysis.failure_index[e]));
        dir_sum[ci] += w * a * Vec2d(std::cos(two_theta), std::sin(two_theta));
        weight[ci]  += w;
        load[ci]     = std::max(load[ci], w);
        max_load     = std::max(max_load, w);
    }
    if (max_load <= 0.)
        return out;
    const int directions = std::clamp(options.directions, 2, 36);
    const double bin = M_PI / directions;
    std::vector<int> cell_bin(n_cells, -1);
    std::vector<double> bin_directionality(directions, 0.);
    std::vector<int>    bin_cells(directions, 0);
    for (size_t ci = 0; ci < n_cells; ++ ci) {
        if (weight[ci] <= 0. || load[ci] < options.min_load * max_load)
            continue;
        const Vec2d  m = dir_sum[ci] / weight[ci];
        const double directionality = m.norm();
        if (directionality < options.min_directionality)
            continue;
        double theta = 0.5 * std::atan2(m.y(), m.x());
        if (theta < 0.)
            theta += M_PI;
        const int b = int(std::floor(theta / bin + 0.5)) % directions;
        cell_bin[ci] = b;
        bin_directionality[b] += directionality;
        ++ bin_cells[b];
    }
    for (int b = 0; b < directions; ++ b) {
        if (bin_cells[b] == 0)
            continue;
        std::vector<char> sel(n_cells, 0);
        for (size_t ci = 0; ci < n_cells; ++ ci)
            sel[ci] = cell_bin[ci] == b;
        RibZone zone;
        zone.mesh           = cells_mesh(sel, nc, analysis.origin, c);
        zone.angle_deg      = b * 180. / directions;
        zone.directionality = bin_directionality[b] / bin_cells[b];
        zone.cells          = bin_cells[b];
        if (! zone.mesh.indices.empty())
            out.push_back(std::move(zone));
    }
    return out;
}

} // namespace Fea
} // namespace Slic3r
