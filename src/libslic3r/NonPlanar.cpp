#include "NonPlanar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

#include <admesh/stl.h>

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace NonPlanar {

static constexpr double TWO_PI = 2. * M_PI;

// ------------------------------------------------------------------- Field

double Field::theta(double z) const
{
    double t = m_params.angle_deg * M_PI / 180.;
    if (m_params.pattern == Pattern::Twisted)
        t += m_params.twist_deg_per_mm * M_PI / 180. * z;
    return t;
}

double Field::g(double x, double y, double z) const
{
    const double dx = x - m_params.center.x();
    const double dy = y - m_params.center.y();
    switch (m_params.mode) {
    case Mode::Disabled:
        return 0.;
    case Mode::Conical: {
        const double r = std::sqrt(dx * dx + dy * dy + m_params.cone_tip_radius * m_params.cone_tip_radius);
        return std::tan(m_params.cone_angle_deg * M_PI / 180.) * (r - m_params.cone_tip_radius);
    }
    case Mode::Wave: {
        const double k = TWO_PI / m_params.wavelength;
        if (m_params.pattern == Pattern::Egg)
            return m_params.amplitude * std::sin(k * dx) * std::sin(k * dy);
        const double t = this->theta(z);
        return m_params.amplitude * std::sin(k * (dx * std::cos(t) + dy * std::sin(t)));
    }
    }
    return 0.;
}

Vec2d Field::grad(double x, double y, double z) const
{
    const double dx = x - m_params.center.x();
    const double dy = y - m_params.center.y();
    switch (m_params.mode) {
    case Mode::Disabled:
        return Vec2d::Zero();
    case Mode::Conical: {
        const double r = std::sqrt(dx * dx + dy * dy + m_params.cone_tip_radius * m_params.cone_tip_radius);
        const double t = std::tan(m_params.cone_angle_deg * M_PI / 180.);
        return { t * dx / r, t * dy / r };
    }
    case Mode::Wave: {
        const double k = TWO_PI / m_params.wavelength;
        const double a = m_params.amplitude * k;
        if (m_params.pattern == Pattern::Egg)
            return { a * std::cos(k * dx) * std::sin(k * dy), a * std::sin(k * dx) * std::cos(k * dy) };
        const double t = this->theta(z);
        const double c = std::cos(k * (dx * std::cos(t) + dy * std::sin(t)));
        return { a * c * std::cos(t), a * c * std::sin(t) };
    }
    }
    return Vec2d::Zero();
}

double Field::dgdz(double x, double y, double z) const
{
    if (m_params.mode != Mode::Wave || m_params.pattern != Pattern::Twisted)
        return 0.;
    const double k  = TWO_PI / m_params.wavelength;
    const double dx = x - m_params.center.x();
    const double dy = y - m_params.center.y();
    const double t  = this->theta(z);
    const double u  = dx * std::cos(t) + dy * std::sin(t);
    const double du = (-dx * std::sin(t) + dy * std::cos(t)) * m_params.twist_deg_per_mm * M_PI / 180.;
    return m_params.amplitude * k * std::cos(k * u) * du;
}

double Field::bound(double x, double y) const
{
    if (m_params.mode == Mode::Wave && m_params.pattern == Pattern::Twisted)
        return std::abs(m_params.amplitude);
    return std::abs(this->g(x, y, 0.));
}

// -------------------------------------------------------------------- Ramp

static inline double smoothstep(double t)
{
    t = std::clamp(t, 0., 1.);
    return t * t * (3. - 2. * t);
}

static inline double dsmoothstep(double t)
{
    return (t > 0. && t < 1.) ? 6. * t * (1. - t) : 0.;
}

double Ramp::value(double z) const
{
    double r = smoothstep((z - z_flat) / z_ramp);
    if (z_top)
        r *= 1. - smoothstep((z - (*z_top - z_ramp_top)) / z_ramp_top);
    return r;
}

double Ramp::deriv(double z) const
{
    const double t1  = (z - z_flat) / z_ramp;
    const double up  = smoothstep(t1);
    const double dup = dsmoothstep(t1) / z_ramp;
    if (! z_top)
        return dup;
    const double t2    = (z - (*z_top - z_ramp_top)) / z_ramp_top;
    const double down  = 1. - smoothstep(t2);
    const double ddown = -dsmoothstep(t2) / z_ramp_top;
    return dup * down + up * ddown;
}

// ------------------------------------------------------------- Deformation

double Deformation::dzs_dz(double x, double y, double z) const
{
    return 1. - m_field.g(x, y, z) * m_ramp.deriv(z) - m_field.dgdz(x, y, z) * m_ramp.value(z);
}

double Deformation::layer_slope(double x, double y, double z) const
{
    return m_field.grad(x, y, z).norm() * m_ramp.value(z);
}

double Deformation::to_real_z(double x, double y, double zs) const
{
    if (! this->enabled())
        return zs;
    // z - D(z) = zs with |D| <= bound -> the root is inside [zs - bound, zs + bound].
    const double b = m_field.bound(x, y);
    double lo = zs - b;
    double hi = zs + b;
    double z  = zs + this->offset(x, y, zs);
    for (int i = 0; i < 60; ++ i) {
        const double r = z - this->offset(x, y, z) - zs;
        if (std::abs(r) < 1e-10)
            break;
        if (r < 0.)
            lo = z;
        else
            hi = z;
        const double df   = this->dzs_dz(x, y, z);
        const double step = z - r / (std::abs(df) > 1e-9 ? df : 1e-9);
        z = (step < lo || step > hi || ! std::isfinite(step)) ? 0.5 * (lo + hi) : step;
    }
    return z;
}

Deformation::Check Deformation::check(const BoundingBoxf3 &bbox) const
{
    Check out;
    if (! this->enabled() || ! bbox.defined)
        return out;
    constexpr int N = 32;
    double max_slope = 0.;
    out.j_min = std::numeric_limits<double>::max();
    out.j_max = 0.;
    for (int i = 0; i <= N; ++ i)
        for (int j = 0; j <= N; ++ j)
            for (int k = 0; k <= N; ++ k) {
                const double x = bbox.min.x() + (bbox.max.x() - bbox.min.x()) * i / N;
                const double y = bbox.min.y() + (bbox.max.y() - bbox.min.y()) * j / N;
                const double z = bbox.min.z() + (bbox.max.z() - bbox.min.z()) * k / N;
                const double d = this->dzs_dz(x, y, z);
                const double jac = d > 0. ? 1. / d : std::numeric_limits<double>::infinity();
                out.j_min = std::min(out.j_min, jac);
                out.j_max = std::max(out.j_max, jac);
                max_slope = std::max(max_slope, this->layer_slope(x, y, z));
            }
    out.max_slope_deg = std::atan(max_slope) * 180. / M_PI;
    return out;
}

double Deformation::max_offset(const BoundingBoxf3 &bbox) const
{
    if (! this->enabled() || ! bbox.defined)
        return 0.;
    constexpr int N = 64;
    double out = 0.;
    for (int i = 0; i <= N; ++ i)
        for (int j = 0; j <= N; ++ j) {
            const double x = bbox.min.x() + (bbox.max.x() - bbox.min.x()) * i / N;
            const double y = bbox.min.y() + (bbox.max.y() - bbox.min.y()) * j / N;
            out = std::max(out, m_field.bound(x, y));
        }
    return out;
}

// -------------------------------------------------------------------- Mesh

void subdivide_mesh(indexed_triangle_set &its, double max_edge)
{
    if (max_edge <= 0.)
        return;
    const double max_edge2 = max_edge * max_edge;
    auto edge_key = [](int a, int b) -> uint64_t {
        if (a > b)
            std::swap(a, b);
        return (uint64_t(uint32_t(a)) << 32) | uint64_t(uint32_t(b));
    };
    for (int iter = 0; iter < 16; ++ iter) {
        // 1) Decide globally which edges are split, so that neighbours agree (watertight).
        std::unordered_map<uint64_t, int> split;
        for (const stl_triangle_vertex_indices &f : its.indices)
            for (int e = 0; e < 3; ++ e) {
                const int a = f[e], b = f[(e + 1) % 3];
                if ((its.vertices[a] - its.vertices[b]).cast<double>().squaredNorm() > max_edge2)
                    split.emplace(edge_key(a, b), -1);
            }
        if (split.empty())
            return;
        for (auto &[key, idx] : split) {
            const int a = int(key >> 32), b = int(key & 0xffffffffu);
            idx = int(its.vertices.size());
            its.vertices.emplace_back(0.5f * (its.vertices[a] + its.vertices[b]));
        }
        auto mid = [&](int a, int b) -> int {
            auto it = split.find(edge_key(a, b));
            return it == split.end() ? -1 : it->second;
        };
        // 2) Re-triangulate each face by the number of its split edges.
        std::vector<stl_triangle_vertex_indices> faces;
        faces.reserve(its.indices.size() * 2);
        for (const stl_triangle_vertex_indices &f : its.indices) {
            int v[3] = { f[0], f[1], f[2] };
            int m[3] = { mid(v[0], v[1]), mid(v[1], v[2]), mid(v[2], v[0]) };
            const int n = (m[0] >= 0) + (m[1] >= 0) + (m[2] >= 0);
            if (n == 0) {
                faces.push_back(f);
            } else if (n == 3) {
                faces.emplace_back(v[0], m[0], m[2]);
                faces.emplace_back(m[0], v[1], m[1]);
                faces.emplace_back(m[2], m[1], v[2]);
                faces.emplace_back(m[0], m[1], m[2]);
            } else {
                // Rotate so that the edge (p, q) is split.
                int r = 0;
                while (m[r] < 0)
                    ++ r;
                const int p = v[r], q = v[(r + 1) % 3], s = v[(r + 2) % 3];
                const int mpq = m[r], mqs = m[(r + 1) % 3], msp = m[(r + 2) % 3];
                if (n == 1) {
                    faces.emplace_back(p, mpq, s);
                    faces.emplace_back(mpq, q, s);
                } else if (mqs >= 0) {
                    faces.emplace_back(p, mpq, s);
                    faces.emplace_back(mpq, q, mqs);
                    faces.emplace_back(mpq, mqs, s);
                } else {
                    // split edges (p, q) and (s, p)
                    faces.emplace_back(p, mpq, msp);
                    faces.emplace_back(mpq, q, s);
                    faces.emplace_back(mpq, s, msp);
                }
            }
        }
        its.indices = std::move(faces);
    }
}

void deform_mesh(indexed_triangle_set &its, const Deformation &deformation, double max_edge)
{
    if (! deformation.enabled())
        return;
    subdivide_mesh(its, max_edge);
    for (stl_vertex &v : its.vertices)
        v.z() = float(deformation.to_slice_z(v.x(), v.y(), v.z()));
}

// ---------------------------------------------------------------- G-code

namespace {

struct Line
{
    std::string cmd;          // "G1", "M83" ... (uppercase), empty for comments
    double      value[26];
    bool        has[26];
    std::string comment;      // text after ';' (without it)

    bool   has_axis(char c) const { return has[c - 'A']; }
    double get(char c) const { return value[c - 'A']; }
};

Line parse_line(const std::string &raw)
{
    Line l;
    std::fill(std::begin(l.has), std::end(l.has), false);
    const size_t semi = raw.find(';');
    const std::string code = semi == std::string::npos ? raw : raw.substr(0, semi);
    if (semi != std::string::npos)
        l.comment = raw.substr(semi + 1);
    const char *c = code.c_str();
    bool first = true;
    while (*c) {
        while (*c == ' ' || *c == '\t')
            ++ c;
        if (! *c)
            break;
        const char letter = char(std::toupper(*c));
        if (letter < 'A' || letter > 'Z')
            break;
        ++ c;
        char *end = nullptr;
        const double v = std::strtod(c, &end);
        if (end == c)
            break;
        if (first) {
            if (letter == 'G' || letter == 'M' || letter == 'T')
                l.cmd = std::string(1, letter) + std::to_string(int(v));
            else
                break;
            first = false;
        } else {
            l.value[letter - 'A'] = v;
            l.has[letter - 'A']   = true;
        }
        c = end;
    }
    return l;
}

void append_number(std::string &out, double v, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (! s.empty() && s.back() == '0')
            s.pop_back();
        if (! s.empty() && s.back() == '.')
            s.pop_back();
    }
    if (s == "-0" || s.empty())
        s = "0";
    out += s;
}

} // namespace

GCodeFilter::GCodeFilter(const Deformation &deformation, const GCodeFilterParams &params) :
    m_deformation(deformation), m_params(params)
{
    m_filament_area = M_PI * 0.25 * m_params.filament_diameter * m_params.filament_diameter;
}

double GCodeFilter::real_z(double x, double y, double z) const
{
    const double zs = z - m_params.z_base;
    if (zs > m_params.top_slice_z)
        return z + m_params.top_lift;
    return m_deformation.to_real_z(x - m_params.origin.x(), y - m_params.origin.y(), zs) + m_params.z_base;
}

double GCodeFilter::target_flow() const
{
    if (m_params.flow_policy != FlowPolicy::Uniform || m_params.uniform_flow <= 0.)
        return 0.;
    for (const std::string &excluded : m_params.uniform_exclude)
        if (m_feature == excluded)
            return 0.;
    return m_params.uniform_flow;
}

double GCodeFilter::z_limit(double feed, double seg_xy, double seg3, double dz, double curvature)
{
    double cap = std::numeric_limits<double>::max();
    const double ratio = seg_xy > 0. ? seg3 / seg_xy : 1.;
    if (m_params.z_max_speed > 0. && std::abs(dz) > 1e-9)
        cap = std::min(cap, seg_xy > 0. ? m_params.z_max_speed * seg_xy / std::abs(dz) * ratio * 60. :
                                          m_params.z_max_speed * 60.);
    if (m_params.z_max_accel > 0. && curvature > 1e-9 && seg_xy > 0.)
        cap = std::min(cap, std::sqrt(m_params.z_max_accel / curvature) * ratio * 60.);
    // 2 % margin: the coordinates are rounded when written.
    cap *= 0.98;
    if (cap < feed) {
        ++ m_stats.z_limited;
        return cap;
    }
    return feed;
}

void GCodeFilter::emit_feed(double feed, std::string &out)
{
    // Rounded down, so that the speed / flow limits are strict.
    const long f = long(std::floor(feed + 1e-6));
    if (m_out_feed && *m_out_feed == f)
        return;
    m_out_feed = f;
    out += " F";
    out += std::to_string(f);
}

std::string GCodeFilter::process_layer(const std::string &gcode)
{
    std::string out;
    out.reserve(gcode.size() * 2);
    size_t start = 0;
    while (start < gcode.size()) {
        size_t end = gcode.find('\n', start);
        const bool has_newline = end != std::string::npos;
        if (! has_newline)
            end = gcode.size();
        this->process_line(gcode.substr(start, end - start), out);
        if (has_newline)
            out += '\n';
        start = end + 1;
    }
    return out;
}

void GCodeFilter::process_line(const std::string &line, std::string &out)
{
    const Line l = parse_line(line);
    if (l.cmd.empty()) {
        if (l.comment.rfind("TYPE:", 0) == 0)
            m_feature = l.comment.substr(5);
        out += line;
        ++ m_stats.lines_out;
        return;
    }
    if (l.cmd == "G90")
        m_absolute_xyz = true;
    else if (l.cmd == "G91")
        m_absolute_xyz = false;
    else if (l.cmd == "G92") {
        if (l.has_axis('X')) m_x = l.get('X');
        if (l.has_axis('Y')) m_y = l.get('Y');
        if (l.has_axis('Z')) m_z = l.get('Z');
    } else if (l.cmd == "G0" || l.cmd == "G1") {
        this->process_move(line, out);
        return;
    }
    out += line;
    ++ m_stats.lines_out;
}

void GCodeFilter::process_move(const std::string &line, std::string &out)
{
    const Line l = parse_line(line);
    if (l.has_axis('F'))
        m_feed = l.get('F');
    const double de = l.has_axis('E') ? l.get('E') : 0.;
    const bool has_xyz = l.has_axis('X') || l.has_axis('Y') || l.has_axis('Z');

    auto passthrough = [&]() {
        if (l.has_axis('F'))
            m_out_feed = long(std::floor(l.get('F') + 1e-6));
        out += line;
        ++ m_stats.lines_out;
    };

    if (! m_absolute_xyz) {
        // Relative moves only appear in custom start / end G-code: keep them.
        if (l.has_axis('X') && m_x) *m_x += l.get('X');
        if (l.has_axis('Y') && m_y) *m_y += l.get('Y');
        if (l.has_axis('Z')) m_z += l.get('Z');
        passthrough();
        return;
    }
    const std::optional<double> x0 = m_x, y0 = m_y;
    const double z0 = m_z;
    if (l.has_axis('X')) m_x = l.get('X');
    if (l.has_axis('Y')) m_y = l.get('Y');
    if (l.has_axis('Z')) m_z = l.get('Z');
    if (! has_xyz || ! m_x || ! m_y) {
        passthrough();
        return;
    }
    const std::string comment = l.comment.empty() ? std::string() : " ;" + l.comment;
    auto write_point = [&](double x, double y, double z) {
        out += "G1 X";
        append_number(out, x, 3);
        out += " Y";
        append_number(out, y, 3);
        out += " Z";
        append_number(out, z, 3);
    };
    if (! x0 || ! y0) {
        // Unknown origin (first move): only transform the target.
        write_point(*m_x, *m_y, this->real_z(*m_x, *m_y, m_z));
        if (de != 0.) {
            out += " E";
            append_number(out, de, 5);
        }
        this->emit_feed(m_feed, out);
        out += comment;
        ++ m_stats.lines_out;
        return;
    }

    ++ m_stats.moves;
    const double x1 = *m_x, y1 = *m_y, z1 = m_z;
    const double lxy = std::hypot(x1 - *x0, y1 - *y0);
    const size_t n   = lxy > 0. ? std::max<size_t>(1, size_t(std::ceil(lxy / m_params.seg_len))) : 1;

    std::vector<double> t(n + 1), xs(n + 1), ys(n + 1), zs(n + 1), zr(n + 1);
    for (size_t i = 0; i <= n; ++ i) {
        t[i]  = double(i) / double(n);
        xs[i] = *x0 + (x1 - *x0) * t[i];
        ys[i] = *y0 + (y1 - *y0) * t[i];
        zs[i] = z0 + (z1 - z0) * t[i];
        zr[i] = this->real_z(xs[i], ys[i], zs[i]);
    }
    const bool extruding = de > 0. && lxy > 0.;
    std::vector<double> J(n, 1.), curvature(n, 0.);
    for (size_t i = 0; i < n; ++ i) {
        const double xm = 0.5 * (xs[i] + xs[i + 1]), ym = 0.5 * (ys[i] + ys[i + 1]);
        const double zm_slice = 0.5 * (zs[i] + zs[i + 1]);
        if (extruding) {
            const double zm = 0.5 * (zr[i] + zr[i + 1]) - m_params.z_base;
            J[i] = m_deformation.jacobian(xm - m_params.origin.x(), ym - m_params.origin.y(), zm);
            m_stats.j_min = std::min(m_stats.j_min, J[i]);
            m_stats.j_max = std::max(m_stats.j_max, J[i]);
        }
        if (lxy > 0. && (m_params.z_max_accel > 0.)) {
            // |d2z/ds2| of the real path, by a central difference with h = 1 mm.
            const double h  = 1.;
            const double ux = (x1 - *x0) / lxy, uy = (y1 - *y0) / lxy;
            const double zc = this->real_z(xm, ym, zm_slice);
            const double zp = this->real_z(xm + h * ux, ym + h * uy, zm_slice);
            const double zn = this->real_z(xm - h * ux, ym - h * uy, zm_slice);
            curvature[i] = std::abs(zp - 2. * zc + zn) / (h * h);
        }
    }

    // Merge samples while the real Z stays linear (within z_tolerance) and J almost constant.
    std::vector<size_t> cuts { 0 };
    for (size_t a = 0; a < n;) {
        size_t b = a + 1;
        while (b < n) {
            const size_t c = b + 1;
            bool ok = true;
            for (size_t i = a; i <= c && ok; ++ i) {
                const double lin = zr[a] + (zr[c] - zr[a]) * (t[i] - t[a]) / (t[c] - t[a]);
                ok = std::abs(zr[i] - lin) <= m_params.z_tolerance;
            }
            if (ok && extruding) {
                double jmin = J[a], jmax = J[a], jsum = 0.;
                for (size_t i = a; i < c; ++ i) {
                    jmin = std::min(jmin, J[i]);
                    jmax = std::max(jmax, J[i]);
                    jsum += J[i];
                }
                ok = (jmax - jmin) <= 0.01 * jsum / double(c - a);
            }
            if (! ok)
                break;
            b = c;
        }
        cuts.push_back(b);
        a = b;
    }

    for (size_t k = 0; k + 1 < cuts.size(); ++ k) {
        const size_t a = cuts[k], b = cuts[k + 1];
        const double frac   = double(b - a) / double(n);
        const double seg_xy = lxy * frac;
        const double dz     = zr[b] - zr[a];
        const double seg3   = std::sqrt(seg_xy * seg_xy + dz * dz);
        write_point(xs[b], ys[b], zr[b]);
        double f = m_feed;
        if (extruding) {
            double j = 0.;
            for (size_t i = a; i < b; ++ i)
                j += J[i];
            j /= double(b - a);
            const double e_seg = de * frac * j;
            if (const double target = this->target_flow(); target > 0. && seg3 > 0.)
                f = target * seg3 * 60. / (e_seg * m_filament_area);
            else if (m_params.flow_policy != FlowPolicy::Off && seg_xy > 0.)
                f = m_feed * (seg3 / seg_xy) / j;
            if (m_params.max_volumetric > 0. && seg3 > 0.) {
                const double flow = e_seg * m_filament_area / (seg3 / (f / 60.));
                if (flow > m_params.max_volumetric)
                    f *= m_params.max_volumetric / flow;
            }
            if (m_params.max_feedrate > 0.)
                f = std::min(f, m_params.max_feedrate);
            f = std::max(f, m_params.min_feedrate);
            out += " E";
            append_number(out, e_seg, 5);
        } else if (de != 0.) {
            // Wipe with retraction: not scaled.
            out += " E";
            append_number(out, de * frac, 5);
        }
        double curv = 0.;
        for (size_t i = a; i < b; ++ i)
            curv = std::max(curv, curvature[i]);
        f = this->z_limit(f, seg_xy, seg3, dz, curv);
        this->emit_feed(f, out);
        if (k == 0)
            out += comment;
        if (k + 2 < cuts.size())
            out += '\n';
        ++ m_stats.lines_out;
    }
}

// ----------------------------------------------------------- From config

bool enabled(const PrintConfig &config)
{
    return config.nonplanar_mode.value != NonPlanarMode::Disabled;
}

static FieldParams field_params(const PrintConfig &config)
{
    FieldParams p;
    switch (config.nonplanar_mode.value) {
    case NonPlanarMode::Disabled: p.mode = Mode::Disabled; break;
    case NonPlanarMode::Wave:     p.mode = Mode::Wave;     break;
    case NonPlanarMode::Conical:  p.mode = Mode::Conical;  break;
    }
    switch (config.nonplanar_pattern.value) {
    case NonPlanarPattern::Egg:     p.pattern = Pattern::Egg;     break;
    case NonPlanarPattern::Ridges:  p.pattern = Pattern::Ridges;  break;
    case NonPlanarPattern::Twisted: p.pattern = Pattern::Twisted; break;
    }
    p.amplitude        = config.nonplanar_amplitude.value;
    p.wavelength       = std::max(1., config.nonplanar_wavelength.value);
    p.angle_deg        = config.nonplanar_angle.value;
    p.twist_deg_per_mm = config.nonplanar_twist.value;
    p.cone_angle_deg   = config.nonplanar_cone_angle.value;
    return p;
}

Deformation make_deformation(const PrintConfig &config, const BoundingBoxf3 &object_bbox)
{
    FieldParams field = field_params(config);
    if (object_bbox.defined)
        field.center = 0.5 * (object_bbox.min.head<2>() + object_bbox.max.head<2>());
    Ramp ramp;
    ramp.z_flat     = config.nonplanar_flat_below.value;
    ramp.z_ramp     = std::max(0.1, config.nonplanar_ramp_height.value);
    ramp.z_ramp_top = ramp.z_ramp;
    if (config.nonplanar_flat_top.value && object_bbox.defined)
        ramp.z_top = object_bbox.max.z();
    return Deformation(field, ramp);
}

double mesh_max_edge(const PrintConfig &config)
{
    if (config.nonplanar_mode.value == NonPlanarMode::Wave)
        return std::clamp(config.nonplanar_wavelength.value / 10., 0.3, 1.5);
    return 2.;
}

GCodeFilterParams make_filter_params(const PrintConfig &config)
{
    GCodeFilterParams p;
    p.seg_len           = std::max(0.05, config.nonplanar_segment_length.value);
    p.filament_diameter = config.filament_diameter.values.empty() ? 1.75 : config.filament_diameter.values.front();
    if (config.machine_limits_usage.value != MachineLimitsUsage::Ignore) {
        if (! config.machine_max_feedrate_z.values.empty())
            p.z_max_speed = config.machine_max_feedrate_z.values.front();
        if (! config.machine_max_acceleration_z.values.empty())
            p.z_max_accel = config.machine_max_acceleration_z.values.front();
    }
    double max_volumetric = config.max_volumetric_speed.value;
    if (! config.filament_max_volumetric_speed.values.empty() && config.filament_max_volumetric_speed.values.front() > 0.)
        max_volumetric = max_volumetric > 0. ? std::min(max_volumetric, config.filament_max_volumetric_speed.values.front()) :
                                               config.filament_max_volumetric_speed.values.front();
    p.max_volumetric = max_volumetric;
    switch (config.nonplanar_flow_policy.value) {
    case NonPlanarFlowPolicy::Preserve: p.flow_policy = FlowPolicy::Preserve; break;
    case NonPlanarFlowPolicy::Uniform:  p.flow_policy = FlowPolicy::Uniform;  break;
    case NonPlanarFlowPolicy::Off:      p.flow_policy = FlowPolicy::Off;      break;
    }
    p.uniform_flow = config.nonplanar_uniform_flow.value;
    if (p.flow_policy == FlowPolicy::Uniform && p.uniform_flow <= 0.)
        // Automatic: 80 % of what the hotend melts, if known, otherwise keep PrusaSlicer's flow.
        p.uniform_flow = max_volumetric > 0. ? 0.8 * max_volumetric : 0.;
    return p;
}

} // namespace NonPlanar
} // namespace Slic3r
