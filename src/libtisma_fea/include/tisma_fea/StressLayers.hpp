///|/ Tisma Slicer: curved layers that follow the stresses of the structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_StressLayers_hpp_
#define tisma_fea_StressLayers_hpp_

#include <functional>
#include <string>

#include <libslic3r/Engineering.hpp>

#include "Analysis.hpp"
#include "ModelSetup.hpp"

namespace Slic3r {
namespace Fea {

// Printed parts are weakest across the layers. Where the part is pulled (the largest principal stress is a tension),
// the layers should contain the direction of the pull, so that it runs along the extrusions instead of across the
// layers. A 3-axis printer can only tilt the layers a little (the free angle of the print head), so the layers are
// tilted towards the pull as far as the head allows: for each voxel the slope of the layer along the horizontal
// direction of the pull, tan = pull_z / pull_horizontal, clamped to the head. The offset of the layers is the least
// squares fit of these slopes, weighted by the tension and by how horizontal the pull is (a vertical pull cannot be
// helped), with smooth layers, the layer thickness kept (small change of the offset along Z) and flat first layers.
// The idea of layers that follow the principal stresses is that of S^3-Slicer (Zhang et al., SIGGRAPH Asia 2022) and
// of CurviSlicer (Etienne et al., SIGGRAPH 2019) for 3 axes; this formulation is Tisma's own and much simpler.
struct StressLayersParams
{
    // Free slopes of the print head along X and Y (tangents).
    double tan_x { 0.4 };
    double tan_y { 0.4 };
    // Layers flat up to this height above the bottom of the part [mm].
    double z_flat { 0.6 };
    // Largest number of nodes of the grid (the cells grow for large parts).
    size_t max_nodes { 40000 };
};

struct StressLayersResult
{
    bool                  ok { false };
    std::string           error;
    EngineeringLayerField field;
    // Voxels in tension that guided the layers.
    size_t                tension_voxels { 0 };
};

// mesh: the part in the coordinates of the setup (as placed on the bed, Z up); analysis: its result with flat layers.
StressLayersResult stress_layers(const indexed_triangle_set &mesh, const Result &analysis, const StressLayersParams &params);

struct CurvedLayersResult
{
    bool                  ok { false };
    std::string           error;
    // The layers (with the safety factors), the analysis with flat layers and with the curved layers as they will be
    // sliced (within the print head, flat first layers).
    EngineeringLayerField field;
    Result                flat;
    Result                curved;
};

// For an object of the model (input from build_analysis_input()): analysis with flat layers, the curved layers that
// follow its stresses, and the analysis with them, with the non-planar settings of the print configuration (print
// head, flat first layers, transitions).
CurvedLayersResult curved_layers_for_strength(const ModelAnalysisInput &input, const DynamicPrintConfig &print_config,
                                              std::function<bool()> cancel = nullptr, std::function<void(int)> progress = nullptr);

// Normals of the curved layers at the points of the setup (for Setup::layer_normal), from the offset of a field whose
// part has its minimum corner at part_min (without the flat first layers and the limits of the slicing).
std::function<Vec3d(const Vec3d &point)> layer_field_normals(const EngineeringLayerField &field, const Vec3d &part_min);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_StressLayers_hpp_
