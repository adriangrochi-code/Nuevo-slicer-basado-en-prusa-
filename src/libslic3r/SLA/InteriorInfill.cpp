///|/ Tisma Slicer: infill of the interior of a hollowed resin part.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "InteriorInfill.hpp"

#include <algorithm>
#include <cmath>

#include "libslic3r/OpenVDBUtils.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {
namespace sla {

std::array<Vec3d, INFILL_DIRECTIONS> infill_directions()
{
    const double s = std::sqrt(0.5);
    return { Vec3d(0., 0., 1.), Vec3d(s, 0., s), Vec3d(-s, 0., s), Vec3d(0., s, s), Vec3d(0., -s, s) };
}

double infill_strut_width(double density, double cell)
{
    return density <= 0. ? 0. : cell * std::sqrt(std::min(density, 1.) / double(INFILL_DIRECTIONS));
}

indexed_triangle_set infill_struts(const BoundingBoxf3 &box, double density, double cell)
{
    indexed_triangle_set out;
    const double width = infill_strut_width(density, cell);
    if (width <= 0. || cell <= 0. || ! box.defined)
        return out;
    const Vec3d  center = box.center();
    // Long enough to cross the box in any direction.
    const double length = box.size().norm() + 2. * cell;
    const double half   = 0.5 * length;
    for (const Vec3d &d : infill_directions()) {
        // Two directions across the strut.
        const Vec3d u = (std::abs(d.z()) > 0.99 ? Vec3d::UnitX() : Vec3d::UnitZ().cross(d)).normalized();
        const Vec3d v = d.cross(u).normalized();
        Transform3d rotation = Transform3d::Identity();
        rotation.linear().col(0) = u;
        rotation.linear().col(1) = v;
        rotation.linear().col(2) = d;
        const int n = int(std::ceil(half / cell));
        for (int i = -n; i <= n; ++ i)
            for (int j = -n; j <= n; ++ j) {
                // The struts of a direction on a grid of the plane across them, through the center of the box.
                const Vec3d axis_point = center + (i * cell) * u + (j * cell) * v;
                // Keep only the struts which can reach the box.
                const Vec3d to_center = center - axis_point;
                if (to_center.norm() > half)
                    continue;
                indexed_triangle_set strut = its_make_cube(width, width, length);
                Transform3d t = Transform3d::Identity();
                t.translate(axis_point);
                t = t * rotation;
                t.translate(Vec3d(-0.5 * width, -0.5 * width, -half));
                its_transform(strut, t);
                its_merge(out, strut);
            }
    }
    return out;
}

bool subtract_infill(indexed_triangle_set &cavity, double density, double cell, float voxel_scale)
{
    const double width = infill_strut_width(density, cell);
    if (width <= 0. || cavity.indices.empty())
        return false;
    BoundingBoxf3 box;
    for (const stl_vertex &p : cavity.vertices)
        box.merge(p.cast<double>());
    // At least three voxels across a strut, at most 10 voxels per mm.
    const float scale = std::clamp(std::max(voxel_scale, float(3. / width)), 1.f, 10.f);
    const MeshToGridParams params = MeshToGridParams{}.voxel_scale(scale);
    VoxelGridPtr grid = mesh_to_grid(cavity, params);
    if (! grid || is_grid_empty(*grid))
        return false;
    const indexed_triangle_set struts = infill_struts(box, density, cell);
    if (struts.indices.empty())
        return false;
    VoxelGridPtr struts_grid = mesh_to_grid(struts, params);
    if (! struts_grid)
        return false;
    grid_difference(*grid, *struts_grid);
    indexed_triangle_set result = grid_to_mesh(*grid, 0., 0.);
    if (result.indices.empty())
        return false;
    cavity = std::move(result);
    return true;
}

} // namespace sla
} // namespace Slic3r
