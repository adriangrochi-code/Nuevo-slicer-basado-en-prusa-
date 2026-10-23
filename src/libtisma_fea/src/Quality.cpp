///|/ Tisma Slicer: quality of an analysis as the edge of its voxels.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/Quality.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace Fea {

double quality_voxel_size(Quality quality, double volume, double line_width, double max_voxels, bool *limited)
{
    volume = std::max(std::abs(volume), 1e-9);
    double h = 0.;
    switch (quality) {
    case Quality::Fast:   h = std::cbrt(volume / double(FAST_VOXELS)); break;
    case Quality::Normal: h = std::cbrt(volume / double(NORMAL_VOXELS)); break;
    default:              h = quality_line_widths(quality) * std::max(line_width, 1e-3); break;
    }
    const bool over = max_voxels > 0. && volume / (h * h * h) > max_voxels;
    if (over)
        h = std::cbrt(volume / max_voxels);
    if (limited)
        *limited = over;
    return h;
}

bool parse_quality(const std::string &name, Quality &quality)
{
    for (Quality q : { Quality::Fast, Quality::Normal, Quality::High, Quality::Ultra })
        if (name == quality_name(q)) {
            quality = q;
            return true;
        }
    return false;
}

const char* quality_name(Quality quality)
{
    switch (quality) {
    case Quality::Fast:   return "fast";
    case Quality::Normal: return "normal";
    case Quality::High:   return "high";
    case Quality::Ultra:  return "ultra";
    }
    return "normal";
}

} // namespace Fea
} // namespace Slic3r
