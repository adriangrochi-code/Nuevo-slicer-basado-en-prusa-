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
};

// print_config: the full print configuration; when given, the analysis takes into account the walls, the top and
// bottom layers, the infill and the infill modifiers of the object (phase 6). nullptr = solid part.
bool build_analysis_input(const ModelObject &object, size_t instance_idx, const std::string &filament_type,
                          ModelAnalysisInput &out, std::string &error, const DynamicPrintConfig *print_config = nullptr);

// Name of the infill modifiers created by apply_infill().
extern const char *INFILL_ZONE_NAME;

// Applies an infill to the object: its infill density and, for the zones (in print coordinates of the instance),
// infill modifiers. The modifiers created by a previous call are replaced. Returns the number of modifiers.
size_t apply_infill(ModelObject &object, size_t instance_idx, double density, const std::vector<InfillZone> &zones);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_ModelSetup_hpp_
