///|/ Tisma Slicer: setup of the structural analysis of an object (phase 5 of docs/IMPLEMENTATION_ROADMAP.md).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Engineering_hpp_
#define slic3r_Engineering_hpp_

#include <string>
#include <vector>

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include "Point.hpp"

namespace Slic3r {

// Faces of a part of the object, as triangles of the mesh of the volume. When the volume comes from a STEP file,
// the B-Rep faces are kept as well, so that the region survives a new tessellation.
struct EngineeringRegion
{
    int              volume { 0 };
    std::vector<int> triangles;
    std::vector<int> cad_faces;

    bool operator==(const EngineeringRegion &rhs) const { return volume == rhs.volume && triangles == rhs.triangles && cad_faces == rhs.cad_faces; }
    template<class Archive> void serialize(Archive &ar) { ar(volume, triangles, cad_faces); }
};

struct EngineeringLoad
{
    enum class Type : int { Point = 0, Faces = 1 };
    Type              type { Type::Point };
    std::string       name;
    // Point loads: position in the coordinates of the mesh of the volume (it follows the volume).
    int               volume { 0 };
    Vec3d             point { Vec3d::Zero() };
    double            radius { 0. };
    // Face loads.
    EngineeringRegion faces;
    // Force [N] in the print coordinates (Z up, as on the bed).
    Vec3d             force { Vec3d::Zero() };
    // Largest allowed deformation where the load is applied: in mm and in % of the largest dimension of the
    // object (0 = no limit; with both, the smaller one applies).
    double            max_displacement { 0. };
    double            max_displacement_percent { 0. };

    bool operator==(const EngineeringLoad &rhs) const {
        return type == rhs.type && name == rhs.name && volume == rhs.volume && point == rhs.point && radius == rhs.radius &&
               faces == rhs.faces && force == rhs.force && max_displacement == rhs.max_displacement &&
               max_displacement_percent == rhs.max_displacement_percent;
    }
    template<class Archive> void serialize(Archive &ar) { ar(type, name, volume, point, radius, faces, force, max_displacement, max_displacement_percent); }
};

// Faces which must not move more than a limit (a fit, a sealing face): elsewhere the part may deform more.
struct EngineeringLimit
{
    std::string       name;
    EngineeringRegion faces;
    // Largest allowed displacement: in mm and in % of the largest dimension of the object (0 = not used; with both,
    // the smaller one applies).
    double            max_displacement { 0. };
    double            max_displacement_percent { 0. };

    bool operator==(const EngineeringLimit &rhs) const {
        return name == rhs.name && faces == rhs.faces && max_displacement == rhs.max_displacement &&
               max_displacement_percent == rhs.max_displacement_percent;
    }
    template<class Archive> void serialize(Archive &ar) { ar(name, faces, max_displacement, max_displacement_percent); }
};

// Shape of curved layers that follow the stresses of the structural analysis (non-planar mode "stress"): the layers
// are the surfaces z = s + offset(x, y, z), with the offset given at the nodes of a grid. The grid is relative to the
// minimum corner of the bounding box of the printable parts as placed on the bed, so that it does not depend on where
// the object is on the bed.
struct EngineeringLayerField
{
    // Node (0, 0, 0) relative to the minimum corner of the part and the distance between the nodes [mm].
    Vec3d              origin { Vec3d::Zero() };
    double             cell { 1. };
    int                nx { 0 }, ny { 0 }, nz { 0 };
    // Size of the bounding box of the part when the field was computed: the part must not change since.
    Vec3d              part_size { Vec3d::Zero() };
    // Offset of the layers at the nodes [mm], x first, then y, then z.
    std::vector<float> offset;
    // Safety factors of the analysis with flat layers and with these layers (0 = not computed), for the reports.
    double             safety_flat { 0. };
    double             safety_curved { 0. };

    bool empty() const { return offset.empty(); }
    bool operator==(const EngineeringLayerField &rhs) const {
        return origin == rhs.origin && cell == rhs.cell && nx == rhs.nx && ny == rhs.ny && nz == rhs.nz &&
               part_size == rhs.part_size && offset == rhs.offset && safety_flat == rhs.safety_flat &&
               safety_curved == rhs.safety_curved;
    }
    bool operator!=(const EngineeringLayerField &rhs) const { return ! (*this == rhs); }
    template<class Archive> void serialize(Archive &ar) { ar(origin, cell, nx, ny, nz, part_size, offset, safety_flat, safety_curved); }
};

// Working conditions of an object: where it is held, the loads, the ambient temperature and the material.
struct EngineeringSetup
{
    // Key of the material of the table of libtisma_fea; empty = the material of the filament of the object.
    std::string                    material;
    double                         temperature { 23. };
    double                         safety_factor { 2. };
    std::vector<EngineeringRegion> fixtures;
    std::vector<EngineeringLoad>   loads;
    std::vector<EngineeringLimit>  limits;
    // Curved layers computed from the stresses (empty = none).
    EngineeringLayerField          layer_field;

    bool empty() const { return fixtures.empty() && loads.empty() && limits.empty() && material.empty() && temperature == 23. && safety_factor == 2. && layer_field.empty(); }
    bool operator==(const EngineeringSetup &rhs) const {
        return material == rhs.material && temperature == rhs.temperature && safety_factor == rhs.safety_factor &&
               fixtures == rhs.fixtures && loads == rhs.loads && limits == rhs.limits && layer_field == rhs.layer_field;
    }
    bool operator!=(const EngineeringSetup &rhs) const { return ! (*this == rhs); }
    template<class Archive> void serialize(Archive &ar) { ar(material, temperature, safety_factor, fixtures, loads, limits, layer_field); }
};

} // namespace Slic3r

#endif // slic3r_Engineering_hpp_
