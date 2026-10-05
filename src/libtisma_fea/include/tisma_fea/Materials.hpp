///|/ Tisma Slicer: materials of the structural analysis (phase 5 of docs/IMPLEMENTATION_ROADMAP.md).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Materials_hpp_
#define tisma_fea_Materials_hpp_

#include <string>
#include <vector>

namespace Slic3r {
namespace Fea {

// Properties of a printed part (not of the raw resin), at room temperature (23 °C), for a solid part.
// Units: MPa and °C. The values are typical values of technical data sheets and literature for FDM prints,
// they are approximate: the real values depend on the brand, the printer, the settings and the orientation.
struct Material
{
    // Key of the material, the same as the filament_type of the filament profiles when there is one.
    std::string key;
    std::string name;
    // Young's modulus in the plane of the layers (along the extrusions) and across the layers.
    double      E_xy { 0. };
    double      E_z  { 0. };
    double      poisson { 0.35 };
    // Tensile strength in the plane of the layers and between layers (layer adhesion).
    double      strength_xy { 0. };
    double      strength_z  { 0. };
    // Glass transition temperature.
    double      glass_transition { 0. };
    // Highest temperature at which the part is still considered structural (close to the HDT at 0.45 MPa).
    double      max_service_temperature { 0. };
    // The linear elastic analysis is meaningful for the material (not for elastomers like TPU).
    bool        linear_analysis_valid { true };
    // Short note about the values.
    std::string note;
    // Density of the solid material [g/cm³], for the estimation of the mass.
    double      density { 1.2 };
};

// Reference temperature of the properties.
constexpr double REFERENCE_TEMPERATURE = 23.;

// All the materials of the table.
const std::vector<Material>& materials();
// The material with the key (case insensitive), or nullptr.
const Material*              find_material(const std::string &key);
// The material for a filament_type of a filament profile (PLA, PET, ABS, FLEX, ...), or nullptr.
const Material*              material_for_filament_type(const std::string &filament_type);

// Factor (0..1] applied to the stiffness and the strength at the temperature T. Simplified model:
// 1 up to 23 °C, linear down to 0.5 at the maximum service temperature, then a steep drop to 0.02 within 15 °C.
double temperature_factor(const Material &material, double temperature);
// The temperature is above the maximum service temperature of the material.
bool   out_of_temperature(const Material &material, double temperature);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Materials_hpp_
