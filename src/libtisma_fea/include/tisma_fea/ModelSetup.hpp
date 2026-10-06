///|/ Tisma Slicer: analysis of an object of the model with its EngineeringSetup (phase 5).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_ModelSetup_hpp_
#define tisma_fea_ModelSetup_hpp_

#include <string>

#include "Analysis.hpp"

namespace Slic3r {

class ModelObject;
class DynamicPrintConfig;

namespace Fea {

// Mesh of the printable parts of an instance of the object in print coordinates (as placed on the bed, Z up) and
// the setup of the analysis with the supports and loads of ModelObject::engineering.
// filament_type is the type of the filament of the object, used when the setup does not choose a material.
struct ModelAnalysisInput
{
    indexed_triangle_set mesh;
    Setup                setup;
    // Material actually used (the chosen one or the one of the filament).
    std::string          material;
    // Width of the perimeter lines and layer height of the print [mm] (defaults without print configuration). The
    // quality of the analysis is given as voxels of a multiple of the line width.
    double               line_width { 0.45 };
    double               layer_height { 0.2 };
};

// print_config: the full print configuration; when given, the analysis takes into account the walls, the top and
// bottom layers, the infill and the infill modifiers of the object (phase 6). nullptr = solid part.
bool build_analysis_input(const ModelObject &object, size_t instance_idx, const std::string &filament_type,
                          ModelAnalysisInput &out, std::string &error, const DynamicPrintConfig *print_config = nullptr);

// Width of the perimeter lines [mm] of the object with the print configuration (a percent is of the layer height, as
// in PrusaSlicer).
double line_width(const ModelObject &object, const DynamicPrintConfig &print_config);

// Name of the infill modifiers created by apply_infill().
extern const char *INFILL_ZONE_NAME;

// Applies an infill to the object: its infill density and, for the zones (in print coordinates of the instance),
// infill modifiers. The modifiers created by a previous call are replaced. Returns the number of modifiers.
size_t apply_infill(ModelObject &object, size_t instance_idx, double density, const std::vector<InfillZone> &zones);

// Names of the modifiers of the local reinforcements and of the lattice.
extern const char *REINFORCEMENT_NAME;
extern const char *LATTICE_NAME;

// Applies a local reinforcement: the infill density of the object and a modifier with more perimeters and infill
// (zone in print coordinates). Replaces a previous reinforcement.
void apply_reinforcement(ModelObject &object, size_t instance_idx, double density, const InfillZone &zone, int perimeters);

// Applies a lattice (mesh of the struts in print coordinates of the cells of edge cell): infill of the object 0 % and
// a solid modifier with the struts. Replaces a previous lattice and the infill zones.
void apply_lattice(ModelObject &object, size_t instance_idx, const indexed_triangle_set &struts, double cell);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_ModelSetup_hpp_
