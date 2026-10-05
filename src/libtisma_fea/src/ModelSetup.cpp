///|/ Tisma Slicer: analysis of an object of the model with its EngineeringSetup (phase 5).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/ModelSetup.hpp"

#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/Model.hpp>

namespace Slic3r {
namespace Fea {

bool build_analysis_input(const ModelObject &object, size_t instance_idx, const std::string &filament_type,
                          ModelAnalysisInput &out, std::string &error)
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

} // namespace Fea
} // namespace Slic3r
