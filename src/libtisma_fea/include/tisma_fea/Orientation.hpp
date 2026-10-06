///|/ Tisma Slicer: the orientation of the print for the loads (the layers are weaker across than along).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Orientation_hpp_
#define tisma_fea_Orientation_hpp_

#include "Analysis.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Slic3r {
namespace Fea {

struct OrientationCandidate
{
    // Build direction in the coordinates of the setup (the current one is Z).
    Vec3d  build_direction { Vec3d::UnitZ() };
    // "Z" (current), "X" or "Y": the axis of the part which would be printed vertically.
    std::string axis;
    Result result;
};

struct OrientationResult
{
    bool        ok { false };
    std::string error;
    // The current orientation first, then the part laid on its X and Y axes.
    std::vector<OrientationCandidate> candidates;
    // Index of the candidate with the highest safety factor (the current one when the gain is below 5 %).
    size_t      best { 0 };
};

// Analyzes the part printed with the layers stacked along Z (current), X and Y of the setup, with the same loads.
// The geometry, the supports and the loads do not move: only the direction of the weak axis of the material
// changes. Walls and infill keep their thickness (the top and bottom layers are not recomputed for the new
// orientation), the printability (overhangs, supports) is not checked.
OrientationResult recommend_orientation(const indexed_triangle_set &mesh, const Setup &setup,
                                        std::function<bool()> cancel = nullptr, std::function<void(int)> progress = nullptr);

// Rotation which brings a build direction of the setup to Z (to apply to the part to print it that way).
Transform3d rotation_to_print(const Vec3d &build_direction);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Orientation_hpp_
