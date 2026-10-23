///|/ Tisma Slicer: the corrections of the sizes of the prints from a measured test block.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "DimensionalCorrection.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

LinearFit fit_line(const std::vector<double> &nominal, const std::vector<double> &measured)
{
    LinearFit out;
    double sx = 0., sy = 0., sxx = 0., sxy = 0.;
    for (size_t i = 0; i < std::min(nominal.size(), measured.size()); ++ i)
        if (measured[i] > 0.) {
            sx  += nominal[i];
            sy  += measured[i];
            sxx += nominal[i] * nominal[i];
            sxy += nominal[i] * measured[i];
            ++ out.points;
        }
    if (out.points == 0)
        return out;
    const double n     = double(out.points);
    const double denom = n * sxx - sx * sx;
    if (out.points >= 2 && std::abs(denom) > 1e-12) {
        out.slope  = (n * sxy - sx * sy) / denom;
        out.offset = (sy - out.slope * sx) / n;
    } else
        out.offset = (sy - sx) / n;
    double sum = 0.;
    for (size_t i = 0; i < std::min(nominal.size(), measured.size()); ++ i)
        if (measured[i] > 0.)
            sum += std::pow(measured[i] - (out.slope * nominal[i] + out.offset), 2);
    out.rms = std::sqrt(sum / n);
    return out;
}

// The scale of the shrinkage compensation of PrusaSlicer for a percentage (Print::shrinkage_compensation()) and back.
static double scale_of(double percent) { return 100. / (100. - percent); }
static double percent_of(double scale) { return 100. * (1. - 1. / scale); }

DimensionalResult dimensional_correction(const DimensionalMeasures &measures, const DimensionalSettings &printed_with)
{
    DimensionalResult out;
    out.settings = printed_with;

    // Outer sizes. The slicer scaled the object by k (shrinkage compensation) and moved each side out by xy_size; the
    // printer shrinks everything by s and adds its own e to the outer sizes:
    //     measured = s * (k * nominal + 2 * xy_size) + e = slope * nominal + offset
    // The next print is as designed with k' = k / slope and xy_size' = xy_size - offset / (2 * s).
    out.x = fit_line(measures.outer_nominal, measures.outer_x);
    out.y = fit_line(measures.outer_nominal, measures.outer_y);
    out.xy_ok = out.x.points >= 2 && out.y.points >= 2;
    const double k_xy   = scale_of(printed_with.shrinkage_xy_percent);
    // The filament shrinks the same in X and in Y: a difference between them is the printer (belts, steps per mm).
    const double slope  = out.xy_ok ? 0.5 * (out.x.slope + out.y.slope) : 1.;
    const double offset = out.xy_ok ? 0.5 * (out.x.offset + out.y.offset) : 0.;
    const double s      = slope / k_xy;
    if (out.xy_ok) {
        out.settings.shrinkage_xy_percent = percent_of(k_xy / slope);
        out.settings.xy_size              = printed_with.xy_size - offset / (2. * s);
    }

    // Holes: xy_size makes them smaller, xy_hole bigger; the printer takes its own e_h:
    //     measured = s * (k * nominal - 2 * xy_size + 2 * xy_hole) - e_h
    // With the slope of the outer sizes (the same plastic) only the offset is fitted.
    if (out.xy_ok) {
        double sum = 0.;
        for (size_t i = 0; i < std::min(measures.hole_nominal.size(), measures.holes.size()); ++ i)
            if (measures.holes[i] > 0.) {
                sum += measures.holes[i] - slope * measures.hole_nominal[i];
                ++ out.holes.points;
            }
        if (out.holes.points > 0) {
            out.holes.slope  = slope;
            out.holes.offset = sum / out.holes.points;
            double sq = 0.;
            for (size_t i = 0; i < std::min(measures.hole_nominal.size(), measures.holes.size()); ++ i)
                if (measures.holes[i] > 0.)
                    sq += std::pow(measures.holes[i] - (slope * measures.hole_nominal[i] + out.holes.offset), 2);
            out.holes.rms = std::sqrt(sq / out.holes.points);
            out.holes_ok  = true;
            // The next print: s * (2 * xy_hole' - 2 * xy_size') - e_h = 0.
            out.settings.xy_hole = out.settings.xy_size - printed_with.xy_size + printed_with.xy_hole -
                                   out.holes.offset / (2. * s);
        }
    }

    // Heights: the same scale; the offset is what the first layers add or take (Z offset, squashed first layer).
    out.z    = fit_line(measures.height_nominal, measures.heights);
    out.z_ok = out.z.points >= 2;
    if (out.z_ok) {
        out.settings.shrinkage_z_percent = percent_of(scale_of(printed_with.shrinkage_z_percent) / out.z.slope);
        out.z_offset                     = out.z.offset;
    }

    // Elephant foot: the first layer of the widest step against the same width higher up (the outer X fit there).
    if (measures.first_layer_width > 0. && out.xy_ok && ! measures.outer_nominal.empty()) {
        const double widest = *std::max_element(measures.outer_nominal.begin(), measures.outer_nominal.end());
        const double above  = out.x.slope * widest + out.x.offset;
        out.foot_ok = true;
        out.settings.elephant_foot = std::max(0., printed_with.elephant_foot + 0.5 * (measures.first_layer_width - above));
    }
    return out;
}

} // namespace Slic3r
