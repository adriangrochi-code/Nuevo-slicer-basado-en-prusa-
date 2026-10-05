///|/ Tisma Slicer: local reinforcements and 3D lattice for the loads (phase 6).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Structures_hpp_
#define tisma_fea_Structures_hpp_

#include <libslic3r/BoundingBox.hpp>

#include "Analysis.hpp"
#include "Optimize.hpp"

namespace Slic3r {
namespace Fea {

// ---------------------------------------------------------------------------------------------------------------------
// Local reinforcements: more perimeters and more infill around the supports and the loads.

struct ReinforcementOptions
{
    // Distance around the fixed faces and the loads [mm], 0 = automatic (twice the load radius, at least 5 mm).
    double radius { 0. };
    // Perimeters added inside the reinforcement.
    int    extra_perimeters { 2 };
    // Infill density inside the reinforcement.
    double density { 0.4 };
    // Edge of the cells of the reinforcement mesh [mm].
    double cell { 2. };
};

// The reinforcement zone of a setup (mesh in the coordinates of the setup), or an empty mesh without supports or loads.
InfillZone reinforcement_zone(const indexed_triangle_set &mesh, const Setup &setup, const ReinforcementOptions &options);

struct ReinforcementResult
{
    bool        ok { false };
    std::string error;
    InfillZone  zone;
    // Lightest infill of the rest of the part with the reinforcement, compared with the lightest infill without it.
    OptimizeResult with;
    OptimizeResult without;
};

// Reinforces around the supports and loads and searches the lightest uniform infill for the rest of the part.
ReinforcementResult optimize_reinforcement(const indexed_triangle_set &mesh, const Setup &setup, const ReinforcementOptions &options,
                                           std::function<bool()> cancel = nullptr, std::function<void(int)> progress = nullptr);

// ---------------------------------------------------------------------------------------------------------------------
// 3D lattice: struts of variable thickness instead of the infill, connected to the walls.
// Printable without supports: vertical struts and struts at 45 degrees in the XZ and YZ planes, no horizontal struts.

struct LatticeOptions
{
    // Edge of the cubic cells [mm].
    double cell { 8. };
    // Diameter of the struts [mm]: the thinnest printable one (about two extrusion widths) and the thickest one.
    double min_diameter { 0.9 };
    double max_diameter { 3. };
    // Sides of the prisms of the struts.
    int    segments { 6 };
};

// Grid of the lattice nodes over a bounding box.
struct LatticeGrid
{
    Vec3d  origin { Vec3d::Zero() };
    double cell { 8. };
    Vec3i  cells { Vec3i::Zero() };
    static LatticeGrid over(const BoundingBoxf3 &bbox, double cell);
};

// Mesh of the struts (closed prisms, overlapping at the nodes). diameter_at(midpoint) gives the diameter of each strut.
indexed_triangle_set lattice_mesh(const LatticeGrid &grid, const LatticeOptions &options,
                                  const std::function<double(const Vec3d &midpoint)> &diameter_at);

struct LatticeResult
{
    bool        ok { false };
    std::string error;
    bool        feasible { false };
    LatticeGrid grid;
    // Uniform struts: the thinnest diameter which meets the requirements.
    double      uniform_diameter { 0. };
    Result      uniform;
    // Variable struts following the stresses (diameters per cell), and their lattice.
    bool        variable_found { false };
    std::vector<float> cell_diameter;
    Result      variable;
    // Lattice to apply (the variable one when found, else the uniform one): a solid modifier, the infill of the part at 0 %.
    indexed_triangle_set mesh;
    double      min_used_diameter { 0. };
    double      max_used_diameter { 0. };
    int         analyses { 0 };
};

// Lattice replacing the infill of the part (infill 0 %, the walls are kept). setup.infill gives the walls.
LatticeResult optimize_lattice(const indexed_triangle_set &mesh, const Setup &setup, const LatticeOptions &options,
                               std::function<bool()> cancel = nullptr, std::function<void(int)> progress = nullptr);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Structures_hpp_
