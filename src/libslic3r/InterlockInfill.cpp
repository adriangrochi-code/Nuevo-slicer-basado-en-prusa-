///|/ Tisma Slicer: interlocking non-planar infill.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "InterlockInfill.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCodeMoveLine.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"

namespace Slic3r {
namespace Interlock {

namespace {

// Largest number of cells of a grid: the cells grow for large objects.
constexpr double MAX_CELLS = 65536.;
// The amplitude changes by at most this fraction of the layer height from one layer to the next (the layer thickness
// stays within 80-120 %).
constexpr double MAX_THICKNESS_CHANGE = 0.2;

// Cells whose center is inside the polygons (even-odd over the contours and the holes).
std::vector<uint8_t> rasterize(const ExPolygons &expolys, const Vec2d &origin, double cell, int nx, int ny)
{
    std::vector<uint8_t> mask(size_t(nx) * size_t(ny), 0);
    std::vector<std::pair<Vec2d, Vec2d>> edges;
    auto add = [&edges](const Polygon &poly) {
        for (size_t i = 0; i < poly.points.size(); ++ i)
            edges.emplace_back(unscaled(poly.points[i]), unscaled(poly.points[(i + 1) % poly.points.size()]));
    };
    for (const ExPolygon &expoly : expolys) {
        add(expoly.contour);
        for (const Polygon &hole : expoly.holes)
            add(hole);
    }
    std::vector<double> xs;
    for (int iy = 0; iy < ny; ++ iy) {
        const double y = origin.y() + (iy + 0.5) * cell;
        xs.clear();
        for (const auto &[a, b] : edges)
            if ((a.y() <= y) != (b.y() <= y))
                xs.emplace_back(a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y()));
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            const int ix0 = std::max(0, int(std::ceil((xs[k] - origin.x()) / cell - 0.5)));
            const int ix1 = std::min(nx - 1, int(std::floor((xs[k + 1] - origin.x()) / cell - 0.5)));
            for (int ix = ix0; ix <= ix1; ++ ix)
                mask[size_t(iy) * nx + ix] = 1;
        }
    }
    return mask;
}

// Distance (in cells, 3-4 chamfer) of the cells of the mask to the nearest cell outside it.
std::vector<float> distance_inside(const std::vector<uint8_t> &mask, int nx, int ny)
{
    const float big = std::numeric_limits<float>::max() / 4.f;
    std::vector<float> d(mask.size());
    for (size_t i = 0; i < mask.size(); ++ i)
        d[i] = mask[i] ? big : 0.f;
    const float a = 1.f, b = std::sqrt(2.f);
    auto at = [&](int ix, int iy) -> float { return ix < 0 || iy < 0 || ix >= nx || iy >= ny ? 0.f : d[size_t(iy) * nx + ix]; };
    for (int iy = 0; iy < ny; ++ iy)
        for (int ix = 0; ix < nx; ++ ix) {
            float &v = d[size_t(iy) * nx + ix];
            if (v > 0.f)
                v = std::min({ v, at(ix - 1, iy) + a, at(ix, iy - 1) + a, at(ix - 1, iy - 1) + b, at(ix + 1, iy - 1) + b });
        }
    for (int iy = ny - 1; iy >= 0; -- iy)
        for (int ix = nx - 1; ix >= 0; -- ix) {
            float &v = d[size_t(iy) * nx + ix];
            if (v > 0.f)
                v = std::min({ v, at(ix + 1, iy) + a, at(ix, iy + 1) + a, at(ix + 1, iy + 1) + b, at(ix - 1, iy + 1) + b });
        }
    return d;
}

} // namespace

Field::Field(const Print &print, const Params &params) : m_params(params)
{
    const double taper = std::max(1., 0.25 * params.wavelength);
    for (const PrintObject *print_object : print.objects()) {
        if (print_object->layers().empty() || print_object->instances().empty())
            continue;
        Object object;
        const double layer_height = print_object->config().layer_height.value;
        object.amplitude = std::clamp(params.amplitude, 0., 1.) * layer_height;
        if (object.amplitude <= 0.)
            continue;
        const BoundingBox bbox = print_object->bounding_box();
        const Vec2d size = unscaled(bbox.size());
        object.cell   = std::max({ 0.5, std::sqrt(std::max(size.x(), 1.) * std::max(size.y(), 1.) / MAX_CELLS) });
        object.origin = unscaled(bbox.min) - Vec2d(object.cell, object.cell);
        object.nx     = int(std::ceil(size.x() / object.cell)) + 3;
        object.ny     = int(std::ceil(size.y() / object.cell)) + 3;
        for (const PrintInstance &instance : print_object->instances())
            object.shifts.emplace_back(unscaled(instance.shift));
        const size_t cells = size_t(object.nx) * size_t(object.ny);

        // Factor of each layer from the distance to what is not sparse infill in the layer.
        std::vector<std::vector<uint8_t>> horizontal(print_object->layers().size());
        for (size_t i = 0; i < print_object->layers().size(); ++ i) {
            const Slic3r::Layer *layer = print_object->layers()[i];
            object.layers.push_back({ layer->print_z, {} });
            ExPolygons sparse;
            for (const LayerRegion *region : layer->regions())
                if (region->region().config().fill_density.value > 0.)
                    for (const Surface &surface : region->fill_surfaces().surfaces)
                        if (surface.surface_type == stInternal)
                            sparse.emplace_back(surface.expolygon);
            if (sparse.empty())
                continue;
            const std::vector<uint8_t> mask = rasterize(union_ex(sparse), object.origin, object.cell, object.nx, object.ny);
            const std::vector<float>   dist = distance_inside(mask, object.nx, object.ny);
            std::vector<uint8_t> &h = horizontal[i];
            h.assign(cells, 0);
            bool any = false;
            for (size_t c = 0; c < cells; ++ c)
                if (dist[c] > 0.f) {
                    h[c] = uint8_t(std::lround(255. * std::min(1., dist[c] * object.cell / taper)));
                    any = any || h[c] > 0;
                }
            if (! any)
                h.clear();
        }

        // Down to zero next to the layers that are not sparse infill there, by a step per layer.
        const int ramp = std::max(1, int(std::ceil(params.amplitude / MAX_THICKNESS_CHANGE - 1e-9)));
        const int step = int(std::ceil(255. / ramp));
        auto propagate = [&](auto begin, auto end, auto layer_of) {
            std::vector<int> carry(cells, std::numeric_limits<int>::max() / 2);
            for (auto it = begin; it != end; ++ it) {
                const size_t i = layer_of(it);
                const std::vector<uint8_t> &h = horizontal[i];
                std::vector<uint8_t> &a = object.layers[i].amplitude;
                if (! h.empty()) {
                    if (a.empty())
                        a = h;
                    for (size_t c = 0; c < cells; ++ c)
                        a[c] = uint8_t(std::min<int>(a[c], carry[c]));
                }
                for (size_t c = 0; c < cells; ++ c)
                    carry[c] = std::min<int>(h.empty() ? 0 : h[c], carry[c] + step);
            }
        };
        std::vector<size_t> order(horizontal.size());
        for (size_t i = 0; i < order.size(); ++ i)
            order[i] = i;
        // From below, then from above (the first pass leaves the result in the amplitudes).
        propagate(order.begin(), order.end(), [](auto it) { return *it; });
        propagate(order.rbegin(), order.rend(), [](auto it) { return *it; });
        for (Layer &layer : object.layers)
            if (! layer.amplitude.empty() && std::all_of(layer.amplitude.begin(), layer.amplitude.end(), [](uint8_t v) { return v == 0; }))
                layer.amplitude.clear();
        m_objects.emplace_back(std::move(object));
    }
}

double Field::factor(const Object &object, int layer, const Vec2d &p) const
{
    if (layer < 0 || layer >= int(object.layers.size()))
        return 0.;
    const std::vector<uint8_t> &a = object.layers[layer].amplitude;
    if (a.empty())
        return 0.;
    const double fx = (p.x() - object.origin.x()) / object.cell - 0.5, fy = (p.y() - object.origin.y()) / object.cell - 0.5;
    const int ix = std::clamp(int(std::floor(fx)), 0, object.nx - 2), iy = std::clamp(int(std::floor(fy)), 0, object.ny - 2);
    const double tx = std::clamp(fx - ix, 0., 1.), ty = std::clamp(fy - iy, 0., 1.);
    auto v = [&](int x, int y) { return double(a[size_t(y) * object.nx + x]); };
    return ((1. - ty) * ((1. - tx) * v(ix, iy) + tx * v(ix + 1, iy)) + ty * ((1. - tx) * v(ix, iy + 1) + tx * v(ix + 1, iy + 1))) / 255.;
}

double Field::offset(const Object &object, int layer, double x, double y) const
{
    for (const Vec2d &shift : object.shifts) {
        const Vec2d p = Vec2d(x, y) - shift;
        if (p.x() < object.origin.x() || p.y() < object.origin.y() || p.x() > object.origin.x() + object.nx * object.cell ||
            p.y() > object.origin.y() + object.ny * object.cell)
            continue;
        const double k = 2. * M_PI / m_params.wavelength;
        return object.amplitude * this->factor(object, layer, p) * std::sin(k * x) * std::sin(k * y);
    }
    return 0.;
}

std::optional<std::pair<const Field::Object*, int>> Field::find(double x, double y, double print_z) const
{
    for (const Object &object : m_objects)
        for (const Vec2d &shift : object.shifts) {
            const Vec2d p = Vec2d(x, y) - shift;
            if (p.x() < object.origin.x() || p.y() < object.origin.y() || p.x() > object.origin.x() + object.nx * object.cell ||
                p.y() > object.origin.y() + object.ny * object.cell)
                continue;
            auto it = std::lower_bound(object.layers.begin(), object.layers.end(), print_z - EPSILON,
                                       [](const Layer &l, double z) { return l.print_z < z; });
            if (it != object.layers.end() && std::abs(it->print_z - print_z) < EPSILON)
                return std::make_pair(&object, int(it - object.layers.begin()));
        }
    return std::nullopt;
}

using namespace GCodeMoveLine;

Filter::Filter(const Field &field, const FilterParams &params) :
    m_field(field), m_params(params), m_relative_e(params.relative_e) {}

std::string Filter::process_layer(const std::string &gcode)
{
    std::string out;
    out.reserve(gcode.size() + gcode.size() / 4);
    size_t start = 0;
    while (start < gcode.size()) {
        size_t end = gcode.find('\n', start);
        if (end == std::string::npos)
            end = gcode.size();
        this->process_line(gcode.substr(start, end - start), out);
        out += '\n';
        start = end + 1;
    }
    return out;
}

void Filter::move_z(double z, std::string &out)
{
    out += "G1";
    append_number(out, 'Z', z, 3);
    out += '\n';
    m_out_z = z;
}

void Filter::process_line(const std::string &line, std::string &out)
{
    if (starts_with(line, ";Z:")) {
        const double z = std::atof(line.c_str() + 3);
        // Printing an object after another (sequential): the crests left behind are not in the way any more.
        if (z < m_layer_z - EPSILON)
            m_crest_z = 0.;
        m_layer_z = z;
        out += line;
        return;
    }
    if (starts_with(line, ";TYPE:")) {
        m_infill = line.substr(6) == "Internal infill";
        out += line;
        return;
    }
    if (starts_with(line, ";HEIGHT:")) {
        m_height = std::atof(line.c_str() + 8);
        out += line;
        return;
    }
    if (starts_with(line, "M82")) {
        m_relative_e = false;
        out += line;
        return;
    }
    if (starts_with(line, "M83")) {
        m_relative_e = true;
        out += line;
        return;
    }
    if (starts_with(line, "G92")) {
        if (line.find('E') != std::string::npos) {
            m_e_in    = 0.;
            m_e_shift = 0.;
        }
        out += line;
        return;
    }

    Move m;
    if (! parse(line, m)) {
        out += line;
        return;
    }
    if (m.has_z)
        m_stream_z = m.z;
    const double de = m.has_e ? (m_relative_e ? m.e : m.e - m_e_in) : 0.;
    if (m.has_e && ! m_relative_e)
        m_e_in = m.e;
    const bool xy_move = (m.has_x || m.has_y) && m_x && m_y;
    const double x0 = m_x ? *m_x : 0., y0 = m_y ? *m_y : 0.;
    const double x1 = m.has_x ? m.x : x0, y1 = m.has_y ? m.y : y0;
    m_x = x1;
    m_y = y1;
    const bool   extrusion = xy_move && de > 0.;
    const double len       = std::hypot(x1 - x0, y1 - y0);

    if (extrusion && m_infill && m_height > 0. && len > 1e-6 && ! m.has_z) {
        const double print_z = m_layer_z - m_params.z_offset;
        if (auto found = m_field.find(0.5 * (x0 + x1), 0.5 * (y0 + y1), print_z); found) {
            const auto [object, layer] = *found;
            const int n = std::max(1, int(std::ceil(len / m_params.seg_len)));
            auto offset = [&](double t, int l) { return m_field.offset(*object, l, x0 + t * (x1 - x0), y0 + t * (y1 - y0)); };
            bool waved = false;
            std::vector<double> zs(n + 1);
            for (int i = 0; i <= n; ++ i) {
                zs[i] = m_stream_z + offset(double(i) / n, layer);
                waved = waved || std::abs(zs[i] - m_stream_z) >= m_params.z_tolerance;
            }
            if (waved) {
                ++ m_stats.waved;
                if (std::abs(m_out_z - zs[0]) >= m_params.z_tolerance)
                    this->move_z(zs[0], out);
                std::string feed;
                if (m.has_f)
                    append_number(feed, 'F', m.f, 0);
                for (int i = 1; i <= n; ++ i) {
                    // Flow by the thickness between this layer and the one below, in the middle of the piece.
                    const double t     = (i - 0.5) / n;
                    const double ratio = std::max(0.1, (m_height + offset(t, layer) - offset(t, layer - 1)) / m_height);
                    const double e     = de / n * ratio;
                    out += "G1";
                    append_number(out, 'X', x0 + double(i) / n * (x1 - x0), 3);
                    append_number(out, 'Y', y0 + double(i) / n * (y1 - y0), 3);
                    if (std::abs(zs[i] - m_out_z) >= m_params.z_tolerance) {
                        append_number(out, 'Z', zs[i], 3);
                        m_out_z = zs[i];
                    }
                    if (m_relative_e)
                        append_number(out, 'E', e, 5);
                    else {
                        m_e_shift += e - de / n;
                        append_number(out, 'E', m_e_in - de + de / n * i + m_e_shift, 5);
                    }
                    out += feed;
                    feed.clear();
                    out += '\n';
                    m_crest_z = std::max(m_crest_z, zs[i]);
                    m_stats.max_offset = std::max(m_stats.max_offset, std::abs(zs[i] - m_stream_z));
                }
                out.pop_back();   // the caller adds the end of line
                return;
            }
        }
    }

    if (! m.has_z) {
        if (xy_move && ! extrusion) {
            // A travel: above the crests of the waved infill printed so far in this layer and the one below.
            const double z = std::max(m_stream_z, m_crest_z);
            if (std::abs(m_out_z - z) >= m_params.z_tolerance)
                this->move_z(z, out);
        } else if (xy_move || de > 0.) {
            // Anything else at the Z of the G-code.
            if (std::abs(m_out_z - m_stream_z) >= m_params.z_tolerance)
                this->move_z(m_stream_z, out);
        }
    } else
        m_out_z = m.z;
    if (! m_relative_e && m.has_e && std::abs(m_e_shift) > 1e-9) {
        std::string rewritten = "G1";
        if (m.has_x) append_number(rewritten, 'X', m.x, 3);
        if (m.has_y) append_number(rewritten, 'Y', m.y, 3);
        if (m.has_z) append_number(rewritten, 'Z', m.z, 3);
        append_number(rewritten, 'E', m.e + m_e_shift, 5);
        if (m.has_f) append_number(rewritten, 'F', m.f, 0);
        out += rewritten + m.comment;
        return;
    }
    out += line;
}

} // namespace Interlock
} // namespace Slic3r
