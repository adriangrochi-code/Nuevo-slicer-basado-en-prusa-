///|/ Tisma Slicer: materials of the structural analysis (phase 5 of docs/IMPLEMENTATION_ROADMAP.md).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/Materials.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r {
namespace Fea {

// Typical values for FDM prints taken from technical data sheets of common brands and from literature on printed
// specimens (tensile tests in XY and Z). They are a starting point to compare options, not design values: the
// documentation (docs/FEA.md) asks to verify them with the data sheet of the filament used.
static const std::vector<Material> s_materials = {
    //  key      name                       E_xy   E_z    nu    S_xy  S_z   Tg     Tmax   linear note, density [g/cm³]
    { "PLA",    "PLA",                      3000., 2600., 0.35, 50.,  30.,  60.,   55.,   true,  "", 1.24 },
    { "PET",    "PETG",                     2000., 1800., 0.38, 45.,  30.,  80.,   70.,   true,  "", 1.27 },
    { "ABS",    "ABS",                      2000., 1700., 0.35, 35.,  20.,  105.,  90.,   true,  "", 1.04 },
    { "ASA",    "ASA",                      2000., 1700., 0.35, 40.,  25.,  100.,  90.,   true,  "", 1.07 },
    { "HIPS",   "HIPS",                     1800., 1500., 0.35, 25.,  15.,  100.,  85.,   true,  "", 1.04 },
    { "PC",     "PC",                       2300., 2000., 0.37, 55.,  35.,  147.,  115.,  true,  "", 1.20 },
    { "PA",     "PA (nylon)",               1500., 1200., 0.39, 45.,  30.,  50.,   90.,   true,  "Semi-crystalline: it keeps stiffness above Tg. Absorbs moisture: properties of a conditioned part.", 1.14 },
    { "PA-CF",  "PA with carbon fiber",     6000., 2500., 0.35, 70.,  35.,  60.,   140.,  true,  "Fibers: much stiffer along the extrusions than across the layers.", 1.18 },
    { "PC-CF",  "PC with carbon fiber",     5000., 2300., 0.35, 65.,  35.,  147.,  120.,  true,  "", 1.22 },
    { "PP",     "PP",                       1000., 800.,  0.40, 25.,  15.,  -10.,  90.,   true,  "Semi-crystalline.", 0.90 },
    { "POM",    "POM (acetal)",             2400., 2000., 0.37, 50.,  25.,  -60.,  110.,  true,  "Semi-crystalline; difficult layer adhesion.", 1.41 },
    { "PEI",    "PEI (ULTEM 9085)",         2200., 2000., 0.36, 70.,  40.,  186.,  150.,  true,  "", 1.27 },
    { "PEEK",   "PEEK",                     3500., 3000., 0.38, 90.,  50.,  143.,  150.,  true,  "Semi-crystalline: depends on the annealing.", 1.30 },
    { "PEKK",   "PEKK",                     3000., 2600., 0.38, 80.,  45.,  160.,  150.,  true,  "", 1.30 },
    { "PSU",    "PSU",                      2400., 2100., 0.37, 60.,  35.,  185.,  170.,  true,  "", 1.24 },
    { "PVDF",   "PVDF",                     1800., 1500., 0.38, 40.,  25.,  -35.,  120.,  true,  "Semi-crystalline.", 1.78 },
    { "FLEX",   "TPU (flexible)",           30.,   25.,   0.45, 30.,  20.,  -40.,  60.,   false, "Elastomer: the linear analysis does not describe it.", 1.21 },
};

static std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

const std::vector<Material>& materials()
{
    return s_materials;
}

const Material* find_material(const std::string &key)
{
    const std::string k = upper(key);
    for (const Material &m : s_materials)
        if (m.key == k)
            return &m;
    return nullptr;
}

const Material* material_for_filament_type(const std::string &filament_type)
{
    std::string t = upper(filament_type);
    if (t == "PETG")
        t = "PET";
    else if (t == "NYLON")
        t = "PA";
    else if (t == "TPU" || t == "TPE")
        t = "FLEX";
    // Profiles of composites often use the base polymer with a suffix ("PA-CF", "PA12-CF", "PETG-CF").
    if (const Material *m = find_material(t))
        return m;
    const bool carbon = t.find("CF") != std::string::npos;
    for (const char *base : { "PEKK", "PEEK", "PEI", "PET", "PA", "PC", "ABS", "ASA", "PLA", "PP" })
        if (t.rfind(base, 0) == 0) {
            if (carbon && (std::string(base) == "PA" || std::string(base) == "PC"))
                return find_material(std::string(base) + "-CF");
            return find_material(base);
        }
    return nullptr;
}

double temperature_factor(const Material &material, double temperature)
{
    const double t_ref = REFERENCE_TEMPERATURE;
    const double t_max = material.max_service_temperature;
    if (temperature <= t_ref || t_max <= t_ref)
        return 1.;
    if (temperature <= t_max)
        return 1. - 0.5 * (temperature - t_ref) / (t_max - t_ref);
    const double drop = std::min(1., (temperature - t_max) / 15.);
    return 0.5 + (0.02 - 0.5) * drop;
}

bool out_of_temperature(const Material &material, double temperature)
{
    return temperature > material.max_service_temperature;
}

} // namespace Fea
} // namespace Slic3r
