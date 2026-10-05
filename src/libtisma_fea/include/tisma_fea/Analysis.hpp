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
};

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
};

enum class Verdict
{
    // Every point of the part has at least the required safety factor.
    Holds,
    // The safety factor is between 1 and the required one somewhere.
    LowMargin,
    // The stresses exceed the strength of the material somewhere.
    OutOfLoad,
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

    double              max_displacement { 0. };
    double              max_von_mises { 0. };
    double              max_failure_index { 0. };
    // Safety factor of the part (1 / max_failure_index).
    double              safety_factor { 0. };
    // Position of the most loaded voxel.
    Vec3d               critical_point { Vec3d::Zero() };
    double              temperature_factor { 1. };
    Verdict             verdict { Verdict::Holds };
    // Materials of the table that would hold with the required safety factor at the temperature, with the
    // safety factor estimated from the same stresses (exact for isotropic materials).
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
