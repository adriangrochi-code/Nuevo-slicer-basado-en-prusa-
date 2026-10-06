///|/ Tisma Slicer: analysis of an object of the model with its EngineeringSetup (phase 5).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/ModelSetup.hpp"

#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/BeltPrinter.hpp>

#include "tisma_fea/Structures.hpp"

namespace Slic3r {
namespace Fea {

const char *INFILL_ZONE_NAME   = "Tisma infill zone";
const char *REINFORCEMENT_NAME = "Tisma reinforcement";
const char *LATTICE_NAME       = "Tisma lattice";

static bool starts_with(const std::string &s, const char *prefix) { return s.rfind(prefix, 0) == 0; }

// Width of the perimeters [mm] of a print configuration (with the overrides of the object applied). As in
// PrusaSlicer (Flow::extrusion_width), a width in percent is relative to the layer height, 0 = automatic
// (1.125 x nozzle).
static double perimeter_width(const DynamicPrintConfig &cfg)
{
    double nozzle = 0.4;
    if (const ConfigOptionFloats *n = cfg.option<ConfigOptionFloats>("nozzle_diameter"); n && ! n->values.empty())
        nozzle = n->values.front();
    double layer_height = 0.2;
    if (const ConfigOption *lh = cfg.option("layer_height"); lh && lh->getFloat() > 0.)
        layer_height = lh->getFloat();
    auto width = [&](const char *key) {
        const ConfigOptionFloatOrPercent *opt = cfg.option<ConfigOptionFloatOrPercent>(key);
        if (opt == nullptr || opt->value <= 0.)
            return 0.;
        return opt->percent ? opt->value * 0.01 * layer_height : opt->value;
    };
    double w = width("perimeter_extrusion_width");
    if (w <= 0.)
        w = width("extrusion_width");
    if (w <= 0.)
        w = 1.125 * nozzle;
    return w;
}

double line_width(const ModelObject &object, const DynamicPrintConfig &print_config)
{
    DynamicPrintConfig cfg = print_config;
    cfg.apply(object.config.get(), true);
    return perimeter_width(cfg);
}

// Walls and infill of the object from the print configuration with the overrides of the object.
static void infill_from_config(const ModelObject &object, const DynamicPrintConfig &print_config, InfillModel &infill)
{
    DynamicPrintConfig cfg = print_config;
    cfg.apply(object.config.get(), true);
    auto number = [&](const char *key, double def) {
        const ConfigOption *opt = cfg.option(key);
        return opt ? opt->getFloat() : def;
    };
    const double layer_height = number("layer_height", 0.2);
    const double w = perimeter_width(cfg);
    const ConfigOption *perimeters = cfg.option("perimeters");
    const ConfigOption *top        = cfg.option("top_solid_layers");
    const ConfigOption *bottom     = cfg.option("bottom_solid_layers");
    infill.enabled              = true;
    infill.perimeters           = perimeters ? perimeters->getInt() : 2;
    infill.perimeter_width      = w;
    infill.wall_thickness       = infill.perimeters * w;
    infill.top_bottom_thickness = std::max(top ? top->getInt() : 3, bottom ? bottom->getInt() : 3) * layer_height;
    const ConfigOptionPercent *density = cfg.option<ConfigOptionPercent>("fill_density");
    infill.density = density ? std::clamp(density->value * 0.01, 0., 1.) : 0.2;
    std::tie(infill.stiffness_exponent, infill.strength_exponent) = infill_exponents(cfg.opt_serialize("fill_pattern"));
}

bool build_analysis_input(const ModelObject &object, size_t instance_idx, const std::string &filament_type,
                          ModelAnalysisInput &out, std::string &error, const DynamicPrintConfig *print_config)
{
    if (instance_idx >= object.instances.size()) {
        error = "The object has no instance";
        return false;
    }
    const EngineeringSetup &eng = object.engineering;

    out.material = eng.material.empty() ? std::string() : eng.material;
    if (out.material.empty()) {
        const Material *m = material_for_filament_type(filament_type);
        if (! m) {
            error = "There is no material of the table for the filament type " + filament_type + ": choose one";
            return false;
        }
        out.material = m->key;
    } else if (! find_material(out.material)) {
        error = "Unknown material " + out.material;
        return false;
    }

    // Printable parts in print coordinates; the offset of the triangles of each volume in the joined mesh.
    const Transform3d instance_trafo = object.instances[instance_idx]->get_matrix();
    std::vector<int>       triangle_offset(object.volumes.size(), -1);
    std::vector<Transform3d> volume_trafo(object.volumes.size(), Transform3d::Identity());
    out.mesh.clear();
    for (size_t v = 0; v < object.volumes.size(); ++ v) {
        const ModelVolume &volume = *object.volumes[v];
        if (! volume.is_model_part())
            continue;
        volume_trafo[v]    = instance_trafo * volume.get_matrix();
        triangle_offset[v] = int(out.mesh.indices.size());
        indexed_triangle_set its = volume.mesh().its;
        its_transform(its, volume_trafo[v]);
        // Mirrored volumes: keep the triangles facing out (the order of the triangles does not change).
        if (volume_trafo[v].matrix().block<3, 3>(0, 0).determinant() < 0.)
            its_flip_triangles(its);
        its_merge(out.mesh, its);
    }
    if (out.mesh.indices.empty()) {
        error = "The object has no printable parts";
        return false;
    }

    // Walls, infill and infill modifiers.
    InfillModel infill;
    if (print_config) {
        infill_from_config(object, *print_config, infill);
        for (size_t v = 0; v < object.volumes.size(); ++ v) {
            const ModelVolume &volume = *object.volumes[v];
            if (! volume.is_modifier())
                continue;
            const ConfigOptionPercent *d = volume.config.get().option<ConfigOptionPercent>("fill_density");
            const ConfigOption        *p = volume.config.get().option("perimeters");
            if (d == nullptr && p == nullptr)
                continue;
            InfillZone zone;
            zone.mesh = volume.mesh().its;
            its_transform(zone.mesh, instance_trafo * volume.get_matrix());
            if (zone.mesh.indices.empty())
                continue;
            zone.density = d ? std::clamp(d->value * 0.01, 0., 1.) : infill.density;
            if (p)
                zone.wall_thickness = p->getInt() * infill.perimeter_width;
            if (starts_with(volume.name, LATTICE_NAME)) {
                // Struts of a lattice: homogenized over its cells (edge in the name, "Tisma lattice 8 mm").
                double cell = 8.;
                try { cell = std::stod(volume.name.substr(std::string(LATTICE_NAME).size())); } catch (...) {}
                zone.volume_fraction    = true;
                zone.homogenize_cell    = cell;
                zone.stiffness_exponent = LATTICE_EXPONENT;
                zone.strength_exponent  = LATTICE_EXPONENT;
                zone.density            = 1.;
            } else if (d && d->value >= 100.) {
                // Solid modifiers: material where the mesh is.
                zone.volume_fraction = true;
            }
            infill.zones.emplace_back(std::move(zone));
        }
    }

    auto region_triangles = [&](const EngineeringRegion &region, std::vector<int> &triangles) {
        if (region.volume < 0 || region.volume >= int(object.volumes.size()) || triangle_offset[region.volume] < 0)
            return false;
        const int n = int(object.volumes[region.volume]->mesh().its.indices.size());
        for (int t : region.triangles)
            if (t >= 0 && t < n)
                triangles.push_back(triangle_offset[region.volume] + t);
        return ! triangles.empty();
    };

    Setup &setup = out.setup;
    setup = Setup();
    setup.material               = out.material;
    setup.temperature            = eng.temperature;
    setup.required_safety_factor = eng.safety_factor;
    if (print_config != nullptr) {
        DynamicPrintConfig cfg = *print_config;
        cfg.apply(object.config.get(), true);
        if (const ConfigOptionFloat *lh = cfg.option<ConfigOptionFloat>("layer_height"); lh && lh->value > 0.)
            out.layer_height = lh->value;
        // Belt printers: the layers are inclined by the angle of the gantry (stacked along its normal).
        if (Belt::enabled(cfg))
            setup.build_direction = Belt::Frame(Belt::angle(cfg)).layer_normal_world();
        // Nozzle temperature of the extruder of the object (layer adhesion).
        if (const ConfigOptionInts *t = cfg.option<ConfigOptionInts>("temperature"); t && ! t->values.empty()) {
            int extruder = 1;
            if (const ConfigOption *e = cfg.option("extruder"); e && e->getInt() > 0)
                extruder = e->getInt();
            setup.print_temperature = double(t->get_at(size_t(extruder - 1)));
        }
    }
    if (infill.enabled && infill.perimeter_width > 0.)
        out.line_width = infill.perimeter_width;
    setup.infill                 = std::move(infill);
    for (const EngineeringRegion &region : eng.fixtures) {
        Fixture fixture;
        if (! region_triangles(region, fixture.triangles)) {
            error = "A fixed region refers to a part that changed or was removed: select it again";
            return false;
        }
        setup.fixtures.emplace_back(std::move(fixture));
    }
    BoundingBoxf3 bbox;
    for (const Vec3f &v : out.mesh.vertices)
        bbox.merge(v.cast<double>());
    const double largest_dimension = bbox.size().maxCoeff();
    for (const EngineeringLoad &eload : eng.loads) {
        Load load;
        load.force = eload.force;
        double limit = eload.max_displacement;
        if (eload.max_displacement_percent > 0.) {
            const double from_percent = eload.max_displacement_percent * 0.01 * largest_dimension;
            limit = limit > 0. ? std::min(limit, from_percent) : from_percent;
        }
        load.max_displacement = limit;
        if (eload.type == EngineeringLoad::Type::Faces) {
            load.type = Load::Type::Faces;
            if (! region_triangles(eload.faces, load.triangles)) {
                error = "The load \"" + eload.name + "\" refers to a part that changed or was removed: select it again";
                return false;
            }
        } else {
            if (eload.volume < 0 || eload.volume >= int(object.volumes.size()) || triangle_offset[eload.volume] < 0) {
                error = "The load \"" + eload.name + "\" refers to a part that was removed: place it again";
                return false;
            }
            load.type   = Load::Type::Point;
            load.point  = volume_trafo[eload.volume] * eload.point;
            load.radius = eload.radius;
        }
        setup.loads.emplace_back(std::move(load));
    }
    return true;
}

// Removes the modifiers whose name starts with one of the prefixes; the regions of the engineering setup follow
// the volumes.
static void remove_modifiers(ModelObject &object, std::initializer_list<const char*> prefixes)
{
    std::vector<int> new_index(object.volumes.size(), -1);
    int next = 0;
    for (size_t v = 0; v < object.volumes.size(); ++ v) {
        bool remove = false;
        for (const char *p : prefixes)
            remove |= object.volumes[v]->is_modifier() && starts_with(object.volumes[v]->name, p);
        if (! remove)
            new_index[v] = next ++;
    }
    for (int v = int(object.volumes.size()) - 1; v >= 0; -- v)
        if (new_index[v] < 0)
            object.delete_volume(size_t(v));
    auto remap = [&](int &volume) { volume = volume >= 0 && volume < int(new_index.size()) ? new_index[volume] : -1; };
    for (EngineeringRegion &r : object.engineering.fixtures)
        remap(r.volume);
    for (EngineeringLoad &l : object.engineering.loads) {
        remap(l.volume);
        remap(l.faces.volume);
    }
}

// Adds a modifier with a mesh in print coordinates of the instance.
static ModelVolume* add_modifier(ModelObject &object, size_t instance_idx, const indexed_triangle_set &mesh, const std::string &name)
{
    const Transform3d to_object = object.instances[std::min(instance_idx, object.instances.size() - 1)]->get_matrix().inverse();
    indexed_triangle_set its = mesh;
    its_transform(its, to_object);
    if (to_object.matrix().block<3, 3>(0, 0).determinant() < 0.)
        its_flip_triangles(its);
    if (its.indices.empty())
        return nullptr;
    ModelVolume *volume = object.add_volume(TriangleMesh(std::move(its)), ModelVolumeType::PARAMETER_MODIFIER);
    volume->name = name;
    return volume;
}

void apply_reinforcement(ModelObject &object, size_t instance_idx, double density, const InfillZone &zone, int perimeters)
{
    remove_modifiers(object, { REINFORCEMENT_NAME, LATTICE_NAME });
    object.config.set_key_value("fill_density", new ConfigOptionPercent(std::round(std::clamp(density, 0., 1.) * 100.)));
    if (ModelVolume *v = add_modifier(object, instance_idx, zone.mesh, REINFORCEMENT_NAME)) {
        const int percent = int(std::round(std::clamp(std::max(zone.density, density), 0., 1.) * 100.));
        v->config.set_key_value("fill_density", new ConfigOptionPercent(percent));
        v->config.set_key_value("perimeters", new ConfigOptionInt(perimeters));
    }
    object.invalidate_bounding_box();
}

void apply_lattice(ModelObject &object, size_t instance_idx, const indexed_triangle_set &struts, double cell)
{
    remove_modifiers(object, { LATTICE_NAME, INFILL_ZONE_NAME, REINFORCEMENT_NAME });
    object.config.set_key_value("fill_density", new ConfigOptionPercent(0));
    char name[64];
    snprintf(name, sizeof(name), "%s %g mm", LATTICE_NAME, cell);
    if (ModelVolume *v = add_modifier(object, instance_idx, struts, name))
        v->config.set_key_value("fill_density", new ConfigOptionPercent(100));
    object.invalidate_bounding_box();
}

size_t apply_infill(ModelObject &object, size_t instance_idx, double density, const std::vector<InfillZone> &zones)
{
    remove_modifiers(object, { INFILL_ZONE_NAME, LATTICE_NAME });
    object.config.set_key_value("fill_density", new ConfigOptionPercent(std::round(std::clamp(density, 0., 1.) * 100.)));

    const Transform3d to_object = object.instances[std::min(instance_idx, object.instances.size() - 1)]->get_matrix().inverse();
    size_t added = 0;
    for (const InfillZone &zone : zones) {
        indexed_triangle_set its = zone.mesh;
        its_transform(its, to_object);
        if (to_object.matrix().block<3, 3>(0, 0).determinant() < 0.)
            its_flip_triangles(its);
        if (its.indices.empty())
            continue;
        ModelVolume *volume = object.add_volume(TriangleMesh(std::move(its)), ModelVolumeType::PARAMETER_MODIFIER);
        const int percent = int(std::round(std::clamp(zone.density, 0., 1.) * 100.));
        volume->name = std::string(INFILL_ZONE_NAME) + " " + std::to_string(percent) + "%";
        volume->config.set_key_value("fill_density", new ConfigOptionPercent(percent));
        ++ added;
    }
    object.invalidate_bounding_box();
    return added;
}

} // namespace Fea
} // namespace Slic3r
