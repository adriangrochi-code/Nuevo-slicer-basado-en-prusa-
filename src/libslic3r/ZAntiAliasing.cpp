///|/ Tisma Slicer: Z anti-aliasing of the top surfaces.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ZAntiAliasing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {
namespace ZAA {

Surface::Surface(indexed_triangle_set mesh) :
    m_vertices(std::move(mesh.vertices)), m_faces(std::move(mesh.indices))
{
    m_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(m_vertices, m_faces);
}

Surface::~Surface() = default;

std::optional<double> Surface::top_in_slab(double x, double y, double z_bottom, double z_top) const
{
    if (m_faces.empty())
        return std::nullopt;
    const Vec3d origin(x, y, z_bottom + 1e-4);
    igl::Hit hit;
    if (! AABBTreeIndirect::intersect_ray_first_hit(m_vertices, m_faces, m_tree, origin, Vec3d(Vec3d::UnitZ()), hit))
        return std::nullopt;
    const double z = origin.z() + hit.t;
    if (z >= z_top)
        return std::nullopt;
    // The first face above must be one that leaves the solid (facing up), otherwise the bottom is not inside.
    const Vec3i &f = m_faces[hit.id];
    const Vec3f  n = (m_vertices[f[1]] - m_vertices[f[0]]).cross(m_vertices[f[2]] - m_vertices[f[0]]);
    if (n.z() <= 0.f)
        return std::nullopt;
    return z;
}

indexed_triangle_set print_mesh(const Print &print)
{
    indexed_triangle_set out;
    for (const PrintObject *object : print.objects()) {
        indexed_triangle_set mesh;
        for (const ModelVolume *volume : object->model_object()->volumes)
            if (volume->is_model_part()) {
                indexed_triangle_set part = volume->mesh().its;
                its_transform(part, object->trafo_centered() * volume->get_matrix());
                its_merge(mesh, part);
            }
        if (mesh.vertices.empty())
            continue;
        // Bottom of the object at its first layer (raft below), as the Z of the G-code.
        float z_min = std::numeric_limits<float>::max();
        for (const stl_vertex &v : mesh.vertices)
            z_min = std::min(z_min, v.z());
        const double z_base = print.config().z_offset.value + object->slicing_parameters().object_print_z_min;
        for (const PrintInstance &instance : object->instances()) {
            indexed_triangle_set copy = mesh;
            const Vec2d shift = unscaled<double>(instance.shift);
            for (stl_vertex &v : copy.vertices)
                v = stl_vertex(float(v.x() + shift.x()), float(v.y() + shift.y()), float(v.z() - z_min + z_base));
            its_merge(out, copy);
        }
    }
    return out;
}

namespace {

struct Move
{
    bool has_x { false }, has_y { false }, has_z { false }, has_e { false }, has_f { false };
    double x { 0. }, y { 0. }, z { 0. }, e { 0. }, f { 0. };
    std::string comment;   // with its ';'
};

// G0 / G1 with X, Y, Z, E, F only (what PrusaSlicer emits); false for anything else.
bool parse_move(const std::string &line, Move &m)
{
    const size_t semi = line.find(';');
    const std::string code = line.substr(0, semi);
    if (semi != std::string::npos)
        m.comment = line.substr(semi);
    const char *c = code.c_str();
    while (*c == ' ' || *c == '\t')
        ++ c;
    if (! (c[0] == 'G' && (c[1] == '0' || c[1] == '1') && (c[2] == ' ' || c[2] == '\t' || c[2] == 0)))
        return false;
    c += 2;
    for (;;) {
        while (*c == ' ' || *c == '\t')
            ++ c;
        if (! *c)
            break;
        const char letter = *c ++;
        char *end = nullptr;
        const double v = std::strtod(c, &end);
        if (end == c)
            return false;
        c = end;
        switch (letter) {
        case 'X': m.has_x = true; m.x = v; break;
        case 'Y': m.has_y = true; m.y = v; break;
        case 'Z': m.has_z = true; m.z = v; break;
        case 'E': m.has_e = true; m.e = v; break;
        case 'F': m.has_f = true; m.f = v; break;
        default: return false;
        }
    }
    return true;
}

void append_number(std::string &out, char axis, double value, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), " %c%.*f", axis, decimals, value);
    // Trim the trailing zeros (and the point).
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    if (s == std::string(" ") + axis + "-0")
        s = std::string(" ") + axis + "0";
    out += s;
}

bool starts_with(const std::string &s, const char *prefix)
{
    return s.rfind(prefix, 0) == 0;
}

} // namespace

Filter::Filter(const Surface &surface, const FilterParams &params) :
    m_surface(surface), m_params(params), m_relative_e(params.relative_e) {}

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

void Filter::process_line(const std::string &line, std::string &out)
{
    // Tags of the G-code processor.
    if (starts_with(line, ";TYPE:")) {
        const std::string type = line.substr(6);
        m_lower_type = std::find(m_params.types.begin(), m_params.types.end(), type) != m_params.types.end();
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
        // G92 E0 (absolute E): both the G-code and the emitted E start again.
        if (line.find('E') != std::string::npos) {
            m_e_in    = 0.;
            m_e_shift = 0.;
        }
        out += line;
        return;
    }

    Move m;
    if (! parse_move(line, m)) {
        out += line;
        return;
    }
    if (m.has_z)
        m_z = m.z;
    const double de = m.has_e ? (m_relative_e ? m.e : m.e - m_e_in) : 0.;
    if (m.has_e && ! m_relative_e)
        m_e_in = m.e;

    const bool xy_move = (m.has_x || m.has_y) && m_x && m_y;
    const double x0 = m_x ? *m_x : 0., y0 = m_y ? *m_y : 0.;
    const double x1 = m.has_x ? m.x : x0, y1 = m.has_y ? m.y : y0;
    m_x = x1;
    m_y = y1;

    const bool extrusion = xy_move && de > 0.;
    const double len = std::hypot(x1 - x0, y1 - y0);
    if (extrusion && m_lower_type && m_height > 0. && len > 1e-6) {
        // Z of the nozzle at each sample: down to the surface, at most down to the thinnest extrusion.
        const double bottom = m_z - m_height, lowest = bottom + m_params.min_thickness * m_height;
        const int n = std::max(1, int(std::ceil(len / m_params.seg_len)));
        std::vector<double> zs(n + 1);
        bool lowered = false;
        for (int i = 0; i <= n; ++ i) {
            const double t = double(i) / n;
            const std::optional<double> top = m_surface.top_in_slab(x0 + t * (x1 - x0), y0 + t * (y1 - y0), bottom, m_z);
            zs[i] = top ? std::clamp(*top, lowest, m_z) : m_z;
            if (zs[i] < m_z - m_params.z_tolerance)
                lowered = true;
        }
        if (lowered) {
            ++ m_stats.lowered;
            std::string feed;
            if (m.has_f)
                append_number(feed, 'F', m.f, 0);
            // Each piece from the previous sample, the flow scaled by the mean thickness left (planar = 1).
            int    from = 0;
            double e_acc = 0.;
            for (int i = 1; i <= n; ++ i) {
                const double frac = (0.5 * (zs[i - 1] + zs[i]) - bottom) / m_height;
                e_acc += de / n * frac;
                // Merge the pieces at the layer top.
                const bool flat_next = i < n && std::abs(zs[i] - m_z) < m_params.z_tolerance &&
                                       std::abs(zs[i + 1] - m_z) < m_params.z_tolerance &&
                                       std::abs(zs[i - 1] - m_z) < m_params.z_tolerance;
                if (flat_next)
                    continue;
                const double t = double(i) / n;
                out += "G1";
                append_number(out, 'X', x0 + t * (x1 - x0), 3);
                append_number(out, 'Y', y0 + t * (y1 - y0), 3);
                if (std::abs(zs[i] - m_out_z) >= m_params.z_tolerance || std::abs(zs[i] - zs[from]) >= m_params.z_tolerance) {
                    append_number(out, 'Z', zs[i], 3);
                    m_out_z = zs[i];
                }
                if (m_relative_e) {
                    append_number(out, 'E', e_acc, 5);
                } else {
                    m_e_shift += e_acc - (de / n) * (i - from);
                    append_number(out, 'E', m_e_in - de + (de / n) * i + m_e_shift, 5);
                }
                out += feed;
                feed.clear();
                out += '\n';
                m_stats.min_z_drop = std::min(m_stats.min_z_drop, zs[i] - m_z);
                from  = i;
                e_acc = 0.;
            }
            out.pop_back();   // the caller adds the end of line
            return;
        }
    }

    // Anything else at the Z of the G-code: back up first if the nozzle is lower (travel, the next feature).
    if (! m.has_z && std::abs(m_out_z - m_z) >= m_params.z_tolerance && xy_move) {
        out += "G1";
        append_number(out, 'Z', m_z, 3);
        out += '\n';
    }
    m_out_z = m_z;
    if (! m_relative_e && m.has_e && std::abs(m_e_shift) > 1e-9) {
        // Absolute E: shifted by what the lowered moves saved.
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

} // namespace ZAA
} // namespace Slic3r
