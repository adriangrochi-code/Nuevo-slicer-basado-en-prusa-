///|/ Tisma Slicer: infill ribs along the stresses of the structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Ribs_hpp_
#define tisma_fea_Ribs_hpp_

#include <vector>

#include "Analysis.hpp"

namespace Slic3r {

class ModelObject;

namespace Fea {

// The infill as ribs: where the part works in one direction within the layers (a bar pulled or bent along it), its
// infill lines are laid along that direction in every layer (aligned rectilinear infill at that angle), so that they
// carry the load along their length as the ribs of a part designed for the loads, instead of crossing it.
//
// For every cell of the interior, from the stresses of the analysis: in each voxel the in-plane stress (xx, yy, xy)
// has two principal stresses; the larger in magnitude gives the direction (theta) and the difference of magnitudes
// how one-directional it is, a = (|s1| - |s2|) / (|s1| + |s2|). The cell averages a * (cos 2 theta, sin 2 theta)
// weighted by the failure index of the voxels (tension and compression along the same line count alike, which a
// sum of the tensors would cancel in bending). Cells loaded enough and directional enough are grouped by the angle
// of their average, in steps of 180° / directions. Tisma's own method; the idea of printing along the principal
// stresses is that of Tam & Mueller, "Additive Manufacturing Along Principal Stress Lines" (3D Printing and
// Additive Manufacturing, 2017), here reduced to the infill angle of zones.
struct RibOptions
{
    // Edge of the cells [mm], 0 = automatic (3 voxels, at least 3 mm), as the zones of the infill.
    double cell { 0. };
    // Smallest average directionality of a cell (0 = any stress, 1 = only pure one-directional stress).
    double min_directionality { 0.5 };
    // Cells whose highest failure index is below this fraction of the part's highest are not worth ribs.
    double min_load { 0.2 };
    // Number of rib directions over 180° (12: steps of 15°, the ribs at most 7.5° off the stress).
    int    directions { 12 };
};

struct RibZone
{
    // Cells of the zone, in the coordinates of the setup (as the infill zones).
    indexed_triangle_set mesh;
    // Direction of the ribs from the X axis in the setup [°], 0..180.
    double               angle_deg { 0. };
    // Average directionality of its cells and their number.
    double               directionality { 0. };
    int                  cells { 0 };
};

std::vector<RibZone> rib_zones(const Result &analysis, const RibOptions &options = {});

// Name of the modifiers created by apply_ribs().
inline constexpr const char *RIB_ZONE_NAME = "Infill ribs";

// Replaces the rib modifiers of the object with the zones (aligned rectilinear infill at their angle; the density
// stays the one of the part and of the infill zones). Returns the number of modifiers added.
size_t apply_ribs(ModelObject &object, size_t instance_idx, const std::vector<RibZone> &zones);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Ribs_hpp_
