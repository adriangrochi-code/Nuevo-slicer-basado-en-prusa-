///|/ Tisma Slicer: projects of Bambu Studio and OrcaSlicer.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BambuProject.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <set>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <expat.h>
#include <LocalesUtils.hpp>

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/miniz_extension.hpp"

namespace Slic3r {

namespace {

const char *MAIN_MODEL_FILE       = "3D/3dmodel.model";
const char *MODEL_SETTINGS_FILE   = "Metadata/model_settings.config";
const char *PROJECT_SETTINGS_FILE = "Metadata/project_settings.config";
// Files of a PrusaSlicer / Tisma project.
const char *PRUSA_PRINT_CONFIG    = "Metadata/Slic3r_PE.config";
const char *PRUSA_MODEL_CONFIG    = "Metadata/Slic3r_PE_model.config";

// Spacing of the plates of Bambu Studio / OrcaSlicer: LOGICAL_PART_PLATE_GAP = 1/5 of the plate (PartPlate.cpp).
constexpr double PLATE_GAP = 1. / 5.;

// --- Archive -------------------------------------------------------------------------------------------------------

class Archive
{
public:
    explicit Archive(const std::string &path)
    {
        mz_zip_zero_struct(&m_zip);
        m_open = open_zip_reader(&m_zip, path);
    }
    ~Archive()
    {
        if (m_open)
            close_zip_reader(&m_zip);
    }
    bool open() const { return m_open; }
    bool has(const std::string &name) { return m_open && locate(name) >= 0; }
    std::optional<std::string> read(const std::string &name)
    {
        const int index = m_open ? locate(name) : -1;
        if (index < 0)
            return std::nullopt;
        size_t size = 0;
        void  *data = mz_zip_reader_extract_to_heap(&m_zip, mz_uint(index), &size, 0);
        if (data == nullptr)
            return std::nullopt;
        std::string out(static_cast<const char*>(data), size);
        mz_free(data);
        return out;
    }

private:
    int locate(std::string name) const
    {
        // Paths of the components start with a slash.
        while (! name.empty() && (name.front() == '/' || name.front() == '\\'))
            name.erase(name.begin());
        return mz_zip_reader_locate_file(const_cast<mz_zip_archive*>(&m_zip), name.c_str(), nullptr, 0);
    }

    mz_zip_archive m_zip;
    bool           m_open { false };
};

// --- Model files (3MF core and production extension) -----------------------------------------------------------------

// The matrices of the 3MF are 4x3, row vectors: m00 m01 m02 m10 m11 m12 m20 m21 m22 m30 m31 m32.
Transform3d parse_transform(const char *text, double unit)
{
    Transform3d out = Transform3d::Identity();
    if (text == nullptr)
        return out;
    std::vector<std::string> items;
    std::string s(text);
    boost::trim(s);
    boost::split(items, s, boost::is_any_of(" \t\n"), boost::token_compress_on);
    if (items.size() != 12)
        return out;
    size_t i = 0;
    for (int c = 0; c < 4; ++ c)
        for (int r = 0; r < 3; ++ r)
            out(r, c) = std::strtod(items[i ++].c_str(), nullptr) * (c == 3 ? unit : 1.);
    return out;
}

double unit_factor(const std::string &unit)
{
    if (unit == "micron")     return 0.001;
    if (unit == "centimeter") return 10.;
    if (unit == "inch")       return 25.4;
    if (unit == "foot")       return 304.8;
    if (unit == "meter")      return 1000.;
    return 1.;
}

struct Component
{
    std::string path;    // empty: the same file
    int         object_id { -1 };
    Transform3d transform { Transform3d::Identity() };
};

struct ObjectData
{
    std::string                  name;
    indexed_triangle_set         its;
    // Painting by triangle (serialized TriangleSelector states), empty when the file has none.
    std::vector<std::string>     paint_color, paint_supports, paint_seam;
    std::vector<Component>       components;
};

struct BuildItem
{
    int         object_id { -1 };
    Transform3d transform { Transform3d::Identity() };
    bool        printable { true };
};

struct ModelFile
{
    std::map<int, ObjectData> objects;
    std::vector<BuildItem>    build;
};

const char* attribute(const char **atts, const char *name)
{
    for (int i = 0; atts[i] != nullptr; i += 2)
        if (std::strcmp(atts[i], name) == 0)
            return atts[i + 1];
    return nullptr;
}

class ModelParser
{
public:
    bool parse(const std::string &text, ModelFile &out, std::string &error)
    {
        m_out = &out;
        XML_Parser parser = XML_ParserCreate(nullptr);
        XML_SetUserData(parser, this);
        XML_SetElementHandler(parser, &ModelParser::start, &ModelParser::end);
        const bool ok = XML_Parse(parser, text.data(), int(text.size()), 1) != XML_STATUS_ERROR;
        if (! ok)
            error = std::string(XML_ErrorString(XML_GetErrorCode(parser))) + " at line " +
                    std::to_string(XML_GetCurrentLineNumber(parser));
        XML_ParserFree(parser);
        return ok;
    }

private:
    static void XMLCALL start(void *user, const char *name, const char **atts)
    {
        auto &self = *static_cast<ModelParser*>(user);
        if (std::strcmp(name, "model") == 0) {
            if (const char *unit = attribute(atts, "unit"))
                self.m_unit = unit_factor(unit);
        } else if (std::strcmp(name, "object") == 0) {
            const char *id = attribute(atts, "id");
            self.m_object = id ? &self.m_out->objects[std::atoi(id)] : nullptr;
            if (self.m_object != nullptr)
                if (const char *n = attribute(atts, "name"))
                    self.m_object->name = n;
        } else if (self.m_object != nullptr && std::strcmp(name, "vertex") == 0) {
            auto coord = [atts, &self](const char *axis) {
                const char *v = attribute(atts, axis);
                return float((v ? std::strtod(v, nullptr) : 0.) * self.m_unit);
            };
            self.m_object->its.vertices.emplace_back(coord("x"), coord("y"), coord("z"));
        } else if (self.m_object != nullptr && std::strcmp(name, "triangle") == 0) {
            auto index = [atts](const char *v) { const char *s = attribute(atts, v); return s ? std::atoi(s) : 0; };
            ObjectData &o = *self.m_object;
            o.its.indices.emplace_back(index("v1"), index("v2"), index("v3"));
            const size_t n = o.its.indices.size();
            auto paint = [atts, n](const char *key, std::vector<std::string> &dst) {
                const char *s = attribute(atts, key);
                if (s != nullptr && *s != 0) {
                    dst.resize(n);
                    dst.back() = s;
                }
            };
            paint("paint_color", o.paint_color);
            paint("paint_supports", o.paint_supports);
            paint("paint_seam", o.paint_seam);
        } else if (self.m_object != nullptr && std::strcmp(name, "component") == 0) {
            Component c;
            if (const char *p = attribute(atts, "p:path"))
                c.path = p;
            if (const char *id = attribute(atts, "objectid"))
                c.object_id = std::atoi(id);
            c.transform = parse_transform(attribute(atts, "transform"), self.m_unit);
            self.m_object->components.push_back(std::move(c));
        } else if (std::strcmp(name, "item") == 0) {
            BuildItem item;
            if (const char *id = attribute(atts, "objectid"))
                item.object_id = std::atoi(id);
            item.transform = parse_transform(attribute(atts, "transform"), self.m_unit);
            if (const char *p = attribute(atts, "printable"))
                item.printable = std::strcmp(p, "0") != 0;
            self.m_out->build.push_back(item);
        }
    }
    static void XMLCALL end(void *user, const char *name)
    {
        auto &self = *static_cast<ModelParser*>(user);
        if (std::strcmp(name, "object") == 0)
            self.m_object = nullptr;
    }

    ModelFile  *m_out    { nullptr };
    ObjectData *m_object { nullptr };
    double      m_unit   { 1. };
};

// --- model_settings.config -------------------------------------------------------------------------------------------

struct PartSettings
{
    std::string                        subtype { "normal_part" };
    std::map<std::string, std::string> metadata;
};

struct ObjectSettings
{
    std::map<std::string, std::string> metadata;
    std::map<int, PartSettings>        parts;
};

struct PlateSettings
{
    int                              id { 0 };
    std::string                      name;
    bool                             locked { false };
    std::vector<std::pair<int, int>> instances;   // object id, instance index
};

struct ModelSettings
{
    std::map<int, ObjectSettings> objects;
    std::vector<PlateSettings>    plates;
};

namespace pt = boost::property_tree;

std::map<std::string, std::string> read_metadata(const pt::ptree &node)
{
    std::map<std::string, std::string> out;
    for (const auto &[tag, child] : node)
        if (tag == "metadata")
            out[child.get<std::string>("<xmlattr>.key", "")] = child.get<std::string>("<xmlattr>.value", "");
    return out;
}

bool parse_model_settings(const std::string &text, ModelSettings &out)
{
    pt::ptree tree;
    try {
        std::istringstream in(text);
        pt::read_xml(in, tree);
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Bambu project: invalid model_settings.config: " << e.what();
        return false;
    }
    for (const auto &[tag, node] : tree.get_child("config", pt::ptree())) {
        if (tag == "object") {
            ObjectSettings &o = out.objects[node.get<int>("<xmlattr>.id", -1)];
            o.metadata = read_metadata(node);
            for (const auto &[ptag, part] : node)
                if (ptag == "part") {
                    PartSettings &p = o.parts[part.get<int>("<xmlattr>.id", -1)];
                    p.subtype  = part.get<std::string>("<xmlattr>.subtype", "normal_part");
                    p.metadata = read_metadata(part);
                }
        } else if (tag == "plate") {
            PlateSettings plate;
            const auto meta = read_metadata(node);
            if (auto it = meta.find("plater_id"); it != meta.end())
                plate.id = std::atoi(it->second.c_str());
            if (auto it = meta.find("plater_name"); it != meta.end())
                plate.name = it->second;
            if (auto it = meta.find("locked"); it != meta.end())
                plate.locked = it->second == "true" || it->second == "1";
            for (const auto &[itag, inst] : node)
                if (itag == "model_instance") {
                    const auto m = read_metadata(inst);
                    auto get = [&m](const char *k) { auto it = m.find(k); return it == m.end() ? -1 : std::atoi(it->second.c_str()); };
                    if (get("object_id") >= 0)
                        plate.instances.emplace_back(get("object_id"), std::max(0, get("instance_id")));
                }
            out.plates.push_back(std::move(plate));
        }
    }
    std::stable_sort(out.plates.begin(), out.plates.end(), [](const PlateSettings &a, const PlateSettings &b) { return a.id < b.id; });
    return true;
}

// --- project_settings.config (JSON) ----------------------------------------------------------------------------------

std::map<std::string, std::vector<std::string>> parse_project_settings(const std::string &text)
{
    std::map<std::string, std::vector<std::string>> out;
    pt::ptree tree;
    try {
        std::istringstream in(text);
        pt::read_json(in, tree);
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Bambu project: invalid project_settings.config: " << e.what();
        return out;
    }
    for (const auto &[key, node] : tree) {
        std::vector<std::string> &values = out[key];
        if (node.empty())
            values.push_back(node.data());
        else
            for (const auto &[k, item] : node)
                values.push_back(item.data());
    }
    return out;
}

// --- Settings: Bambu Studio / OrcaSlicer -> Tisma -----------------------------------------------------------------------
// The same correspondence as tools/profiles/convert_orca_profiles.py (the converter of the printer profiles), for the
// options of the projects.

const std::map<std::string, std::string>& renamed()
{
    static const std::map<std::string, std::string> map {
        // printer
        { "printable_area", "bed_shape" }, { "printable_height", "max_print_height" },
        { "machine_start_gcode", "start_gcode" }, { "machine_end_gcode", "end_gcode" },
        { "before_layer_change_gcode", "before_layer_gcode" }, { "layer_change_gcode", "layer_gcode" },
        { "change_filament_gcode", "toolchange_gcode" }, { "machine_pause_gcode", "pause_print_gcode" },
        { "retraction_length", "retract_length" }, { "retraction_speed", "retract_speed" },
        { "deretraction_speed", "deretract_speed" }, { "z_hop", "retract_lift" },
        { "retraction_minimum_travel", "retract_before_travel" }, { "retract_when_changing_layer", "retract_layer_change" },
        { "extruder_clearance_height_to_rod", "extruder_clearance_height" },
        { "machine_load_filament_time", "filament_load_time" }, { "machine_unload_filament_time", "filament_unload_time" },
        // print
        { "initial_layer_print_height", "first_layer_height" }, { "wall_loops", "perimeters" },
        { "top_shell_layers", "top_solid_layers" }, { "bottom_shell_layers", "bottom_solid_layers" },
        { "top_shell_thickness", "top_solid_min_thickness" }, { "bottom_shell_thickness", "bottom_solid_min_thickness" },
        { "sparse_infill_density", "fill_density" }, { "sparse_infill_pattern", "fill_pattern" },
        { "top_surface_pattern", "top_fill_pattern" }, { "bottom_surface_pattern", "bottom_fill_pattern" },
        { "infill_direction", "fill_angle" }, { "line_width", "extrusion_width" },
        { "outer_wall_line_width", "external_perimeter_extrusion_width" },
        { "inner_wall_line_width", "perimeter_extrusion_width" }, { "sparse_infill_line_width", "infill_extrusion_width" },
        { "internal_solid_infill_line_width", "solid_infill_extrusion_width" },
        { "top_surface_line_width", "top_infill_extrusion_width" },
        { "initial_layer_line_width", "first_layer_extrusion_width" },
        { "support_line_width", "support_material_extrusion_width" },
        { "outer_wall_speed", "external_perimeter_speed" }, { "inner_wall_speed", "perimeter_speed" },
        { "sparse_infill_speed", "infill_speed" }, { "internal_solid_infill_speed", "solid_infill_speed" },
        { "top_surface_speed", "top_solid_infill_speed" }, { "gap_infill_speed", "gap_fill_speed" },
        { "support_speed", "support_material_speed" }, { "support_interface_speed", "support_material_interface_speed" },
        { "initial_layer_speed", "first_layer_speed" }, { "initial_layer_infill_speed", "first_layer_infill_speed" },
        { "initial_layer_travel_speed", "first_layer_travel_speed" },
        { "outer_wall_acceleration", "external_perimeter_acceleration" },
        { "inner_wall_acceleration", "perimeter_acceleration" }, { "sparse_infill_acceleration", "infill_acceleration" },
        { "internal_solid_infill_acceleration", "solid_infill_acceleration" },
        { "top_surface_acceleration", "top_solid_infill_acceleration" },
        { "initial_layer_acceleration", "first_layer_acceleration" },
        { "skirt_loops", "skirts" }, { "brim_object_gap", "brim_separation" },
        { "enable_support", "support_material" }, { "support_threshold_angle", "support_material_threshold" },
        { "support_on_build_plate_only", "support_material_buildplate_only" },
        { "support_top_z_distance", "support_material_contact_distance" },
        { "support_bottom_z_distance", "support_material_bottom_contact_distance" },
        { "support_interface_top_layers", "support_material_interface_layers" },
        { "support_interface_bottom_layers", "support_material_bottom_interface_layers" },
        { "support_base_pattern_spacing", "support_material_spacing" },
        { "support_interface_spacing", "support_material_interface_spacing" },
        { "support_object_xy_distance", "support_material_xy_spacing" }, { "support_angle", "support_material_angle" },
        { "support_filament", "support_material_extruder" },
        { "support_interface_filament", "support_material_interface_extruder" },
        { "detect_thin_wall", "thin_walls" }, { "detect_overhang_wall", "overhangs" },
        { "xy_contour_compensation", "xy_size_compensation" }, { "spiral_mode", "spiral_vase" },
        { "enable_prime_tower", "wipe_tower" }, { "prime_tower_width", "wipe_tower_width" },
        { "prime_tower_brim_width", "wipe_tower_brim_width" }, { "infill_wall_overlap", "infill_overlap" },
        { "bridge_flow", "bridge_flow_ratio" }, { "reduce_crossing_wall", "avoid_crossing_perimeters" },
        { "max_travel_detour_distance", "avoid_crossing_perimeters_max_detour" },
        { "minimum_sparse_infill_area", "solid_infill_below_area" }, { "filename_format", "output_filename_format" },
        { "fuzzy_skin_point_distance", "fuzzy_skin_point_dist" }, { "wall_generator", "perimeter_generator" },
        { "only_one_wall_first_layer", "only_one_perimeter_first_layer" },
        { "seam_slope_start_height", "scarf_seam_start_height" },
        // filament
        { "nozzle_temperature", "temperature" }, { "nozzle_temperature_initial_layer", "first_layer_temperature" },
        { "filament_flow_ratio", "extrusion_multiplier" }, { "fan_min_speed", "min_fan_speed" },
        { "fan_max_speed", "max_fan_speed" }, { "fan_cooling_layer_time", "fan_below_layer_time" },
        { "slow_down_layer_time", "slowdown_below_layer_time" }, { "slow_down_min_speed", "min_print_speed" },
        { "close_fan_the_first_x_layers", "disable_fan_first_layers" }, { "reduce_fan_stop_start_freq", "fan_always_on" },
        { "slow_down_for_layer_cooling", "cooling" }, { "overhang_fan_speed", "bridge_fan_speed" },
        { "filament_start_gcode", "start_filament_gcode" }, { "filament_end_gcode", "end_filament_gcode" },
        { "filament_retraction_length", "filament_retract_length" }, { "filament_z_hop", "filament_retract_lift" },
        { "filament_retraction_speed", "filament_retract_speed" },
        { "filament_deretraction_speed", "filament_deretract_speed" },
        { "filament_retraction_minimum_travel", "filament_retract_before_travel" },
        { "filament_retract_when_changing_layer", "filament_retract_layer_change" },
        { "chamber_temperatures", "chamber_temperature" },
    };
    return map;
}

// Options of the projects which are not copied even when Tisma has one with the same name (other meaning, or handled
// below), and the bookkeeping keys.
const std::set<std::string>& skipped_keys()
{
    static const std::set<std::string> set {
        "name", "type", "from", "inherits", "instantiation", "setting_id", "filament_id", "version", "is_custom_defined",
        "printer_settings_id", "print_settings_id", "filament_settings_id", "printer_model", "printer_variant",
        "compatible_printers", "compatible_prints", "compatible_printers_condition", "compatible_prints_condition",
        "default_print_profile", "default_filament_profile", "upward_compatible_machine", "thumbnails",
        "thumbnails_format", "host_type", "print_host", "print_host_webui", "printhost_apikey", "printhost_cafile",
        "printer_technology", "bed_custom_texture", "bed_custom_model", "inherits_group", "renamed_from", "description",
        "wall_infill_order", "brim_type", "support_type", "support_style", "ironing_type", "print_sequence",
        "exclude_object", "gcode_label_objects", "only_one_wall_top", "infill_combination",
        "ensure_vertical_shell_thickness", "fuzzy_skin", "seam_position", "default_bed_type", "curr_bed_type",
        "post_process",
    };
    return set;
}

const std::map<std::string, std::string> INFILL {
    { "crosshatch", "grid" }, { "zig-zag", "rectilinear" }, { "tri-hexagon", "stars" }, { "monotonicline", "monotoniclines" },
    { "lateral-honeycomb", "honeycomb" }, { "lateral-lattice", "grid" }, { "cross-zag", "rectilinear" },
    { "locked-zag", "rectilinear" }, { "2dhoneycomb", "honeycomb" }, { "2dlattice", "grid" }, { "tpmsd", "gyroid" },
    { "tpmsfk", "gyroid" }, { "quartercubic", "cubic" },
};
const std::map<std::string, std::string> SEAM { { "back", "rear" }, { "aligned_back", "aligned" } };
// Line widths: % of the nozzle in Bambu Studio / OrcaSlicer -> mm.
const std::set<std::string> WIDTHS { "extrusion_width", "external_perimeter_extrusion_width", "perimeter_extrusion_width",
    "infill_extrusion_width", "solid_infill_extrusion_width", "top_infill_extrusion_width", "first_layer_extrusion_width",
    "support_material_extrusion_width" };
// Accelerations: % of the default acceleration -> mm/s².
const std::set<std::string> ACCELERATIONS { "external_perimeter_acceleration", "perimeter_acceleration",
    "infill_acceleration", "solid_infill_acceleration", "top_solid_infill_acceleration", "first_layer_acceleration",
    "travel_acceleration", "bridge_acceleration" };
// Bed temperatures of the filaments, one pair per plate type.
const std::vector<std::pair<std::string, std::string>> PLATE_TEMPS {
    { "High Temp Plate", "hot_plate_temp" }, { "Textured PEI Plate", "textured_plate_temp" },
    { "Engineering Plate", "eng_plate_temp" }, { "Cool Plate", "cool_plate_temp" },
    { "Textured Cool Plate", "textured_cool_plate_temp" }, { "Supertack Plate", "supertack_plate_temp" },
};

double to_double(const std::string &s, double fallback = 0.)
{
    std::string t = boost::trim_copy(s);
    if (! t.empty() && t.back() == '%')
        t.pop_back();
    char *end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    return end == t.c_str() ? fallback : v;
}

bool is_true(const std::string &s) { return s == "1" || boost::iequals(s, "true"); }

const std::string& first(const std::vector<std::string> &v)
{
    static const std::string empty;
    return v.empty() ? empty : v.front();
}

// Serializes the values for a Tisma option; nullopt when they cannot be represented.
std::optional<std::string> serialize(const std::string &key, std::vector<std::string> values)
{
    const ConfigOptionDef *def = print_config_def.get(key);
    if (def == nullptr || values.empty())
        return std::nullopt;
    // Some vectors come as one comma separated string.
    if (values.size() == 1 && (def->type & coVectorType) != 0 && def->type != coStrings && values.front().find(',') != std::string::npos) {
        std::vector<std::string> split;
        boost::split(split, values.front(), boost::is_any_of(","));
        values = split;
    }
    for (std::string &v : values)
        boost::trim(v);
    switch (def->type) {
    case coBool:
        return is_true(values.front()) ? "1" : "0";
    case coBools: {
        std::string out;
        for (const std::string &v : values)
            out += (out.empty() ? "" : ",") + std::string(is_true(v) ? "1" : "0");
        return out;
    }
    case coEnum: {
        std::string v = values.front();
        if (key == "fill_pattern" || key == "top_fill_pattern" || key == "bottom_fill_pattern")
            if (auto it = INFILL.find(v); it != INFILL.end())
                v = it->second;
        if (! def->enum_def || ! def->enum_def->value_to_index(v))
            return std::nullopt;
        return v;
    }
    case coString:
        return values.front();
    case coStrings: {
        ConfigOptionStrings opt(values);
        return opt.serialize();
    }
    case coPercent: {
        std::string v = values.front();
        if (! v.empty() && v.back() != '%')
            v += '%';
        return v;
    }
    case coPoints: {
        // "0x0","256x0",...
        std::string out;
        for (const std::string &v : values)
            out += (out.empty() ? "" : ",") + v;
        return out;
    }
    default:
        break;
    }
    if ((def->type & coVectorType) != 0) {
        std::string out;
        for (const std::string &v : values)
            out += (out.empty() ? "" : ",") + v;
        return out;
    }
    return values.front();
}

// Sets a serialized value if Tisma accepts it.
bool set_value(DynamicPrintConfig &out, const std::string &key, const std::string &value)
{
    try {
        ConfigSubstitutionContext ctxt { ForwardCompatibilitySubstitutionRule::Disable };
        DynamicPrintConfig tmp;
        tmp.set_deserialize(key, value, ctxt);
        // Within the limits of the option.
        if (const ConfigOptionDef *def = print_config_def.get(key); def != nullptr && (def->type & coVectorType) == 0) {
            if (const ConfigOption *opt = tmp.option(key); opt != nullptr && opt->is_scalar() &&
                (def->type == coFloat || def->type == coInt || def->type == coPercent)) {
                const double v = def->type == coInt ? double(opt->getInt()) : opt->getFloat();
                if (v < def->min || v > def->max)
                    return false;
            }
        }
        out.set_key_value(key, tmp.option(key)->clone());
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

} // namespace

void translate_bambu_settings(const std::map<std::string, std::vector<std::string>> &settings, DynamicPrintConfig &out,
                              bool printer, std::vector<std::string> *skipped)
{
    std::set<std::string> allowed;
    for (const std::string &k : Preset::print_options())
        allowed.insert(k);
    for (const std::string &k : Preset::filament_options())
        allowed.insert(k);
    if (printer)
        for (const std::string &k : Preset::printer_options())
            allowed.insert(k);
    auto get = [&settings](const std::string &k) -> const std::vector<std::string>* {
        auto it = settings.find(k);
        return it == settings.end() ? nullptr : &it->second;
    };
    auto skip = [skipped](const std::string &k) { if (skipped) skipped->push_back(k); };

    // A renamed option wins over an option with the same name in Tisma.
    std::set<std::string> renamed_targets;
    for (const auto &[orca_key, values] : settings)
        if (auto it = renamed().find(orca_key); it != renamed().end())
            renamed_targets.insert(it->second);
    for (const auto &[orca_key, values] : settings) {
        if (skipped_keys().count(orca_key))
            continue;
        auto renamed_it = renamed().find(orca_key);
        const std::string key = renamed_it == renamed().end() ? orca_key : renamed_it->second;
        if (renamed_it == renamed().end() && renamed_targets.count(key))
            continue;
        // The G-code of Bambu Studio / OrcaSlicer uses their own placeholders: not loaded.
        if (allowed.count(key) == 0 || boost::algorithm::ends_with(key, "_gcode")) {
            skip(orca_key);
            continue;
        }
        std::optional<std::string> value = serialize(key, values);
        if (value && WIDTHS.count(key) && ! value->empty() && value->back() == '%') {
            const std::vector<std::string> *nozzle = get("nozzle_diameter");
            const double d = nozzle ? to_double(first(*nozzle), 0.4) : 0.4;
            char buf[32];
            snprintf(buf, sizeof(buf), "%.3g", to_double(*value) * 0.01 * d);
            value = buf;
        }
        if (value && ACCELERATIONS.count(key) && ! value->empty() && value->back() == '%') {
            const std::vector<std::string> *base = get("default_acceleration");
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0f", to_double(*value) * 0.01 * (base ? to_double(first(*base)) : 0.));
            value = buf;
        }
        if (! value || ! set_value(out, key, *value))
            skip(orca_key);
    }

    // Options that change meaning.
    auto value_of = [&get](const std::string &k) { const auto *v = get(k); return v ? first(*v) : std::string(); };
    if (const std::string order = value_of("wall_infill_order"); ! order.empty())
        set_value(out, "external_perimeters_first",
                  boost::starts_with(order, "outer wall") || boost::starts_with(order, "infill/outer") ? "1" : "0");
    if (const std::string brim = value_of("brim_type"); ! brim.empty()) {
        if (brim == "no_brim")
            set_value(out, "brim_width", "0");
        else if (brim == "outer_only" || brim == "inner_only" || brim == "outer_and_inner")
            set_value(out, "brim_type", brim);
        else if (brim == "auto_brim" || brim == "brim_ears")
            set_value(out, "brim_type", "outer_only");
    }
    if (const std::string st = value_of("support_type"); ! st.empty()) {
        set_value(out, "support_material_auto", st.find("manual") != std::string::npos ? "0" : "1");
        if (boost::starts_with(st, "tree"))
            set_value(out, "support_material_style", "organic");
    }
    if (const std::string style = value_of("support_style"); style == "grid" || style == "snug")
        set_value(out, "support_material_style", style);
    if (const std::string it = value_of("ironing_type"); ! it.empty()) {
        set_value(out, "ironing", it == "no ironing" ? "0" : "1");
        if (it == "top" || it == "topmost" || it == "solid")
            set_value(out, "ironing_type", it);
    }
    if (value_of("print_sequence") == "by object")
        set_value(out, "complete_objects", "1");
    if (const auto *v = get("only_one_wall_top"))
        set_value(out, "top_one_perimeter_type", is_true(first(*v)) ? "top" : "none");
    if (is_true(value_of("infill_combination")))
        set_value(out, "infill_every_layers", "2");
    if (const std::string fz = value_of("fuzzy_skin"); ! fz.empty())
        set_value(out, "fuzzy_skin", boost::starts_with(fz, "all") ? "all" : (fz == "external" || fz == "outer" ? "external" : "none"));
    if (const std::string seam = value_of("seam_position"); ! seam.empty()) {
        auto it = SEAM.find(seam);
        const std::string v = it == SEAM.end() ? seam : it->second;
        set_value(out, "seam_position", (v == "random" || v == "nearest" || v == "aligned" || v == "rear") ? v : "aligned");
    }
    // Bed temperatures: those of the plate type of the project (one value per filament).
    std::string plate = value_of("curr_bed_type");
    for (const auto &[plate_name, temp_key] : PLATE_TEMPS)
        if ((plate.empty() || plate == plate_name) && get(temp_key)) {
            if (auto v = serialize("bed_temperature", *get(temp_key)))
                set_value(out, "bed_temperature", *v);
            if (const auto *first_layer = get(temp_key + "_initial_layer"))
                if (auto v = serialize("first_layer_bed_temperature", *first_layer))
                    set_value(out, "first_layer_bed_temperature", *v);
            break;
        }
}

void translate_bambu_object_settings(const std::map<std::string, std::string> &settings, ModelConfig &out)
{
    static const std::set<std::string> not_settings { "name", "module", "matrix", "source_file", "source_object_id",
        "source_volume_id", "source_offset_x", "source_offset_y", "source_offset_z", "source_in_inches",
        "source_in_meters", "precise_seam_type" };
    std::map<std::string, std::vector<std::string>> values;
    for (const auto &[k, v] : settings)
        if (not_settings.count(k) == 0)
            values[k] = { v };
    DynamicPrintConfig translated;
    translate_bambu_settings(values, translated, false);
    // The extruder of the object / part (0 = the default one).
    if (auto it = settings.find("extruder"); it != settings.end())
        translated.set_key_value("extruder", new ConfigOptionInt(std::max(0, std::atoi(it->second.c_str()))));
    static const PrintObjectConfig object_config;
    static const PrintRegionConfig region_config;
    for (const std::string &key : translated.keys())
        if (key == "extruder" || object_config.has(key) || region_config.has(key))
            out.set_key_value(key, translated.option(key)->clone());
}

bool is_bambu_project_3mf(const std::string &path)
{
    Archive archive(path);
    return archive.open() && (archive.has(MODEL_SETTINGS_FILE) || archive.has(PROJECT_SETTINGS_FILE)) &&
           ! archive.has(PRUSA_PRINT_CONFIG) && ! archive.has(PRUSA_MODEL_CONFIG);
}

bool load_bambu_project_3mf(const std::string &path, Model &model, DynamicPrintConfig &config, BambuProjectInfo &info,
                            std::string &error)
{
    CNumericLocalesSetter locales;
    Archive archive(path);
    if (! archive.open()) {
        error = "Unable to open the file";
        return false;
    }
    // Model files, the main one and those of the components.
    std::map<std::string, ModelFile> files;
    auto load_file = [&archive, &files, &error](std::string name) -> const ModelFile* {
        while (! name.empty() && name.front() == '/')
            name.erase(name.begin());
        if (auto it = files.find(name); it != files.end())
            return &it->second;
        const std::optional<std::string> text = archive.read(name);
        if (! text) {
            error = "Missing file " + name;
            return nullptr;
        }
        ModelFile file;
        std::string parse_error;
        if (! ModelParser().parse(*text, file, parse_error)) {
            error = name + ": " + parse_error;
            return nullptr;
        }
        return &files.emplace(name, std::move(file)).first->second;
    };
    const ModelFile *main = load_file(MAIN_MODEL_FILE);
    if (main == nullptr)
        return false;

    ModelSettings settings;
    if (auto text = archive.read(MODEL_SETTINGS_FILE))
        parse_model_settings(*text, settings);
    std::map<std::string, std::vector<std::string>> project;
    if (auto text = archive.read(PROJECT_SETTINGS_FILE))
        project = parse_project_settings(*text);

    const std::string stem = boost::filesystem::path(path).stem().string();
    auto volume_type = [](const std::string &subtype) -> std::optional<ModelVolumeType> {
        if (subtype == "normal_part")      return ModelVolumeType::MODEL_PART;
        if (subtype == "modifier_part")    return ModelVolumeType::PARAMETER_MODIFIER;
        if (subtype == "negative_part")    return ModelVolumeType::NEGATIVE_VOLUME;
        if (subtype == "support_enforcer") return ModelVolumeType::SUPPORT_ENFORCER;
        if (subtype == "support_blocker")  return ModelVolumeType::SUPPORT_BLOCKER;
        return std::nullopt;   // helpers of OrcaSlicer without equivalent (precise seam)
    };

    // Objects, created at their first build item.
    std::map<int, ModelObject*> objects;
    // (object id, instance index) -> instance
    std::map<std::pair<int, int>, ModelInstance*> instances;
    for (const BuildItem &item : main->build) {
        auto obj_it = main->objects.find(item.object_id);
        if (obj_it == main->objects.end())
            continue;
        const ObjectData &object = obj_it->second;
        ModelObject *&mo = objects[item.object_id];
        if (mo == nullptr) {
            const ObjectSettings *os = nullptr;
            if (auto it = settings.objects.find(item.object_id); it != settings.objects.end())
                os = &it->second;
            mo = model.add_object();
            if (os != nullptr)
                if (auto it = os->metadata.find("name"); it != os->metadata.end())
                    mo->name = it->second;
            if (mo->name.empty())
                mo->name = object.name.empty() ? stem + "_" + std::to_string(model.objects.size()) : object.name;
            if (os != nullptr)
                translate_bambu_object_settings(os->metadata, mo->config);
            // The parts: one per component (or the mesh of the object itself).
            std::vector<std::pair<const ObjectData*, Component>> parts;
            if (object.components.empty()) {
                Component self;
                self.object_id = item.object_id;
                parts.emplace_back(&object, self);
            } else {
                for (const Component &c : object.components) {
                    const ModelFile *file = c.path.empty() ? main : load_file(c.path);
                    if (file == nullptr)
                        return false;
                    auto sub = file->objects.find(c.object_id);
                    if (sub == file->objects.end() || sub->second.its.indices.empty()) {
                        BOOST_LOG_TRIVIAL(warning) << "Bambu project: missing mesh " << c.path << " #" << c.object_id;
                        continue;
                    }
                    parts.emplace_back(&sub->second, c);
                }
            }
            for (const auto &[data, component] : parts) {
                const PartSettings *ps = nullptr;
                if (os != nullptr)
                    if (auto it = os->parts.find(component.object_id); it != os->parts.end())
                        ps = &it->second;
                const std::optional<ModelVolumeType> type = volume_type(ps ? ps->subtype : std::string("normal_part"));
                if (! type) {
                    BOOST_LOG_TRIVIAL(info) << "Bambu project: part type " << ps->subtype << " not loaded";
                    continue;
                }
                TriangleMesh mesh(data->its);
                if (mesh.volume() < 0)
                    mesh.flip_triangles();
                ModelVolume *volume = mo->add_volume(std::move(mesh), *type);
                volume->set_transformation(Geometry::Transformation(component.transform));
                if (ps != nullptr) {
                    if (auto it = ps->metadata.find("name"); it != ps->metadata.end())
                        volume->name = it->second;
                    translate_bambu_object_settings(ps->metadata, volume->config);
                }
                if (volume->name.empty())
                    volume->name = mo->name;
                // Painting.
                const size_t n = data->its.indices.size();
                auto paint = [n](const std::vector<std::string> &src, FacetsAnnotation &dst) {
                    if (src.empty())
                        return;
                    dst.reserve(int(n));
                    for (size_t i = 0; i < std::min(n, src.size()); ++ i)
                        if (! src[i].empty())
                            dst.set_triangle_from_string(int(i), src[i]);
                    dst.shrink_to_fit();
                };
                paint(data->paint_color, volume->mm_segmentation_facets);
                paint(data->paint_supports, volume->supported_facets);
                paint(data->paint_seam, volume->seam_facets);
            }
            if (std::none_of(mo->volumes.begin(), mo->volumes.end(), [](const ModelVolume *v) { return v->is_model_part(); })) {
                model.delete_object(mo);
                mo = nullptr;
                objects.erase(item.object_id);
                continue;
            }
        }
        const int index = int(mo->instances.size());
        ModelInstance *instance = mo->add_instance(Geometry::Transformation(item.transform));
        instance->printable = item.printable;
        instances[{ item.object_id, index }] = instance;
    }
    if (model.objects.empty()) {
        error = "The project has no objects";
        return false;
    }
    for (ModelObject *mo : model.objects)
        mo->invalidate_bounding_box();

    // Plates: the instances move from the plate of the project to the bed of Tisma with the same index, keeping their
    // place relative to the center of the bed.
    info.plates = int(settings.plates.size());
    BoundingBoxf bambu_bed;
    if (auto it = project.find("printable_area"); it != project.end())
        for (const std::string &p : it->second) {
            std::vector<std::string> xy;
            boost::split(xy, p, boost::is_any_of("x"));
            if (xy.size() == 2)
                bambu_bed.merge(Vec2d(to_double(xy[0]), to_double(xy[1])));
        }
    const BoundingBoxf tisma_bed = s_multiple_beds.get_build_volume_box();
    const bool move = bambu_bed.defined && tisma_bed.defined && tisma_bed.size().x() > 0. && ! settings.plates.empty();
    const int cols = [n = int(settings.plates.size())]() {
        // compute_colum_count() of OrcaSlicer: ceil(sqrt(n)) for n plates.
        const float value = std::sqrt(float(std::max(1, n)));
        const float rounded = std::round(value);
        return value > rounded ? int(rounded) + 1 : int(rounded);
    }();
    for (size_t p = 0; p < settings.plates.size(); ++ p) {
        const PlateSettings &plate = settings.plates[p];
        int bed = int(p);
        if (bed >= int(MAX_NUMBER_OF_BEDS)) {
            info.instances_off_beds += int(plate.instances.size());
            bed = int(MAX_NUMBER_OF_BEDS) - 1;
        }
        if (bed == int(p)) {
            model.plates[bed].name   = plate.name;
            model.plates[bed].locked = plate.locked;
        }
        if (! move)
            continue;
        const Vec2d bambu_origin(double(int(p) % cols) * bambu_bed.size().x() * (1. + PLATE_GAP),
                                 - double(int(p) / cols) * bambu_bed.size().y() * (1. + PLATE_GAP));
        const Vec2d from = bambu_origin + bambu_bed.center();
        const Vec2d to   = tisma_bed.center() + s_multiple_beds.get_bed_translation(bed).head<2>();
        for (const auto &[object_id, instance_idx] : plate.instances)
            if (auto it = instances.find({ object_id, instance_idx }); it != instances.end()) {
                Vec3d offset = it->second->get_offset();
                offset.head<2>() += to - from;
                it->second->set_offset(offset);
            }
    }

    // The print and filament settings of the project (the printer stays the one selected in Tisma).
    translate_bambu_settings(project, config, false, &info.skipped_settings);
    return true;
}

} // namespace Slic3r
