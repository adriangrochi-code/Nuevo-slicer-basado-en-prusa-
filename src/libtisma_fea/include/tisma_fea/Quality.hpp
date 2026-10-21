///|/ Tisma Slicer: quality of an analysis as the edge of its voxels, shared by the GUI and tisma-fea.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Quality_hpp_
#define tisma_fea_Quality_hpp_

#include <cstddef>
#include <string>

namespace Slic3r {
namespace Fea {

// Fast and Normal: a fixed number of voxels; High and Ultra: voxels of 2 and 1 perimeter line widths.
enum class Quality : int { Fast = 0, Normal = 1, High = 2, Ultra = 3 };

// Voxels of the qualities Fast and Normal.
constexpr size_t FAST_VOXELS   = 20000;
constexpr size_t NORMAL_VOXELS = 60000;
// No limit of the number of voxels: above these the analysis is large and the panel says so (about a minute with a
// million voxels on 8 cores; the jobs of many analyses, such as the lightest infill, run about 20 of them).
constexpr double LARGE_VOXELS_SINGLE = 1.2e6;
constexpr double LARGE_VOXELS_MANY   = 2.5e5;

// Voxel of the qualities High and Ultra in line widths.
inline double quality_line_widths(Quality quality) { return quality == Quality::Ultra ? 1. : 2.; }

// Edge of the voxels [mm] of a part of the volume [mm³] at the quality. With max_voxels > 0 at most that many voxels
// (limited is set when the quality was reduced to keep within it); 0: no limit.
double quality_voxel_size(Quality quality, double volume, double line_width, double max_voxels = 0., bool *limited = nullptr);

// "fast", "normal", "high", "ultra"; false for any other name.
bool        parse_quality(const std::string &name, Quality &quality);
const char* quality_name(Quality quality);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Quality_hpp_
