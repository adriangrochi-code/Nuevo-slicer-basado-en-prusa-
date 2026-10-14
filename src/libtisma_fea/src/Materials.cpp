///|/ Tisma Slicer: materials of the structural analysis (phase 5 of docs/IMPLEMENTATION_ROADMAP.md).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/Materials.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace Slic3r {
namespace Fea {

// Typical values for FDM prints taken from technical data sheets of common brands and from literature on printed
// specimens (tensile tests in XY and Z). They are a starting point to compare options, not design values: the
// documentation (docs/FEA.md) asks to verify them with the data sheet of the filament used.
static const std::vector<Material> s_materials = {
    //  key      name                       E_xy   E_z    nu    S_xy  S_z   Tg     Tmax   linear note, density [g/cm³]
    { "PLA",    "PLA",                      3000., 2600., 0.35, 50.,  30.,  60.,   55.,   true,  "", 1.24, 190., 220. },
    { "PET",    "PETG",                     2000., 1800., 0.38, 45.,  30.,  80.,   70.,   true,  "", 1.27, 220., 250. },
    { "ABS",    "ABS",                      2000., 1700., 0.35, 35.,  20.,  105.,  90.,   true,  "", 1.04, 230., 260. },
    { "ASA",    "ASA",                      2000., 1700., 0.35, 40.,  25.,  100.,  90.,   true,  "", 1.07, 240., 260. },
    { "HIPS",   "HIPS",                     1800., 1500., 0.35, 25.,  15.,  100.,  85.,   true,  "", 1.04, 220., 250. },
    { "PC",     "PC",                       2300., 2000., 0.37, 55.,  35.,  147.,  115.,  true,  "", 1.20, 260., 300. },
    { "PA",     "PA (nylon)",               1500., 1200., 0.39, 45.,  30.,  50.,   90.,   true,  "Semi-crystalline: it keeps stiffness above Tg. Absorbs moisture: properties of a conditioned part.", 1.14, 240., 280. },
    { "PA-CF",  "PA with carbon fiber",     6000., 2500., 0.35, 70.,  35.,  60.,   140.,  true,  "Fibers: much stiffer along the extrusions than across the layers.", 1.18, 260., 290. },
    { "PC-CF",  "PC with carbon fiber",     5000., 2300., 0.35, 65.,  35.,  147.,  120.,  true,  "", 1.22, 270., 300. },
    { "PP",     "PP",                       1000., 800.,  0.40, 25.,  15.,  -10.,  90.,   true,  "Semi-crystalline.", 0.90, 220., 250. },
    { "POM",    "POM (acetal)",             2400., 2000., 0.37, 50.,  25.,  -60.,  110.,  true,  "Semi-crystalline; difficult layer adhesion.", 1.41, 210., 230. },
    { "PEI",    "PEI (ULTEM 9085)",         2200., 2000., 0.36, 70.,  40.,  186.,  150.,  true,  "", 1.27, 350., 390. },
    { "PEEK",   "PEEK",                     3500., 3000., 0.38, 90.,  50.,  143.,  150.,  true,  "Semi-crystalline: depends on the annealing.", 1.30, 370., 410. },
    { "PEKK",   "PEKK",                     3000., 2600., 0.38, 80.,  45.,  160.,  150.,  true,  "", 1.30, 340., 380. },
    { "PSU",    "PSU",                      2400., 2100., 0.37, 60.,  35.,  185.,  170.,  true,  "", 1.24, 340., 380. },
    { "PVDF",   "PVDF",                     1800., 1500., 0.38, 40.,  25.,  -35.,  120.,  true,  "Semi-crystalline.", 1.78, 240., 260. },
    { "FLEX",   "TPU (flexible)",           30.,   25.,   0.45, 30.,  20.,  -40.,  60.,   false, "Elastomer: the linear analysis does not describe it.", 1.21, 210., 240. },
    // Resins (SLA / MSLA), from the data sheets of the makers; resin prints are close to isotropic (the same values
    // across the layers). Glass transition: the heat deflection temperature at 1.82 MPa; maximum service: at 0.45 MPa.
    // The data sheets give no density: 1.15, typical of cured photopolymers. No nozzle: no print temperatures.
    { "RESIN",  "Standard resin",           709.,  709.,  0.35, 33.8, 33.8, 50.,   50.,   true,  "Elegoo Standard resin data sheet, post-cured: tensile strength 33.8 MPa, flexural modulus 709 MPa "
                                                                                              "(no tensile modulus nor heat deflection: 50 °C assumed). Density not in the sheet.", 1.15, 0., 0. },
    { "RESIN-FL", "Formlabs Standard resin", 2800., 2800., 0.35, 65., 65.,  58.4,  73.1,  true,  "Formlabs Clear / White / Grey / Black (FLGPCL03), post-cured 60 min at 60 °C: 65 MPa, 2.8 GPa, "
                                                                                              "heat deflection 58.4 °C at 1.82 MPa. Not post-cured: 38 MPa, 1.6 GPa. Density not in the sheet.", 1.15, 0., 0. },
    { "RESIN-TOUGH", "Tough resin (Formlabs Tough 2000)", 1800., 1800., 0.35, 40.4, 40.4, 57., 70., true, "Formlabs Tough 2000 V2, post-cured: 40.4 MPa, 1.8 GPa, elongation 79 %, heat deflection "
                                                                                              "57 °C at 1.8 MPa, 70 °C at 0.45 MPa. Density not in the sheet.", 1.15, 0., 0. },
};

static std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

// The table with the changes of the user (s_table) and the changes themselves (s_user). Written only by
// set_user_materials(); s_mutex guards the writes and material_by_key().
static std::mutex            s_mutex;
static std::vector<Material> s_user;
static std::vector<Material> s_table = s_materials;

const std::vector<Material>& materials()
{
    return s_table;
}

const std::vector<Material>& builtin_materials()
{
    return s_materials;
}

const Material* find_material(const std::string &key)
{
    const std::string k = upper(key);
    for (const Material &m : s_table)
        if (m.key == k)
            return &m;
    return nullptr;
}

std::optional<Material> material_by_key(const std::string &key)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (const Material *m = find_material(key))
        return *m;
    return std::nullopt;
}

void set_user_materials(const std::vector<Material> &user)
{
    std::vector<Material> clean;
    for (Material m : user) {
        m.key  = upper(m.key);
        m.user = true;
        if (m.key.empty() || std::any_of(clean.begin(), clean.end(), [&m](const Material &o) { return o.key == m.key; }))
            continue;
        if (m.name.empty())
            m.name = m.key;
        clean.push_back(std::move(m));
    }
    std::vector<Material> table = s_materials;
    for (const Material &m : clean) {
        auto it = std::find_if(table.begin(), table.end(), [&m](const Material &o) { return o.key == m.key; });
        if (it != table.end())
            *it = m;
        else
            table.push_back(m);
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    s_user  = std::move(clean);
    s_table = std::move(table);
}

std::vector<Material> user_materials()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_user;
}

bool validate_material(const Material &m, std::string &error)
{
    if (upper(m.key).empty())
        error = "The key is empty";
    else if (m.E_xy <= 0. || m.E_z <= 0.)
        error = "The Young's moduli must be positive";
    else if (m.poisson <= 0. || m.poisson >= 0.5)
        error = "The Poisson ratio must be between 0 and 0.5";
    else if (m.strength_xy <= 0. || m.strength_z <= 0.)
        error = "The strengths must be positive";
    else if (m.density <= 0.)
        error = "The density must be positive";
    else if (m.max_service_temperature <= REFERENCE_TEMPERATURE)
        error = "The maximum service temperature must be above 23 °C";
    else if (m.print_temperature_max < m.print_temperature_min)
        error = "The print temperature range is reversed";
    else
        return true;
    return false;
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

const Material* material_for_resin(const std::string &resin_name)
{
    const std::string t = upper(resin_name);
    return find_material(t.find("TOUGH") != std::string::npos || t.find("ABS") != std::string::npos ? "RESIN-TOUGH" : "RESIN");
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

double layer_adhesion_factor(const Material &material, double print_temperature)
{
    const double t_min = material.print_temperature_min, t_max = material.print_temperature_max;
    if (print_temperature <= 0. || t_max <= t_min)
        return 1.;
    const double t_mid = 0.5 * (t_min + t_max);
    if (print_temperature >= t_max)
        return 1.15;
    if (print_temperature >= t_mid)
        return 1. + 0.15 * (print_temperature - t_mid) / (t_max - t_mid);
    if (print_temperature >= t_min)
        return 0.8 + 0.2 * (print_temperature - t_min) / (t_mid - t_min);
    return std::max(0.5, 0.8 - 0.3 * (t_min - print_temperature) / 20.);
}

} // namespace Fea
} // namespace Slic3r
