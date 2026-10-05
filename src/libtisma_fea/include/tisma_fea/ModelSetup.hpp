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

bool build_analysis_input(const ModelObject &object, size_t instance_idx, const std::string &filament_type,
                          ModelAnalysisInput &out, std::string &error);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_ModelSetup_hpp_
