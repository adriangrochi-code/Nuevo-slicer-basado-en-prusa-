///|/ Tisma Slicer: linear static structural analysis of a part on a voxel grid (phase 5).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Analysis_hpp_
#define tisma_fea_Analysis_hpp_

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include <libslic3r/Point.hpp>
#include <admesh/stl.h>

#include "Materials.hpp"

namespace Slic3r {
namespace Fea {

// Units of the analysis: mm, N, MPa (N/mm²), °C. The Z axis is the build direction (across the layers).

// Faces of the mesh where the part is held: the nodes on them cannot move.
struct Fixture
{
    std::vector<int> triangles;
};

struct Load
{
    enum class Type { Point, Faces };
    Type             type { Type::Point };
    // Point loads: position on the part; the force is shared by the nodes within radius.
    Vec3d            point { Vec3d::Zero() };
    double           radius { 0. };     // 0 = automatic (1.5 voxels)
    // Face loads: the force is shared by the nodes on the faces.
    std::vector<int> triangles;
    // Total force [N].
    Vec3d            force { Vec3d::Zero() };
    // Largest allowed displacement where the load is applied [mm], 0 = no limit.
    double           max_displacement { 0. };
};

// Faces of the part which must not move more than a limit, whatever the loads (a fit, a sealing face, a
// mechanism). Other places may move more: the rest of the part is only checked for strength.
struct DisplacementLimit
{
    std::vector<int> triangles;
    // Largest allowed displacement of the faces [mm] (> 0).
    double           max_displacement { 0. };
};

// Region of the part with its own infill density (an infill modifier), in the coordinates of the setup.
struct InfillZone
{
    indexed_triangle_set mesh;
    // < 0: the density of what is below (the part or the previous zones), as a modifier without fill_density.
    double               density { 0.2 };
    // Aligned infill (aligned rectilinear: the lines of every layer in one direction): direction of the lines from
    // the X axis of the setup in the plane of the layers [°]; < 0 = not aligned; NaN = the one of what is below.
    double               aligned_angle_deg { std::numeric_limits<double>::quiet_NaN() };
    // Thickness of the walls inside the zone (more perimeters: local reinforcement), < 0 = the one of the part.
    double               wall_thickness { -1. };
    // Exponents of the infill of the zone, < 0 = the ones of the part.
    double               stiffness_exponent { -1. };
    double               strength_exponent { -1. };
    // Material deposited per nominal density of the pattern of the zone (infill_deposit_factor()), < 0 = the part's.
    double               deposit_factor { -1. };
    // The mesh is material at the density of the zone (a solid modifier, the struts of a lattice): every voxel
    // gets the fraction of its volume inside the mesh, instead of all or nothing by its center.
    bool                 volume_fraction { false };
    // With volume_fraction: the fraction is averaged over cubic blocks of this edge [mm] from the minimum of the
    // zone (the cells of a lattice), so that the zone is a homogenized material. 0 = per voxel.
    double               homogenize_cell { 0. };
};

// Printed structure of the part (phase 6): a solid shell (perimeters, top and bottom layers) and an infill whose
// properties are homogenized: E_infill = E * density^stiffness_exponent, strength * density^strength_exponent.
// The exponents depend on the infill pattern (see infill_exponents()); they are approximate and should be
// calibrated with printed specimens.
// Aligned infill (aligned_angle_deg >= 0, the ribs) is orthotropic instead (see aligned_infill_factors()).
struct InfillModel
{
    bool                    enabled { false };
    // Infill density of the part (0..1).
    double                  density { 0.2 };
    // Thickness of the solid shell [mm] on walls and on top / bottom surfaces.
    double                  wall_thickness { 0.9 };
    double                  top_bottom_thickness { 0.8 };
    // Perimeters of the walls and the width of one of them [mm]. The walls are as thick as the material they hold:
    // the spacing of the external perimeter plus that of the others (rounded beads, Flow::rounded_rectangle_
    // extrusion_spacing()), see wall_thickness_of().
    int                     perimeters { 2 };
    double                  perimeter_width { 0.45 };
    double                  perimeter_spacing { 0.45 };
    double                  external_perimeter_spacing { 0.45 };
    // Material deposited by the infill pattern per nominal density (infill_deposit_factor()): the infill of the
    // analysis is min(1, deposit_factor * density) of the material, for the mass and the properties.
    double                  deposit_factor { 1. };
    double                  stiffness_exponent { 1.5 };
    double                  strength_exponent { 1.5 };
    // Aligned infill of the whole part: direction of the lines [°] as InfillZone::aligned_angle_deg, < 0 = not.
    double                  aligned_angle_deg { -1. };
    // Zones with other densities, applied in order (the later ones win).
    std::vector<InfillZone> zones;
    // When set, the density of the infill at a point (overrides density and zones). Used by the optimizer.
    std::function<double(const Vec3d &point)> density_field;
};

// Exponents of the homogenized infill for a pattern name of the print settings (rectilinear, grid, gyroid, ...).
std::pair<double, double> infill_exponents(const std::string &pattern);

// Fraction of material the slicer actually deposits in the sparse infill per nominal density of the pattern, measured
// on Tisma's own slicing (test "Infill model matches the sliced toolpaths", docs/FEA.md): the lines overlap at their
// crossings and anchors, honeycomb deposits a third more, 3D honeycomb less. Adaptive and support cubic and lightning
// depend on the shape of the part (denser only under the top surfaces): 1, approximate.
double infill_deposit_factor(const std::string &pattern);

// Thickness of solid material of walls of n perimeters.
inline double wall_thickness_of(const InfillModel &infill, int n)
{
    return n <= 0 ? 0. : infill.external_perimeter_spacing + double(n - 1) * infill.perimeter_spacing;
}

// Aligned infill: all the lines of every layer in one direction (L), stacked into walls along the layer normal (N),
// with gaps between them across (T). Factors of the properties of the solid material:
// - along the lines and along the walls (E_L, E_N, G_LN and their strengths): the lines are continuous, so the rule
//   of mixtures (Voigt bound) gives density;
// - across the lines (E_T, G_LT, G_TN and their strengths): the walls are only joined by the perimeters and the
//   connections of the lines, so they work in bending; the scaling of bending dominated cellular solids is used,
//   E ~ density^2 and strength ~ 0.3 * density^1.5 (Gibson & Ashby, "Cellular Solids", 2nd ed., 1997, ch. 5), and
//   never more than the isotropic infill of the same density (iso_stiffness, iso_strength).
// Approximate values, to be calibrated with printed specimens as the exponents.
struct AlignedInfillFactors
{
    double along { 1. };
    double across { 1. };
    double strength_along { 1. };
    double strength_across { 1. };
};
AlignedInfillFactors aligned_infill_factors(double density, double iso_stiffness, double iso_strength);

// Angular step of the groups of layer normals (Setup::layer_normal) [°].
constexpr double LAYER_NORMAL_STEP_DEG = 2.;

struct Setup
{
    std::string          material { "PLA" };
    double               temperature { REFERENCE_TEMPERATURE };   // ambient temperature where the part is used
    double               required_safety_factor { 2. };
    std::vector<Fixture> fixtures;
    std::vector<Load>    loads;
    // Faces with their own displacement limit.
    std::vector<DisplacementLimit> limits;
    // Edge of the voxels [mm], 0 = automatic (about target_voxels voxels inside the part).
    double               voxel_size { 0. };
    size_t               target_voxels { 60000 };
    // Convergence of the iterative solver (relative residual).
    double               tolerance { 1e-7 };
    // Walls and infill of the print; disabled = solid part.
    InfillModel          infill;
    // Direction in which the layers are stacked (the Z of the printer) in the coordinates of the setup. The
    // material is weaker across the layers (stiffness E_z, layer adhesion strength_z).
    Vec3d                build_direction { Vec3d::UnitZ() };
    // Curved (non-planar) layers: the normal of the layer at a point of the setup, which replaces build_direction
    // locally. The normals are grouped in directions about LAYER_NORMAL_STEP_DEG apart.
    std::function<Vec3d(const Vec3d &point)> layer_normal;
    // Nozzle temperature of the print [°C], 0 = unknown: scales the layer adhesion (layer_adhesion_factor()).
    double               print_temperature { 0. };
};

enum class Verdict
{
    // Every point of the part has at least the required safety factor.
    Holds,
    // The safety factor is between 1 and the required one somewhere.
    LowMargin,
    // The stresses exceed the strength of the material somewhere.
    OutOfLoad,
    // The part holds, but a load or a limited zone moves more than its allowed displacement.
    TooFlexible,
    // The temperature is above the maximum service temperature of the material.
    OutOfTemperature,
    // The linear analysis does not describe the material (elastomers).
    NotApplicable,
};

struct Result
{
    bool                ok { false };
    std::string         error;

    // Voxel grid: voxel (i, j, k) has its minimum corner at origin + h * (i, j, k).
    Vec3d               origin { Vec3d::Zero() };
    double              h { 0. };
    Vec3i               size { Vec3i::Zero() };
    // Index of every solid voxel (i + size.x * (j + size.y * k)) and, for each of them, the results.
    std::vector<int>    voxels;
    std::vector<float>  von_mises;          // [MPa]
    std::vector<float>  failure_index;      // stress / strength at the temperature, > 1 = breaks
    std::vector<Vec3f>  displacement;       // at the center of the voxel [mm]
    // Homogenized stress at the center of the voxel in the coordinates of the setup [MPa]: xx, yy, zz, yz, xz, xy.
    std::vector<std::array<float, 6>> stress;
    // Fraction of material in the voxel (1 = solid, shell; the infill density inside).
    std::vector<float>  density;
    // Interior voxels (not in the shell): their density is the one of the infill.
    std::vector<char>   interior;
    // Estimated mass of the printed part and of the solid part [g].
    double              mass { 0. };
    double              solid_mass { 0. };

    double              max_displacement { 0. };
    double              max_von_mises { 0. };
    double              max_failure_index { 0. };
    // Safety factor of the part (1 / max_failure_index).
    double              safety_factor { 0. };
    // Position of the most loaded voxel.
    Vec3d               critical_point { Vec3d::Zero() };
    double              temperature_factor { 1. };
    // Factor of the layer adhesion for the nozzle temperature of the print (1 when unknown).
    double              layer_adhesion_factor { 1. };
    // Largest displacement of the nodes of each load [mm], in the order of Setup::loads.
    std::vector<double> load_displacement;
    // Largest displacement of the faces of each limited zone [mm], in the order of Setup::limits.
    std::vector<double> limit_displacement;
    Verdict             verdict { Verdict::Holds };
    // Materials of the table that would hold with the required safety factor at the temperature (and keep the
    // displacements within the limits), with the safety factor estimated from the same stresses (exact for
    // isotropic materials; the displacements are scaled with the stiffness).
    std::vector<std::pair<std::string, double>> alternatives;

    int                 iterations { 0 };
    // Number of material orientations used (1 with flat layers).
    size_t              layer_orientations { 1 };
    // Voxels with aligned infill (ribs).
    size_t              aligned_voxels { 0 };
    double              residual { 0. };
};

// Runs the analysis of a closed mesh (in the coordinates of the setup). cancel() is polled and the analysis
// stops with an error when it returns true; progress() receives 0..100.
Result analyze(const indexed_triangle_set &mesh, const Setup &setup,
               std::function<bool()> cancel = nullptr, std::function<void(int)> progress = nullptr);

// Text of a verdict (English, to be translated by the GUI).
const char* verdict_name(Verdict verdict);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Analysis_hpp_
