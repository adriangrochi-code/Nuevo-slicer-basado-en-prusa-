///|/ Tisma Slicer: infill of the interior of a hollowed resin part.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SLA_InteriorInfill_hpp_
#define slic3r_SLA_InteriorInfill_hpp_

#include <array>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

struct indexed_triangle_set;

namespace Slic3r {
namespace sla {

// A hollowed part (hollowing_enable) can keep struts inside its cavity: the cavity, which the slicing subtracts from
// the part, is cut by a lattice of struts of square section in five directions, vertical and at 45 degrees in XZ and
// YZ, so that every strut supports itself on the layers below (no supports inside the cavity).
// density: fraction of the cavity filled by the struts (0..1, the crossings counted twice); cell: spacing of the
// struts of a direction [mm].
constexpr int    INFILL_DIRECTIONS    = 5;
constexpr double INFILL_MIN_STRUT     = 0.8;   // mm: thinner struts do not print
std::array<Vec3d, INFILL_DIRECTIONS> infill_directions();

// Width of the struts [mm] for a density and a cell: density = directions x (width / cell)^2.
double infill_strut_width(double density, double cell);

// The struts which cross the box, as one mesh of separate boxes (they overlap at the crossings).
indexed_triangle_set infill_struts(const BoundingBoxf3 &box, double density, double cell);

// Removes the struts from the cavity (a closed mesh): the cavity is voxelized with voxels of at most a third of the
// strut width (and at least those of voxel_scale, voxels per mm), the struts subtracted and the cavity meshed again.
// Returns false and keeps the cavity when it cannot be done (density 0, an empty cavity).
bool subtract_infill(indexed_triangle_set &cavity, double density, double cell, float voxel_scale);

} // namespace sla
} // namespace Slic3r

#endif // slic3r_SLA_InteriorInfill_hpp_
