///|/ Tisma Slicer: analysis of an object of the model with its EngineeringSetup (phase 5).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/ModelSetup.hpp"

#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace Slic3r {
namespace Fea {

const char *INFILL_ZONE_NAME = "Tisma infill zone";

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
    double nozzle = 0.4;
    if (const ConfigOptionFloats *n = cfg.option<ConfigOptionFloats>("nozzle_diameter"); n && ! n->values.empty())
        nozzle = n->values.front();
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
    const ConfigOption *perimeters = cfg.option("perimeters");
    const ConfigOption *top        = cfg.option("top_solid_layers");
    const ConfigOption *bottom     = cfg.option("bottom_solid_layers");
    infill.enabled              = true;
    infill.wall_thickness       = (perimeters ? perimeters->getInt() : 2) * w;
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
            if (d == nullptr)
                continue;
            InfillZone zone;
            zone.mesh = volume.mesh().its;
            its_transform(zone.mesh, instance_trafo * volume.get_matrix());
            if (zone.mesh.indices.empty())
                continue;
            zone.density = std::clamp(d->value * 0.01, 0., 1.);
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

size_t apply_infill(ModelObject &object, size_t instance_idx, double density, const std::vector<InfillZone> &zones)
{
    // Remove the zones of a previous optimization; the regions of the engineering setup follow the volumes.
    std::vector<int> new_index(object.volumes.size(), -1);
    int next = 0;
    for (size_t v = 0; v < object.volumes.size(); ++ v)
        if (! (object.volumes[v]->is_modifier() && object.volumes[v]->name.rfind(INFILL_ZONE_NAME, 0) == 0))
            new_index[v] = next ++;
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
