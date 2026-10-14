///|/ Tisma Slicer: splitting an object larger than the printer into pieces joined by connectors.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Fragment.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <boost/log/trivial.hpp>

#include "ClipperUtils.hpp"
#include "CutUtils.hpp"
#include "ExPolygon.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "TriangleMeshSlicer.hpp"

namespace Slic3r {
namespace Fragment {

size_t Plan::joint_count() const
{
    size_t n = 0;
    for (const CutPlane &cut : cuts)
        n += cut.connectors.size();
    return n;
}

namespace {

// Rotation taking the local (u, v, w) frame of a plane perpendicular to the axis to the world: w is along the axis.
// It is also the rotation of the cut plane and of the connectors (their Z along the axis).
Transform3d axis_rotation(int axis)
{
    Matrix3d m = Matrix3d::Identity();
    if (axis == 0)
        m << 0, 0, 1,
             1, 0, 0,
             0, 1, 0;   // world = (w, u, v)
    else if (axis == 1)
        m << 0, 1, 0,
             0, 0, 1,
             1, 0, 0;   // world = (v, w, u)
    Transform3d t = Transform3d::Identity();
    t.linear()    = m;
    return t;
}

// World axes of u and v for a plane perpendicular to the axis.
std::array<int, 2> plane_axes(int axis)
{
    return axis == 0 ? std::array<int, 2>{ 1, 2 } : axis == 1 ? std::array<int, 2>{ 2, 0 } : std::array<int, 2>{ 0, 1 };
}

// Cross sections of the mesh (world coordinates) by planes perpendicular to the axis at the given positions.
std::vector<ExPolygons> sections(const indexed_triangle_set &mesh, int axis, const std::vector<double> &positions)
{
    MeshSlicingParamsEx params;
    params.trafo = axis_rotation(axis).inverse();
    std::vector<float> zs(positions.begin(), positions.end());
    return slice_mesh_ex(mesh, zs, params);
}

double area_mm2(const ExPolygons &expolygons)
{
    double a = 0.;
    for (const ExPolygon &e : expolygons)
        a += e.area();
    return a * SCALING_FACTOR * SCALING_FACTOR;
}

// Distance kept between the centre of a connector and the outline of the section or another cut.
double joint_keep_out(const Params &params, double diameter)
{
    return 0.5 * diameter + params.clearance + params.joint_wall;
}

// Diameters tried, largest first: the one asked, then thinner ones for thin walls.
std::vector<double> joint_diameters(const Params &params)
{
    std::vector<double> out{ params.joint_diameter };
    for (double d : { 0.6 * params.joint_diameter, params.min_joint_diameter })
        if (d >= params.min_joint_diameter && d < out.back() - EPSILON)
            out.push_back(d);
    return out;
}

// Shortest piece along an axis: room for the connectors of both of its cuts.
double min_piece(const Params &params)
{
    return params.joint == Joint::None ? 1. : 2. * (params.joint_depth + params.clearance + params.joint_wall);
}

// Positions of the cuts along one axis. Empty and no error when no cut is needed.
std::vector<double> cut_positions(const indexed_triangle_set &mesh, const BoundingBoxf3 &bbox, int axis, double max_len,
                                  const Params &params, std::vector<double> &areas, std::string &error)
{
    const double lo   = bbox.min[axis];
    const double size = bbox.size()[axis];
    if (size <= max_len + EPSILON)
        return {};
    const double lmin = min_piece(params);
    if (max_len < 2. * lmin) {
        error = _u8L("The printable size is too small for the connectors");
        return {};
    }
    // Candidate positions and their cross sections.
    const double        step = std::max(0.5, size / 1500.);
    const int           K    = int(std::floor(size / step));
    std::vector<double> w;
    // Off the round numbers: a plane through vertices of the mesh (as the corners left by another cut) leaves the cut
    // tool with an open mesh.
    for (int k = 1; k < K; ++k)
        w.push_back(lo + k * step + 0.0137);
    const std::vector<ExPolygons> secs  = sections(mesh, axis, w);
    std::vector<double>           area(w.size(), 0.);
    std::vector<char>             fits(w.size(), 1);
    double                        amax = 0.;
    for (size_t k = 0; k < w.size(); ++k) {
        area[k] = area_mm2(secs[k]);
        amax    = std::max(amax, area[k]);
        if (params.joint != Joint::None && area[k] > 0.)
            fits[k] = !offset_ex(secs[k], -float(scale_(joint_keep_out(params, joint_diameters(params).back())))).empty();
    }
    if (amax <= 0.)
        amax = 1.;

    for (int n = int(std::ceil(size / max_len - EPSILON)); n <= int(std::ceil(size / max_len)) + 3; ++n) {
        // Dynamic programming over the cuts: the cut i at the candidate k, every piece between lmin and max_len.
        // Cost: the section (less to glue), a connector that does not fit, and a little the distance to even pieces.
        const size_t                     N   = w.size();
        const double                     inf = std::numeric_limits<double>::infinity();
        std::vector<std::vector<double>> best(n - 1, std::vector<double>(N, inf));
        std::vector<std::vector<int>>    from(n - 1, std::vector<int>(N, -1));
        auto cost = [&](int i, size_t k) {
            const double even = lo + (i + 1) * size / n;
            const double d    = (w[k] - even) / max_len;
            return area[k] / amax + (fits[k] ? 0. : 1.) + 0.25 * d * d;
        };
        for (size_t k = 0; k < N; ++k)
            if (w[k] - lo <= max_len && w[k] - lo >= lmin)
                best[0][k] = cost(0, k);
        for (int i = 1; i < n - 1; ++i)
            for (size_t k = 0; k < N; ++k) {
                double c = inf;
                int    f = -1;
                for (size_t j = 0; j < k; ++j)
                    if (best[i - 1][j] < c && w[k] - w[j] <= max_len && w[k] - w[j] >= lmin) {
                        c = best[i - 1][j];
                        f = int(j);
                    }
                if (f >= 0) {
                    best[i][k] = c + cost(i, k);
                    from[i][k] = f;
                }
            }
        double c    = inf;
        int    last = -1;
        for (size_t k = 0; k < N; ++k) {
            const double rest = lo + size - w[k];
            if (best[n - 2][k] < c && rest <= max_len && rest >= lmin) {
                c    = best[n - 2][k];
                last = int(k);
            }
        }
        if (last < 0)
            continue;
        std::vector<double> out(n - 1);
        areas.assign(n - 1, 0.);
        for (int i = n - 2, k = last; i >= 0; k = from[i][k], --i) {
            out[i]   = w[k];
            areas[i] = area[k];
        }
        return out;
    }
    error = _u8L("No positions of the cuts keep every piece within the printable size");
    return {};
}

// Up to max_count points of the region, far from each other: the one nearest to the centre of each island first.
std::vector<Point> spread_points(const ExPolygons &region, double d, const Params &params)
{
    std::vector<Point> out;
    const coord_t      min_gap = scaled<coord_t>(4. * d);
    for (const ExPolygon &island : region) {
        const double area = island.area() * SCALING_FACTOR * SCALING_FACTOR;
        const int    want = std::clamp(int(area / (M_PI * 9. * d * d)), 1, std::max(1, params.max_joints_per_section));
        const BoundingBox bb = get_extents(island);
        const double grid    = std::max(0.5 * d, std::sqrt(std::max(area, 1.) / 400.));
        std::vector<Point> candidates;
        for (coord_t x = bb.min.x(); x <= bb.max.x(); x += scaled<coord_t>(grid))
            for (coord_t y = bb.min.y(); y <= bb.max.y(); y += scaled<coord_t>(grid))
                if (island.contains(Point(x, y)))
                    candidates.emplace_back(x, y);
        if (candidates.empty()) {
            // A thin island: its centre if inside.
            const Point c = bb.center();
            if (!island.contains(c))
                continue;
            candidates.push_back(c);
        }
        const Point        centre = island.contour.centroid();
        std::vector<Point> chosen;
        chosen.push_back(*std::min_element(candidates.begin(), candidates.end(), [&centre](const Point &a, const Point &b) {
            return (a - centre).cast<double>().squaredNorm() < (b - centre).cast<double>().squaredNorm();
        }));
        while (int(chosen.size()) < want) {
            double far   = 0.;
            Point  pick;
            for (const Point &c : candidates) {
                double nearest = std::numeric_limits<double>::max();
                for (const Point &p : chosen)
                    nearest = std::min(nearest, (c - p).cast<double>().norm());
                if (nearest > far) {
                    far  = nearest;
                    pick = c;
                }
            }
            if (far < double(min_gap))
                break;
            chosen.push_back(pick);
        }
        out.insert(out.end(), chosen.begin(), chosen.end());
    }
    return out;
}

// Connectors of one cut: on each part of the section between the other cuts, away from the outline (also a connector
// depth to both sides, where the holes go) and from the other cuts. Each island gets the largest diameter that fits.
void place_joints(const indexed_triangle_set &mesh, const BoundingBoxf3 &bbox, const std::array<std::vector<double>, 3> &cuts,
                  const Params &params, CutPlane &cut)
{
    if (params.joint == Joint::None)
        return;
    const double                  depth = params.joint_depth + params.clearance;
    const std::vector<ExPolygons> secs  = sections(mesh, cut.axis, { cut.position - depth, cut.position, cut.position + depth });
    // Where the centre of a connector of each diameter may go.
    const std::vector<double> diameters = joint_diameters(params);
    std::vector<ExPolygons>   regions;
    for (double d : diameters) {
        const float keep   = -float(scale_(joint_keep_out(params, d)));
        ExPolygons  region = offset_ex(secs[1], keep);
        for (const ExPolygons *s : { &secs[0], &secs[2] })
            region = intersection_ex(region, offset_ex(*s, keep));
        regions.push_back(std::move(region));
    }

    const std::array<int, 2> uv = plane_axes(cut.axis);
    std::array<std::vector<double>, 2> bounds;
    for (int i = 0; i < 2; ++i) {
        bounds[i].push_back(bbox.min[uv[i]]);
        bounds[i].insert(bounds[i].end(), cuts[uv[i]].begin(), cuts[uv[i]].end());
        bounds[i].push_back(bbox.max[uv[i]]);
    }
    const Transform3d to_world = axis_rotation(cut.axis);
    // Rectangle of a cell, away from the other cuts by keep (not from the outside of the object).
    auto cell = [&bounds](size_t a, size_t b, double keep) {
        const double u0 = bounds[0][a] + (a == 0 ? -1. : keep), u1 = bounds[0][a + 1] - (a + 2 == bounds[0].size() ? -1. : keep);
        const double v0 = bounds[1][b] + (b == 0 ? -1. : keep), v1 = bounds[1][b + 1] - (b + 2 == bounds[1].size() ? -1. : keep);
        return u1 <= u0 || v1 <= v0 ? Polygons{} :
            Polygons{ Polygon({ Point::new_scale(u0, v0), Point::new_scale(u1, v0), Point::new_scale(u1, v1), Point::new_scale(u0, v1) }) };
    };
    for (size_t a = 0; a + 1 < bounds[0].size(); ++a)
        for (size_t b = 0; b + 1 < bounds[1].size(); ++b)
            for (const ExPolygon &island : intersection_ex(secs[1], cell(a, b, 0.))) {
                if (island.area() * SCALING_FACTOR * SCALING_FACTOR < 1.)
                    continue;
                bool placed = false;
                for (size_t i = 0; i < diameters.size() && !placed; ++i) {
                    const Polygons rect = cell(a, b, joint_keep_out(params, diameters[i]));
                    if (rect.empty())
                        continue;
                    for (const Point &p : spread_points(intersection_ex(intersection_ex(regions[i], rect), island), diameters[i], params)) {
                        const Vec2d q = unscaled(p);
                        cut.connectors.push_back({ to_world * Vec3d(q.x(), q.y(), cut.position), diameters[i] });
                        placed = true;
                    }
                }
                if (!placed)
                    ++cut.sections_without_joint;
            }
}

indexed_triangle_set world_mesh(const ModelObject &object, size_t instance)
{
    indexed_triangle_set its = object.raw_indexed_triangle_set();
    its_transform(its, object.instances[instance]->get_matrix());
    return its;
}

bool is_dowel(const ModelObject &object)
{
    for (const ModelVolume *v : object.volumes)
        if (v->is_model_part() && !v->cut_info.is_connector)
            return false;
    return !object.volumes.empty();
}

} // namespace

Plan plan(const ModelObject &object, size_t instance, const Params &params)
{
    Plan out;
    if (instance >= object.instances.size()) {
        out.error = _u8L("No such instance");
        return out;
    }
    const indexed_triangle_set mesh = world_mesh(object, instance);
    if (mesh.indices.empty()) {
        out.error = _u8L("The object has no solid part");
        return out;
    }
    for (const stl_vertex &v : mesh.vertices)
        out.bbox.merge(v.cast<double>());
    std::array<std::vector<double>, 3> cuts;
    std::array<std::vector<double>, 3> areas;
    for (int axis = 0; axis < 3; ++axis) {
        // Z has no margin: the height of the printable volume is all usable.
        // The plug of a piece sticks out of its cut.
        const double max_len = params.max_size[axis] - (axis < 2 ? 2. * params.margin : 0.) -
                               (params.joint == Joint::Plug ? params.joint_depth : 0.);
        if (max_len <= 0.) {
            out.error = _u8L("The printable size is smaller than the margin");
            return out;
        }
        cuts[axis] = cut_positions(mesh, out.bbox, axis, max_len, params, areas[axis], out.error);
        if (!out.error.empty())
            return out;
        out.pieces[axis] = int(cuts[axis].size()) + 1;
    }
    for (int axis = 0; axis < 3; ++axis)
        for (size_t i = 0; i < cuts[axis].size(); ++i) {
            CutPlane cut;
            cut.axis     = axis;
            cut.position = cuts[axis][i];
            cut.area     = areas[axis][i];
            place_joints(mesh, out.bbox, cuts, params, cut);
            out.cuts.push_back(std::move(cut));
        }
    return out;
}

ModelObjectPtrs apply(const ModelObject &object, size_t instance, const Plan &plan, const Params &params, Model &out)
{
    if (!plan.error.empty() || !plan.needed() || instance >= object.instances.size())
        return {};

    // A copy of the object with only the instance to split.
    Model        work;
    ModelObject *src = work.add_object(object);
    for (int i = int(src->instances.size()) - 1; i >= 0; --i)
        if (size_t(i) != instance)
            src->delete_instance(size_t(i));
    src->cut_connectors.clear();
    src->cut_id.invalidate();

    std::vector<ModelObject*> pieces{ src };
    std::vector<ModelObject*> dowels;
    const double              tol = 1e-3;
    for (const CutPlane &cut : plan.cuts) {
        const Transform3d        rot = axis_rotation(cut.axis);
        const std::array<int, 2> uv  = plane_axes(cut.axis);
        std::vector<ModelObject*> next;
        for (ModelObject *piece : pieces) {
            const BoundingBoxf3 bb = piece->instance_bounding_box(0);
            if (!(bb.min[cut.axis] < cut.position - tol && bb.max[cut.axis] > cut.position + tol)) {
                next.push_back(piece);
                continue;
            }
            const Vec3d offset = piece->instances.front()->get_offset();
            // Connectors of this piece, as the cut tool makes them (GLGizmoCut3D::apply_cut_connectors()).
            int dowel_count = 0;
            for (const Connector &c : cut.connectors) {
                const Vec3d &j = c.position;
                if (j[uv[0]] < bb.min[uv[0]] - tol || j[uv[0]] > bb.max[uv[0]] + tol ||
                    j[uv[1]] < bb.min[uv[1]] - tol || j[uv[1]] > bb.max[uv[1]] + tol)
                    continue;
                const CutConnectorType type   = params.joint == Joint::Dowel ? CutConnectorType::Dowel : CutConnectorType::Plug;
                const double           radius = 0.5 * c.diameter;
                double                 height = params.joint_depth;
                Vec3d                  pos    = j - offset;
                if (type == CutConnectorType::Dowel) {
                    // Half of the pin in each piece.
                    height *= 2.;
                    ++dowel_count;
                } else
                    // The plug stands on the lower piece, the hole is in the upper one.
                    pos += rot.linear() * Vec3d::UnitZ() * 0.5 * height;
                ModelVolume *v = piece->add_volume(TriangleMesh(its_make_cylinder(1., 1., 2. * PI / 360.)), ModelVolumeType::NEGATIVE_VOLUME);
                v->set_transformation(Geometry::translation_transform(pos) * rot *
                                      Geometry::scale_transform(Vec3d(radius, radius, height)));
                v->cut_info = ModelVolume::CutInfo(type, float(params.clearance), float(params.clearance));
                v->name     = "Connector-" + std::to_string(piece->volumes.size());
            }
            Vec3d plane_point     = bb.center();
            plane_point[cut.axis] = cut.position;
            const ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                                        only_if(dowel_count > 0, ModelObjectCutAttribute::CreateDowels);
            Cut cutter(piece, 0, Geometry::translation_transform(plane_point - offset) * rot, attributes);
            for (ModelObject *result : cutter.perform_with_plane()) {
                ModelObject *copy = work.add_object(*result);
                (is_dowel(*copy) ? dowels : next).push_back(copy);
            }
            work.delete_object(piece);
        }
        pieces = std::move(next);
    }

    ModelObjectPtrs result;
    int             index = 0;
    for (ModelObject *piece : pieces) {
        ModelObject *o = out.add_object(*piece);
        o->name        = object.name + " - " + std::to_string(++index);
        o->ensure_on_bed();
        result.push_back(o);
    }
    for (ModelObject *dowel : dowels) {
        ModelObject *o = out.add_object(*dowel);
        o->name        = object.name + " - pin";
        o->ensure_on_bed();
        result.push_back(o);
    }
    BOOST_LOG_TRIVIAL(info) << "Fragment: " << object.name << " split into " << pieces.size() << " pieces with "
                            << plan.joint_count() << " connectors";
    return result;
}

} // namespace Fragment
} // namespace Slic3r
