///|/ Tisma Slicer: lightest infill which meets the safety factor and the deformation limits (phase 6).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Optimize_hpp_
#define tisma_fea_Optimize_hpp_

#include "Analysis.hpp"

namespace Slic3r {
namespace Fea {

struct OptimizeOptions
{
    double min_density { 0.05 };
    double max_density { 1.0 };
    // Precision of the density search.
    double tolerance { 0.01 };
    // Infill by zones following the stresses (as infill modifiers), besides the uniform infill.
    bool   zones { true };
    // Number of density levels of the zones (the lowest one is the infill of the part).
    int    zone_levels { 3 };
    // Edge of the cells of the zones [mm], 0 = automatic (3 voxels, at least 3 mm).
    double zone_cell { 0. };
    // Zones always applied over the searched infill (local reinforcements).
    std::vector<InfillZone> fixed_zones;
};

struct OptimizeResult
{
    bool        ok { false };
    std::string error;
    // A density meets the requirements (safety factor, deformation limits, temperature).
    bool        feasible { false };

    // 1) Uniform infill: the lowest density which meets the requirements, and its analysis.
    double      uniform_density { 0. };
    Result      uniform;

    // 2) Infill by zones: density of the part and zones with more infill where the stresses are higher.
    bool        zones_found { false };
    double      base_density { 0. };
    std::vector<InfillZone> zones;
    Result      zoned;

    int         analyses { 0 };
};

// The requirements of the setup are met (the verdict is Holds).
inline bool meets_requirements(const Result &r) { return r.ok && r.verdict == Verdict::Holds; }

// Searches the lightest infill for the part. setup.infill gives the shell thickness and the exponents of the infill
// pattern (it is enabled by the optimizer); its density and zones are replaced.
OptimizeResult optimize_infill(const indexed_triangle_set &mesh, const Setup &setup, const OptimizeOptions &options = {},
                               std::function<bool()> cancel = nullptr, std::function<void(int)> progress = nullptr);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Optimize_hpp_
