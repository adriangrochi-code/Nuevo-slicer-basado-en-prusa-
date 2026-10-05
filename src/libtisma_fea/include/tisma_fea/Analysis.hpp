///|/ Tisma Slicer: linear static structural analysis of a part on a voxel grid (phase 5).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Analysis_hpp_
#define tisma_fea_Analysis_hpp_

#include <cstdint>
#include <functional>
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

// Region of the part with its own infill density (an infill modifier), in the coordinates of the setup.
struct InfillZone
{
    indexed_triangle_set mesh;
    double               density { 0.2 };
};

// Printed structure of the part (phase 6): a solid shell (perimeters, top and bottom layers) and an infill whose
// properties are homogenized: E_infill = E * density^stiffness_exponent, strength * density^strength_exponent.
// The exponents depend on the infill pattern (see infill_exponents()); they are approximate and should be
// calibrated with printed specimens.
struct InfillModel
{
    bool                    enabled { false };
    // Infill density of the part (0..1).
    double                  density { 0.2 };
    // Thickness of the solid shell [mm] on walls and on top / bottom surfaces.
    double                  wall_thickness { 0.9 };
    double                  top_bottom_thickness { 0.8 };
    double                  stiffness_exponent { 1.5 };
    double                  strength_exponent { 1.5 };
    // Zones with other densities, applied in order (the later ones win).
    std::vector<InfillZone> zones;
    // When set, the density of the infill at a point (overrides density and zones). Used by the optimizer.
    std::function<double(const Vec3d &point)> density_field;
};

// Exponents of the homogenized infill for a pattern name of the print settings (rectilinear, grid, gyroid, ...).
std::pair<double, double> infill_exponents(const std::string &pattern);

struct Setup
{
    std::string          material { "PLA" };
    double               temperature { REFERENCE_TEMPERATURE };   // ambient temperature where the part is used
    double               required_safety_factor { 2. };
    std::vector<Fixture> fixtures;
    std::vector<Load>    loads;
    // Edge of the voxels [mm], 0 = automatic (about target_voxels voxels inside the part).
    double               voxel_size { 0. };
    size_t               target_voxels { 60000 };
    // Convergence of the iterative solver (relative residual).
    double               tolerance { 1e-7 };
    // Walls and infill of the print; disabled = solid part.
    InfillModel          infill;
};

enum class Verdict
{
    // Every point of the part has at least the required safety factor.
    Holds,
    // The safety factor is between 1 and the required one somewhere.
    LowMargin,
    // The stresses exceed the strength of the material somewhere.
    OutOfLoad,
    // The part holds, but a load moves more than its allowed displacement.
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
    // Largest displacement of the nodes of each load [mm], in the order of Setup::loads.
    std::vector<double> load_displacement;
    Verdict             verdict { Verdict::Holds };
    // Materials of the table that would hold with the required safety factor at the temperature (and keep the
    // displacements within the limits), with the safety factor estimated from the same stresses (exact for
    // isotropic materials; the displacements are scaled with the stiffness).
    std::vector<std::pair<std::string, double>> alternatives;

    int                 iterations { 0 };
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
