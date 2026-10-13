#include "NonPlanar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>

#include <admesh/stl.h>

#include "libslic3r/Engineering.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

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

std::pair<double, double> FieldParams::slope_test_tangent(double x) const
{
    const double band = std::max(1., slope_band);
    const double t    = (x - slope_x0) / band;
    const int    i    = int(std::floor(t));
    auto tangent = [this](int band_idx) {
        const double lo = std::min(slope_start_deg, slope_end_deg), hi = std::max(slope_start_deg, slope_end_deg);
        const double deg = std::clamp(slope_start_deg + std::max(0, band_idx) * slope_step_deg, lo, hi);
        return std::tan(std::clamp(deg, 0., 80.) * M_PI / 180.);
    };
    // Constant inside the band, smooth transition over the last 20 % of it.
    const double f  = t - i;
    const double s  = std::clamp((f - 0.8) / 0.2, 0., 1.);
    const double w  = s * s * (3. - 2. * s);
    const double dw = (s > 0. && s < 1.) ? 6. * s * (1. - s) / (0.2 * band) : 0.;
    const double a = tangent(i), b = tangent(i + 1);
    return { a + (b - a) * w, (b - a) * dw };
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
    case Mode::SlopeTest: {
        // Ridges along X: amplitude = tangent / k gives the tangent as the steepest slope.
        const double k = TWO_PI / m_params.wavelength;
        return m_params.slope_test_tangent(x).first / k * std::sin(k * dy);
    }
    case Mode::Overhangs:
        // The layers sink towards the overhangs.
        return m_params.overhangs ? - m_params.overhang_scale * m_params.overhangs->sink(x, y, z) : 0.;
    case Mode::Stress:
    case Mode::Surface:
        return m_params.layers ? m_params.layers_scale * m_params.layers->offset(x, y, z) : 0.;
    case Mode::Automatic:
        return (m_params.overhangs ? - m_params.overhang_scale * m_params.overhangs->sink(x, y, z) : 0.) +
               (m_params.layers ? m_params.layers_scale * m_params.layers->offset(x, y, z) : 0.);
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
    case Mode::SlopeTest: {
        const double k = TWO_PI / m_params.wavelength;
        const auto [tangent, dtangent] = m_params.slope_test_tangent(x);
        return { dtangent / k * std::sin(k * dy), tangent * std::cos(k * dy) };
    }
    case Mode::Overhangs:
        return m_params.overhangs ? Vec2d(- m_params.overhang_scale * m_params.overhangs->sink_grad(x, y, z)) : Vec2d::Zero();
    case Mode::Stress:
    case Mode::Surface:
        return m_params.layers ? Vec2d(m_params.layers_scale * m_params.layers->grad(x, y, z)) : Vec2d::Zero();
    case Mode::Automatic:
        return (m_params.overhangs ? Vec2d(- m_params.overhang_scale * m_params.overhangs->sink_grad(x, y, z)) : Vec2d::Zero()) +
               (m_params.layers ? Vec2d(m_params.layers_scale * m_params.layers->grad(x, y, z)) : Vec2d::Zero());
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
    if (m_params.mode == Mode::Automatic)
        return (m_params.overhangs ? - m_params.overhang_scale * m_params.overhangs->sink_dz(x, y, z) : 0.) +
               (m_params.layers ? m_params.layers_scale * m_params.layers->dz(x, y, z) : 0.);
    if (m_params.mode == Mode::Overhangs)
        return m_params.overhangs ? - m_params.overhang_scale * m_params.overhangs->sink_dz(x, y, z) : 0.;
    if (m_params.mode == Mode::Stress || m_params.mode == Mode::Surface)
        return m_params.layers ? m_params.layers_scale * m_params.layers->dz(x, y, z) : 0.;
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
    if (m_params.mode == Mode::SlopeTest)
        return m_params.slope_test_tangent(x).first / (TWO_PI / m_params.wavelength);
    if (m_params.mode == Mode::Automatic)
        return (m_params.overhangs ? m_params.overhang_scale * m_params.overhangs->bound(x, y) : 0.) +
               (m_params.layers ? std::abs(m_params.layers_scale) * m_params.layers->bound(x, y) : 0.);
    if (m_params.mode == Mode::Overhangs)
        return m_params.overhangs ? m_params.overhang_scale * m_params.overhangs->bound(x, y) : 0.;
    if (m_params.mode == Mode::Stress || m_params.mode == Mode::Surface)
        return m_params.layers ? std::abs(m_params.layers_scale) * m_params.layers->bound(x, y) : 0.;
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

Vec3d Deformation::layer_normal(double x, double y, double z) const
{
    if (! this->enabled())
        return Vec3d::UnitZ();
    const Vec2d g = m_field.grad(x, y, z) * m_ramp.value(z);
    const Vec3d n(- g.x(), - g.y(), this->dzs_dz(x, y, z));
    return n.norm() > 1e-12 ? Vec3d(n.normalized()) : Vec3d(Vec3d::UnitZ());
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

double Deformation::head_slope_ratio(const BoundingBoxf3 &bbox, double tan_x, double tan_y) const
{
    if (! this->enabled() || ! bbox.defined || tan_x <= 0. || tan_y <= 0.)
        return 0.;
    constexpr int N = 32;
    double out = 0.;
    for (int i = 0; i <= N; ++ i)
        for (int j = 0; j <= N; ++ j)
            for (int k = 0; k <= N; ++ k) {
                const double x = bbox.min.x() + (bbox.max.x() - bbox.min.x()) * i / N;
                const double y = bbox.min.y() + (bbox.max.y() - bbox.min.y()) * j / N;
                const double z = bbox.min.z() + (bbox.max.z() - bbox.min.z()) * k / N;
                const Vec2d  g = m_field.grad(x, y, z) * m_ramp.value(z);
                out = std::max(out, std::hypot(g.x() / tan_x, g.y() / tan_y));
            }
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

// -------------------------------------------------------------------- Print head collisions

double HeadClearance::allowed_rise(double distance) const
{
    if (this->profile.empty())
        return this->height;
    // Each measured ramp was free: rising at its angle up to its height, then flat (the plateau behind it).
    double out = 0.;
    for (const auto &[h, angle_deg] : this->profile) {
        const double t     = std::tan(std::clamp(angle_deg, 0.1, 89.) * M_PI / 180.);
        const double reach = h / t;
        out = std::max(out, distance <= reach ? distance * t : h);
    }
    return out;
}

std::vector<std::pair<double, double>> parse_head_profile(const std::string &text)
{
    std::vector<std::pair<double, double>> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string item = text.substr(start, end - start);
        if (const size_t colon = item.find(':'); colon != std::string::npos) {
            char *e1 = nullptr, *e2 = nullptr;
            const std::string hs = item.substr(0, colon), as = item.substr(colon + 1);
            const double h = std::strtod(hs.c_str(), &e1);
            const double a = std::strtod(as.c_str(), &e2);
            if (e1 != hs.c_str() && e2 != as.c_str() && h > 0. && a > 0. && a < 90.)
                out.emplace_back(h, a);
        }
        start = end + 1;
    }
    std::sort(out.begin(), out.end());
    return out;
}

HeadClearance head_clearance(const PrintConfig &config)
{
    HeadClearance head{ config.nonplanar_head_clearance_height.value, config.nonplanar_head_clearance_radius.value };
    head.profile = parse_head_profile(config.nonplanar_head_profile.value);
    return head;
}

HeadCollision check_head_collision(const Deformation &deformation, const indexed_triangle_set &mesh, const HeadClearance &head)
{
    HeadCollision out;
    if (! deformation.enabled() || (head.height <= 0. && head.profile.empty()) || head.radius <= 0. || mesh.vertices.empty())
        return out;

    double z_min = std::numeric_limits<double>::max();
    double z_max = std::numeric_limits<double>::lowest();
    for (const stl_vertex &v : mesh.vertices) {
        z_min = std::min(z_min, double(v.z()));
        z_max = std::max(z_max, double(v.z()));
    }

    constexpr int LEVELS = 24; // printing heights
    constexpr int GRID   = 24; // nozzle positions per height
    constexpr int RINGS  = 6;  // samples around the nozzle
    constexpr int SPOKES = 24;
    double worst = -std::numeric_limits<double>::max();

    for (int level = 1; level <= LEVELS; ++ level) {
        const double s = z_min + (z_max - z_min) * level / LEVELS;
        // Cross section at s (where the nozzle prints) and everything below s (already printed).
        Points section, printed;
        for (const stl_vertex &v : mesh.vertices)
            if (v.z() <= s)
                printed.emplace_back(scaled<coord_t>(v.x()), scaled<coord_t>(v.y()));
        for (const stl_triangle_vertex_indices &f : mesh.indices)
            for (int i = 0; i < 3; ++ i) {
                const stl_vertex &a = mesh.vertices[f[i]];
                const stl_vertex &b = mesh.vertices[f[(i + 1) % 3]];
                if ((a.z() - s) * (b.z() - s) <= 0. && a.z() != b.z()) {
                    const double t = (s - a.z()) / (b.z() - a.z());
                    const Point pt(scaled<coord_t>(a.x() + t * (b.x() - a.x())), scaled<coord_t>(a.y() + t * (b.y() - a.y())));
                    section.emplace_back(pt);
                    printed.emplace_back(pt);
                }
            }
        if (section.size() < 3)
            continue;
        const Polygon section_hull = Geometry::convex_hull(std::move(section));
        const Polygon printed_hull = Geometry::convex_hull(std::move(printed));
        if (section_hull.size() < 3)
            continue;
        const BoundingBox bb = section_hull.bounding_box();
        for (int i = 0; i <= GRID; ++ i)
            for (int j = 0; j <= GRID; ++ j) {
                const Point pp(bb.min.x() + coord_t(double(bb.max.x() - bb.min.x()) * i / GRID),
                               bb.min.y() + coord_t(double(bb.max.y() - bb.min.y()) * j / GRID));
                if (! section_hull.contains(pp))
                    continue;
                const Vec2d  p      = unscaled(pp);
                const double z_tip  = deformation.offset(p.x(), p.y(), s);
                for (int ring = 1; ring <= RINGS; ++ ring) {
                    const double r = head.radius * ring / RINGS;
                    for (int spoke = 0; spoke < SPOKES; ++ spoke) {
                        const double a = TWO_PI * spoke / SPOKES;
                        const Vec2d  q = p + r * Vec2d(std::cos(a), std::sin(a));
                        if (! printed_hull.contains(Point(scaled<coord_t>(q.x()), scaled<coord_t>(q.y()))))
                            continue;
                        // Top of the part printed so far at q: the current layer surface.
                        const double rise = deformation.offset(q.x(), q.y(), s) - z_tip;
                        const double over = rise - head.allowed_rise(r);
                        if (over > worst) {
                            worst        = over;
                            out.rise     = rise;
                            out.distance = r;
                            out.nozzle   = Vec3d(p.x(), p.y(), s + z_tip);
                        }
                    }
                }
            }
    }
    out.collides = worst > 0.;
    return out;
}


// -------------------------------------------------------------------- Overhangs (Tisma)

double OverhangField::Zone::weight(double z) const
{
    const double in  = z_in1 > z_in0 ? smoothstep((z - z_in0) / (z_in1 - z_in0)) : (z >= z_in1 ? 1. : 0.);
    const double out = z_out1 > z_out0 ? 1. - smoothstep((z - z_out0) / (z_out1 - z_out0)) : (z <= z_out0 ? 1. : 0.);
    return in * out;
}

double OverhangField::Zone::dweight(double z) const
{
    const double t_in = z_in1 > z_in0 ? (z - z_in0) / (z_in1 - z_in0) : (z >= z_in1 ? 1. : 0.);
    const double t_out = z_out1 > z_out0 ? (z - z_out0) / (z_out1 - z_out0) : (z <= z_out0 ? 0. : 1.);
    const double in   = smoothstep(t_in);
    const double din  = z_in1 > z_in0 ? dsmoothstep(t_in) / (z_in1 - z_in0) : 0.;
    const double out  = 1. - smoothstep(t_out);
    const double dout = z_out1 > z_out0 ? - dsmoothstep(t_out) / (z_out1 - z_out0) : 0.;
    return din * out + in * dout;
}

std::pair<double, Vec2d> OverhangField::sample(const Zone &zone, double x, double y) const
{
    if (nx < 2 || ny < 2)
        return { 0., Vec2d::Zero() };
    // Outside of the grid the nearest edge value (no gradient across).
    const double u = std::clamp((x - origin.x()) / cell, 0., double(nx - 1));
    const double v = std::clamp((y - origin.y()) / cell, 0., double(ny - 1));
    const int    i = std::min(int(u), nx - 2);
    const int    j = std::min(int(v), ny - 2);
    const double fu = u - i, fv = v - j;
    const double f00 = zone.sink[j * nx + i],       f10 = zone.sink[j * nx + i + 1];
    const double f01 = zone.sink[(j + 1) * nx + i], f11 = zone.sink[(j + 1) * nx + i + 1];
    const double value = (1. - fv) * ((1. - fu) * f00 + fu * f10) + fv * ((1. - fu) * f01 + fu * f11);
    const double inside_x = (x - origin.x()) / cell > 0. && (x - origin.x()) / cell < double(nx - 1) ? 1. : 0.;
    const double inside_y = (y - origin.y()) / cell > 0. && (y - origin.y()) / cell < double(ny - 1) ? 1. : 0.;
    const Vec2d grad(inside_x * ((1. - fv) * (f10 - f00) + fv * (f11 - f01)) / cell,
                     inside_y * ((1. - fu) * (f01 - f00) + fu * (f11 - f10)) / cell);
    return { value, grad };
}

double OverhangField::sink(double x, double y, double z) const
{
    double out = 0.;
    for (const Zone &zone : zones)
        if (const double w = zone.weight(z); w > 0.)
            out += w * this->sample(zone, x, y).first;
    return out;
}

Vec2d OverhangField::sink_grad(double x, double y, double z) const
{
    Vec2d out = Vec2d::Zero();
    for (const Zone &zone : zones)
        if (const double w = zone.weight(z); w > 0.)
            out += w * this->sample(zone, x, y).second;
    return out;
}

double OverhangField::sink_dz(double x, double y, double z) const
{
    double out = 0.;
    for (const Zone &zone : zones)
        if (const double dw = zone.dweight(z); dw != 0.)
            out += dw * this->sample(zone, x, y).first;
    return out;
}

double OverhangField::bound(double x, double y) const
{
    double out = 0.;
    for (const Zone &zone : zones)
        out += this->sample(zone, x, y).first;
    return out;
}

LayerGrid::LayerGrid(const EngineeringLayerField &field, const Vec3d &part_min) :
    m_origin(part_min + field.origin), m_cell(std::max(1e-3, field.cell)), m_nx(field.nx), m_ny(field.ny), m_nz(field.nz),
    m_offset(field.offset)
{
    if (m_offset.size() != size_t(m_nx) * size_t(m_ny) * size_t(m_nz) || m_nx < 2 || m_ny < 2 || m_nz < 2) {
        m_offset.clear();
        m_nx = m_ny = m_nz = 0;
        return;
    }
    m_column_max.assign(size_t(m_nx) * m_ny, 0.f);
    for (int k = 0; k < m_nz; ++ k)
        for (int j = 0; j < m_ny; ++ j)
            for (int i = 0; i < m_nx; ++ i) {
                float &m = m_column_max[size_t(j) * m_nx + i];
                m = std::max(m, std::abs(float(this->node(i, j, k))));
            }
}

bool LayerGrid::locate(double x, double y, double z, int &i, int &j, int &k, double &tx, double &ty, double &tz) const
{
    if (m_offset.empty())
        return false;
    const double fx = (x - m_origin.x()) / m_cell, fy = (y - m_origin.y()) / m_cell, fz = (z - m_origin.z()) / m_cell;
    if (fx < 0. || fy < 0. || fz < 0. || fx > m_nx - 1 || fy > m_ny - 1 || fz > m_nz - 1)
        return false;
    i  = std::min(int(fx), m_nx - 2);
    j  = std::min(int(fy), m_ny - 2);
    k  = std::min(int(fz), m_nz - 2);
    tx = fx - i;
    ty = fy - j;
    tz = fz - k;
    return true;
}

double LayerGrid::offset(double x, double y, double z) const
{
    int i, j, k;
    double tx, ty, tz;
    if (! this->locate(x, y, z, i, j, k, tx, ty, tz))
        return 0.;
    double v = 0.;
    for (int c = 0; c < 8; ++ c) {
        const int a = c & 1, b = (c >> 1) & 1, d = c >> 2;
        v += (a ? tx : 1. - tx) * (b ? ty : 1. - ty) * (d ? tz : 1. - tz) * this->node(i + a, j + b, k + d);
    }
    return v;
}

Vec2d LayerGrid::grad(double x, double y, double z) const
{
    int i, j, k;
    double tx, ty, tz;
    if (! this->locate(x, y, z, i, j, k, tx, ty, tz))
        return Vec2d::Zero();
    Vec2d g = Vec2d::Zero();
    for (int c = 0; c < 8; ++ c) {
        const int a = c & 1, b = (c >> 1) & 1, d = c >> 2;
        const double v  = this->node(i + a, j + b, k + d);
        const double wz = d ? tz : 1. - tz;
        g.x() += (a ? 1. : -1.) * (b ? ty : 1. - ty) * wz * v;
        g.y() += (a ? tx : 1. - tx) * (b ? 1. : -1.) * wz * v;
    }
    return g / m_cell;
}

double LayerGrid::dz(double x, double y, double z) const
{
    int i, j, k;
    double tx, ty, tz;
    if (! this->locate(x, y, z, i, j, k, tx, ty, tz))
        return 0.;
    double g = 0.;
    for (int c = 0; c < 8; ++ c) {
        const int a = c & 1, b = (c >> 1) & 1, d = c >> 2;
        g += (a ? tx : 1. - tx) * (b ? ty : 1. - ty) * (d ? 1. : -1.) * this->node(i + a, j + b, k + d);
    }
    return g / m_cell;
}

double LayerGrid::bound(double x, double y) const
{
    if (m_offset.empty())
        return 0.;
    const int i = int(std::floor((x - m_origin.x()) / m_cell)), j = int(std::floor((y - m_origin.y()) / m_cell));
    double m = 0.;
    for (int jj = j; jj <= j + 1; ++ jj)
        for (int ii = i; ii <= i + 1; ++ ii)
            if (ii >= 0 && jj >= 0 && ii < m_nx && jj < m_ny)
                m = std::max(m, double(m_column_max[size_t(jj) * m_nx + ii]));
    return m;
}

namespace {

// Exact squared distance transform of one row / column (Felzenszwalb & Huttenlocher, "Distance Transforms of Sampled
// Functions", Theory of Computing 8, 2012) with the samples `step` apart.
void distance_transform_1d(const std::vector<double> &f, double step, std::vector<double> &d,
                           std::vector<int> &v, std::vector<double> &z)
{
    const int n = int(f.size());
    v.assign(n, 0);
    z.assign(n + 1, 0.);
    int k = 0;
    z[0] = - std::numeric_limits<double>::infinity();
    z[1] =   std::numeric_limits<double>::infinity();
    auto pos = [step](int q) { return step * q; };
    auto intersection = [&](int q, int p) {
        return ((f[q] + pos(q) * pos(q)) - (f[p] + pos(p) * pos(p))) / (2. * (pos(q) - pos(p)));
    };
    for (int q = 1; q < n; ++ q) {
        // z[0] is -infinity: the loop stops at k = 0 at the latest.
        double s = intersection(q, v[k]);
        while (s <= z[k])
            s = intersection(q, v[-- k]);
        ++ k;
        v[k]     = q;
        z[k]     = s;
        z[k + 1] = std::numeric_limits<double>::infinity();
    }
    d.assign(n, 0.);
    k = 0;
    for (int q = 0; q < n; ++ q) {
        while (z[k + 1] < pos(q))
            ++ k;
        const double dq = pos(q) - pos(v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

// Nodes of the grid inside the expolygons (even-odd along each row of nodes).
std::vector<char> rasterize(const ExPolygons &expolygons, const Vec2d &origin, double cell, int nx, int ny)
{
    std::vector<char> out(size_t(nx) * size_t(ny), 0);
    std::vector<std::pair<Vec2d, Vec2d>> edges;
    auto add = [&edges](const Polygon &polygon) {
        for (size_t i = 0; i < polygon.points.size(); ++ i)
            edges.emplace_back(unscaled(polygon.points[i]), unscaled(polygon.points[(i + 1) % polygon.points.size()]));
    };
    for (const ExPolygon &expolygon : expolygons) {
        add(expolygon.contour);
        for (const Polygon &hole : expolygon.holes)
            add(hole);
    }
    std::vector<double> xs;
    for (int j = 0; j < ny; ++ j) {
        const double y = origin.y() + cell * j;
        xs.clear();
        for (const auto &[a, b] : edges)
            if ((a.y() <= y) != (b.y() <= y))
                xs.emplace_back(a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y()));
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            const int i0 = std::max(0, int(std::ceil((xs[k] - origin.x()) / cell)));
            const int i1 = std::min(nx - 1, int(std::floor((xs[k + 1] - origin.x()) / cell)));
            for (int i = i0; i <= i1; ++ i)
                out[size_t(j) * nx + i] = 1;
        }
    }
    return out;
}

// Squared distance of each node to the nearest seed, along X `step_x` and along Y `step_y` per node (exact).
std::vector<double> distance_transform(const std::vector<char> &seed, int nx, int ny, double step_x, double step_y)
{
    constexpr double FAR = 1e20;
    std::vector<double> dist(size_t(nx) * ny), row, out_row, z;
    std::vector<int> v;
    for (int j = 0; j < ny; ++ j) {
        row.resize(nx);
        for (int i = 0; i < nx; ++ i)
            row[i] = seed[size_t(j) * nx + i] ? 0. : FAR;
        distance_transform_1d(row, step_x, out_row, v, z);
        std::copy(out_row.begin(), out_row.end(), dist.begin() + size_t(j) * nx);
    }
    for (int i = 0; i < nx; ++ i) {
        row.resize(ny);
        for (int j = 0; j < ny; ++ j)
            row[j] = dist[size_t(j) * nx + i];
        distance_transform_1d(row, step_y, out_row, v, z);
        for (int j = 0; j < ny; ++ j)
            dist[size_t(j) * nx + i] = out_row[j];
    }
    return dist;
}

// Removes the nodes closer than `distance` to the outside (round in every direction); unchanged if nothing remains.
void erode(std::vector<char> &inside, int nx, int ny, double cell, double distance)
{
    std::vector<char> outside(inside.size());
    for (size_t i = 0; i < inside.size(); ++ i)
        outside[i] = ! inside[i];
    if (std::find(outside.begin(), outside.end(), 1) == outside.end())
        return;
    const std::vector<double> dist = distance_transform(outside, nx, ny, cell, cell);
    std::vector<char> out = inside;
    for (size_t i = 0; i < out.size(); ++ i)
        if (out[i] && dist[i] < distance * distance)
            out[i] = 0;
    if (std::find(out.begin(), out.end(), 1) != out.end())
        inside.swap(out);
}

// Average over the 3 x 3 neighbourhood (does not make the field steeper).
void smooth(std::vector<float> &f, int nx, int ny)
{
    std::vector<float> tmp(f.size());
    for (int j = 0; j < ny; ++ j)
        for (int i = 0; i < nx; ++ i) {
            double sum = 0.;
            for (int dj = -1; dj <= 1; ++ dj)
                for (int di = -1; di <= 1; ++ di)
                    sum += f[size_t(std::clamp(j + dj, 0, ny - 1)) * nx + std::clamp(i + di, 0, nx - 1)];
            tmp[size_t(j) * nx + i] = float(sum / 9.);
        }
    f.swap(tmp);
}

struct OverhangFacet
{
    double z_min, z_max, angle_deg;
    Vec2d  points[4];   // vertices and centroid
};

} // namespace

std::shared_ptr<const OverhangField> build_overhang_field(const indexed_triangle_set &mesh, const OverhangParams &params)
{
    if (mesh.indices.empty() || params.tan_x <= 0. || params.tan_y <= 0.)
        return nullptr;

    // Steepest slope the print head allows anywhere around the nozzle: the part already printed rises by the slope
    // times the distance, which must stay below what the head allows at that distance (heater block, cooling duct).
    double tan_head = std::numeric_limits<double>::max();
    if (params.head.radius > 0. && (params.head.height > 0. || ! params.head.profile.empty()))
        for (int i = 1; i <= 64; ++ i) {
            const double r = params.head.radius * i / 64.;
            tan_head = std::min(tan_head, params.head.allowed_rise(r) / r);
        }
    // Margin for the interpolation between the nodes of the grid.
    constexpr double SAFETY = 0.9;
    const double tan_x = SAFETY * std::min(params.tan_x, tan_head);
    const double tan_y = SAFETY * std::min(params.tan_y, tan_head);
    if (tan_x <= 1e-3 || tan_y <= 1e-3)
        return nullptr;

    // Sinking the layers by tan(b) per mm shears an overhang in the slice space (where the supports are generated): the
    // cotangent of its angle from the vertical grows by tan(b). An overhang is tilted only if the print head allows
    // enough to bring it below the printable angle: a partial tilt would leave its supports and make them taller.
    const auto cot = [](double deg) { return 1. / std::tan(std::clamp(deg, 1., 89.) * M_PI / 180.); };
    // Aim 5 degrees below the printable angle, accept 2 degrees below.
    const double cot_aim = cot(params.threshold_deg - 5.), cot_accept = cot(params.threshold_deg - 2.);

    // Overhangs steeper than the threshold (from the vertical) that the head can fix, except ceilings and what lies on
    // the bed.
    BoundingBoxf3 bbox;
    for (const stl_vertex &v : mesh.vertices)
        bbox.merge(v.cast<double>());
    std::vector<OverhangFacet> facets;
    for (const stl_triangle_vertex_indices &f : mesh.indices) {
        const Vec3d a = mesh.vertices[f[0]].cast<double>(), b = mesh.vertices[f[1]].cast<double>(), c = mesh.vertices[f[2]].cast<double>();
        const Vec3d n = (b - a).cross(c - a);
        const double len = n.norm();
        if (len < 1e-12 || n.z() >= 0.)
            continue;
        const double angle = std::asin(std::clamp(- n.z() / len, 0., 1.)) * 180. / M_PI;
        if (angle <= params.threshold_deg || angle > params.ceiling_deg)
            continue;
        OverhangFacet of;
        of.z_min = std::min({ a.z(), b.z(), c.z() });
        of.z_max = std::max({ a.z(), b.z(), c.z() });
        if (of.z_min <= bbox.min.z() + params.z_flat)
            continue;
        // Slope the head allows towards the overhang (the elliptic cone of the free angles along X and Y).
        const Vec2d dir = n.head<2>().normalized();
        const double tan_dir = 1. / std::hypot(dir.x() / tan_x, dir.y() / tan_y);
        if (cot(angle) + tan_dir < cot_accept)
            continue;
        of.angle_deg = angle;
        of.points[0] = a.head<2>();
        of.points[1] = b.head<2>();
        of.points[2] = c.head<2>();
        of.points[3] = (a + b + c).head<2>() / 3.;
        facets.emplace_back(of);
    }
    if (facets.empty())
        return nullptr;
    std::sort(facets.begin(), facets.end(), [](const OverhangFacet &l, const OverhangFacet &r) { return l.z_min < r.z_min; });

    auto field = std::make_shared<OverhangField>();
    const double margin = 2.;
    const double extent = std::max(bbox.max.x() - bbox.min.x(), bbox.max.y() - bbox.min.y()) + 2. * margin;
    field->cell   = std::max(0.25, extent / 500.);
    field->origin = bbox.min.head<2>() - Vec2d(margin, margin);
    field->nx     = int(std::ceil((bbox.max.x() - bbox.min.x() + 2. * margin) / field->cell)) + 1;
    field->ny     = int(std::ceil((bbox.max.y() - bbox.min.y() + 2. * margin) / field->cell)) + 1;
    const int nx = field->nx, ny = field->ny;

    // Group the facets by height: overhangs closer than this share one zone.
    constexpr double MERGE_GAP = 2.;
    // Layer thickness while the zone comes in (thinner) and goes out (thicker), within J_MIN..J_MAX with a margin:
    // the steepest smoothstep is 1.5 / its length.
    constexpr double DZ_IN = 0.55, DZ_OUT = 0.25;
    for (size_t begin = 0; begin < facets.size();) {
        size_t end = begin + 1;
        double z_max = facets[begin].z_max;
        while (end < facets.size() && facets[end].z_min <= z_max + MERGE_GAP) {
            z_max = std::max(z_max, facets[end].z_max);
            ++ end;
        }
        OverhangField::Zone zone;
        zone.z_min = facets[begin].z_min;
        zone.z_max = z_max;
        for (size_t i = begin; i < end; ++ i)
            zone.steepest_deg = std::max(zone.steepest_deg, facets[i].angle_deg);
        // The layers lean only as much as the steepest overhang of the zone needs, if the head allows it.
        const double need = std::max(0., cot_aim - cot(zone.steepest_deg));
        const double tx = std::min(tan_x, need), ty = std::min(tan_y, need);
        if (tx <= 1e-3 || ty <= 1e-3) {
            begin = end;
            continue;
        }

        // The part already printed where the overhangs start: the layers stay flat there.
        const std::vector<ExPolygons> sections = slice_mesh_ex(mesh, { float(zone.z_min - 0.05) }, MeshSlicingParamsEx{});
        std::vector<char> inside = sections.empty() ? std::vector<char>() : rasterize(sections.front(), field->origin, field->cell, nx, ny);
        if (std::find(inside.begin(), inside.end(), 1) == inside.end()) {
            begin = end;
            continue;
        }
        // The smoothing below rounds the edge of the flat part over two nodes on each side: start the slope that much
        // inside, so that it is full where the overhang starts.
        constexpr int SMOOTH_NODES = 2;
        erode(inside, nx, ny, field->cell, SMOOTH_NODES * field->cell);
        // Distance from it in the metric of the print head: one unit is the rise allowed along X and Y, so the sink
        // is as steep as the head allows in every direction ((gx / tx)^2 + (gy / ty)^2 = 1).
        std::vector<double> dist = distance_transform(inside, nx, ny, tx * field->cell, ty * field->cell);
        for (double &d : dist)
            d = std::sqrt(d);
        zone.sink.assign(dist.begin(), dist.end());
        // Down to the farthest point of the overhangs of the zone (and the smoothing beyond it), flat further out.
        double deepest = 0.;
        for (size_t i = begin; i < end; ++ i)
            for (const Vec2d &p : facets[i].points)
                deepest = std::max(deepest, field->sample(zone, p.x(), p.y()).first);
        deepest += SMOOTH_NODES * field->cell * std::max(tx, ty);
        for (float &value : zone.sink)
            value = std::min(value, float(deepest));
        smooth(zone.sink, nx, ny);
        smooth(zone.sink, nx, ny);
        // Brought in below the overhangs and taken out above them; less deep if there is no room above the flat
        // first layers.
        double length_in  = 1.5 * deepest / DZ_IN;
        double length_out = 1.5 * deepest / DZ_OUT;
        const double room = zone.z_min - (bbox.min.z() + params.z_flat);
        if (deepest <= 1e-3 || room <= 0.) {
            begin = end;
            continue;
        }
        if (length_in > room) {
            const double s = room / length_in;
            // Not enough room to bring in a tilt that fixes the overhangs: leave them flat (with their supports).
            if (s * std::min(tx, ty) < cot_accept - cot(zone.steepest_deg)) {
                begin = end;
                continue;
            }
            for (float &value : zone.sink)
                value *= float(s);
            length_in  *= s;
            length_out *= s;
        }
        zone.z_in0  = zone.z_min - length_in;
        zone.z_in1  = zone.z_min;
        zone.z_out0 = zone.z_max;
        zone.z_out1 = zone.z_max + length_out;
        field->zones.emplace_back(std::move(zone));
        begin = end;
    }
    return field->empty() ? nullptr : field;
}

indexed_triangle_set object_mesh(const std::vector<ModelVolume*> &volumes, const Transform3d &trafo)
{
    indexed_triangle_set out;
    for (const ModelVolume *volume : volumes)
        if (volume->is_model_part()) {
            indexed_triangle_set part = volume->mesh().its;
            its_transform(part, trafo * volume->get_matrix());
            its_merge(out, part);
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
    double z_max_speed = m_params.z_max_speed;
    if (m_params.z_speed_test_band > 0.) {
        // Calibration: the Z speed allowed in the band of the current height.
        const int    band = std::max(0, int(std::floor((m_z - m_params.z_base - m_params.z_speed_test_offset) / m_params.z_speed_test_band)));
        const double lo   = std::min(m_params.z_speed_test_start, m_params.z_speed_test_end);
        const double hi   = std::max(m_params.z_speed_test_start, m_params.z_speed_test_end);
        z_max_speed = std::clamp(m_params.z_speed_test_start + band * m_params.z_speed_test_step, lo, hi);
    }
    if (z_max_speed > 0. && std::abs(dz) > 1e-9)
        cap = std::min(cap, seg_xy > 0. ? z_max_speed * seg_xy / std::abs(dz) * ratio * 60. :
                                          z_max_speed * 60.);
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

bool calibration_test(const PrintConfig &config)
{
    return config.calib_mode.value == CalibMode::NonPlanarSlope || config.calib_mode.value == CalibMode::NonPlanarZSpeed;
}

bool enabled(const PrintConfig &config)
{
    return config.nonplanar_mode.value != NonPlanarMode::Disabled || calibration_test(config);
}

// Fields of the calibration tests (CalibrationDialog.cpp builds the matching objects).
static constexpr double SLOPE_TEST_WAVELENGTH = 8.;
static constexpr double Z_SPEED_TEST_AMPLITUDE = 0.8;
static constexpr double Z_SPEED_TEST_WAVELENGTH = 16.;
static constexpr double TEST_RAMP_HEIGHT = 8.;

FieldParams slope_test_field(double start_deg, double step_deg, double end_deg, double band)
{
    FieldParams p;
    p.mode            = Mode::SlopeTest;
    p.wavelength      = SLOPE_TEST_WAVELENGTH;
    p.slope_band      = band;
    p.slope_start_deg = start_deg;
    p.slope_step_deg  = step_deg;
    p.slope_end_deg   = end_deg;
    return p;
}

double nonplanar_test_start_height(const PrintConfig &config)
{
    return config.nonplanar_flat_below.value + TEST_RAMP_HEIGHT;
}

// Waves fitted to the print head (Tisma): the two measurements of the head decide them. The steepest slope of a wave of
// amplitude a and wavelength L is a * 2 pi / L, which must be the free angle of the head, and the wave rises 2 a from
// trough to crest, which must be the clearance height: L = pi * height / tan(angle). Without a clearance height, or
// with the waves set by hand, the wavelength of the print settings.
static double wave_wavelength(const PrintConfig &config)
{
    if (config.nonplanar_fit_head.value) {
        const HeadLimits head = head_limits(config);
        const double tan_min = std::min(head.tan_x, head.tan_y);
        if (head.height > 0. && tan_min > 0.)
            return std::clamp(M_PI * head.height / tan_min, 4., 100.);
    }
    return std::max(1., config.nonplanar_wavelength.value);
}

static FieldParams field_params(const PrintConfig &config)
{
    FieldParams p;
    switch (config.nonplanar_mode.value) {
    case NonPlanarMode::Disabled: p.mode = Mode::Disabled; break;
    case NonPlanarMode::Wave:     p.mode = Mode::Wave;     break;
    case NonPlanarMode::Conical:  p.mode = Mode::Conical;  break;
    case NonPlanarMode::Overhangs: p.mode = Mode::Overhangs; break;
    case NonPlanarMode::Stress:   p.mode = Mode::Stress;   break;
    case NonPlanarMode::Surface:  p.mode = Mode::Surface;  break;
    case NonPlanarMode::Automatic: p.mode = Mode::Automatic; break;
    }
    switch (config.nonplanar_pattern.value) {
    case NonPlanarPattern::Egg:     p.pattern = Pattern::Egg;     break;
    case NonPlanarPattern::Ridges:  p.pattern = Pattern::Ridges;  break;
    case NonPlanarPattern::Twisted: p.pattern = Pattern::Twisted; break;
    }
    p.amplitude        = config.nonplanar_amplitude.value;
    p.wavelength       = wave_wavelength(config);
    p.angle_deg        = config.nonplanar_angle.value;
    p.twist_deg_per_mm = config.nonplanar_twist.value;
    p.cone_angle_deg   = config.nonplanar_cone_angle.value;
    if (config.calib_mode.value == CalibMode::NonPlanarSlope) {
        p = slope_test_field(config.calib_start.value, config.calib_step.value, config.calib_end.value,
                             config.calib_band_height.value);
    } else if (config.calib_mode.value == CalibMode::NonPlanarZSpeed) {
        p.mode       = Mode::Wave;
        p.pattern    = Pattern::Egg;
        p.amplitude  = Z_SPEED_TEST_AMPLITUDE;
        p.wavelength = Z_SPEED_TEST_WAVELENGTH;
    }
    return p;
}

HeadLimits head_limits(const PrintConfig &config)
{
    auto tangent = [](double deg) { return deg > 0. ? std::tan(std::min(deg, 89.) * M_PI / 180.) : 0.; };
    const double fallback = tangent(config.nonplanar_max_slope.value);
    HeadLimits out;
    out.tan_x  = config.nonplanar_head_free_angle_x.value > 0. ? tangent(config.nonplanar_head_free_angle_x.value) : fallback;
    out.tan_y  = config.nonplanar_head_free_angle_y.value > 0. ? tangent(config.nonplanar_head_free_angle_y.value) : fallback;
    out.height = std::max(0., config.nonplanar_head_clearance_height.value);
    return out;
}

FieldParams fit_to_head(FieldParams field, const Ramp &ramp, const BoundingBoxf3 &object_bbox, const HeadLimits &head)
{
    if (head.tan_x <= 0. || head.tan_y <= 0.)
        return field;
    // The slope of a layer is its gradient times ramp(z) <= 1, so the steepest one is that of the field.
    const double tan_min = std::min(head.tan_x, head.tan_y);
    double tangent = 0.;   // conical: tangent of the cone angle
    if (field.mode == Mode::Wave) {
        const double k = TWO_PI / field.wavelength;
        // Slope of a wave of unit amplitude relative to the head. Ridges slope along their direction only; the egg crate
        // reaches its steepest slope along X and along Y, the twisted ridges along every direction.
        double ratio = k / tan_min;
        if (field.pattern == Pattern::Ridges) {
            const double t = field.angle_deg * M_PI / 180.;
            ratio = k * std::hypot(std::cos(t) / head.tan_x, std::sin(t) / head.tan_y);
        }
        field.amplitude = 1. / ratio;
        // The wave rises 2 * amplitude from its troughs to its crests, which must pass below the print head.
        if (head.height > 0.)
            field.amplitude = std::min(field.amplitude, 0.5 * head.height);
    } else if (field.mode == Mode::Conical) {
        // The cone slopes the same in every direction: the smaller free angle. The sign (rising outwards or inwards)
        // is the one set by the user.
        tangent = std::copysign(tan_min, field.cone_angle_deg);
        field.cone_angle_deg = std::atan(tangent) * 180. / M_PI;
    } else
        return field;

    // Layer thickness: the deformation is linear in the amplitude / cone tangent, shrink it until J is acceptable.
    if (object_bbox.defined) {
        auto scaled = [&field, tangent](double s) {
            FieldParams f = field;
            if (f.mode == Mode::Wave)
                f.amplitude *= s;
            else
                f.cone_angle_deg = std::atan(tangent * s) * 180. / M_PI;
            return f;
        };
        auto acceptable = [&](double s) {
            const Deformation::Check c = Deformation(scaled(s), ramp).check(object_bbox);
            return c.j_min >= J_MIN && c.j_max <= J_MAX;
        };
        if (! acceptable(1.)) {
            double lo = 0., hi = 1.;
            for (int i = 0; i < 30; ++ i) {
                const double mid = 0.5 * (lo + hi);
                (acceptable(mid) ? lo : hi) = mid;
            }
            field = scaled(lo);
        }
    }
    return field;
}

// Overhangs mode: the field from the mesh, reduced until the layer thickness and the slope are acceptable.
static FieldParams overhang_field(FieldParams field, const PrintConfig &config, const BoundingBoxf3 &object_bbox,
                                  const indexed_triangle_set *mesh)
{
    if (mesh == nullptr || ! object_bbox.defined) {
        field.mode = Mode::Disabled;
        return field;
    }
    const HeadLimits limits = head_limits(config);
    OverhangParams params;
    params.threshold_deg = config.nonplanar_overhang_threshold.value;
    params.tan_x         = limits.tan_x;
    params.tan_y         = limits.tan_y;
    params.head          = head_clearance(config);
    params.z_flat        = config.nonplanar_flat_below.value;
    field.overhangs = build_overhang_field(*mesh, params);
    if (! field.overhangs) {
        field.mode = Mode::Disabled;
        return field;
    }
    // Ramp of value 1: the zones bring the deformation in and out themselves.
    Ramp ramp;
    ramp.z_flat = -1e6;
    ramp.z_ramp = 1.;
    auto acceptable = [&](double s) {
        FieldParams f = field;
        f.overhang_scale = s;
        const Deformation d(f, ramp);
        const Deformation::Check c = d.check(object_bbox);
        return c.j_min >= J_MIN && c.j_max <= J_MAX && d.head_slope_ratio(object_bbox, limits.tan_x, limits.tan_y) <= 1.;
    };
    if (! acceptable(1.)) {
        double lo = 0., hi = 1.;
        for (int i = 0; i < 20; ++ i) {
            const double mid = 0.5 * (lo + hi);
            (acceptable(mid) ? lo : hi) = mid;
        }
        field.overhang_scale = lo;
    }
    return field;
}

// Waves and cones fitted to the print head: their amplitude / cone tangent reduced until the print head clears the part
// already printed (Print::validate() would refuse them otherwise).
static FieldParams clear_print_head(FieldParams field, const Ramp &ramp, const PrintConfig &config, const indexed_triangle_set &mesh)
{
    const HeadClearance head = head_clearance(config);
    if ((head.height <= 0. && head.profile.empty()) || head.radius <= 0. ||
        (field.mode != Mode::Wave && field.mode != Mode::Conical))
        return field;
    const double tangent = std::tan(field.cone_angle_deg * M_PI / 180.);
    auto scaled = [&field, tangent](double s) {
        FieldParams f = field;
        if (f.mode == Mode::Wave)
            f.amplitude *= s;
        else
            f.cone_angle_deg = std::atan(tangent * s) * 180. / M_PI;
        return f;
    };
    auto clear = [&](double s) { return ! check_head_collision(Deformation(scaled(s), ramp), mesh, head).collides; };
    if (clear(1.))
        return field;
    double lo = 0., hi = 1.;
    for (int i = 0; i < 12; ++ i) {
        const double mid = 0.5 * (lo + hi);
        (clear(mid) ? lo : hi) = mid;
    }
    return scaled(lo);
}

bool layer_field_matches(const EngineeringLayerField &field, const BoundingBoxf3 &object_bbox)
{
    if (field.empty() || ! object_bbox.defined)
        return false;
    const Vec3d d = object_bbox.size() - field.part_size;
    return d.cwiseAbs().maxCoeff() <= std::max(0.05, 0.005 * field.part_size.maxCoeff());
}

// Stresses and surface modes: the layers given on a grid, reduced until the layer thickness and the slope are acceptable
// and the print head clears the part (mesh: object coordinates, may be null).
static FieldParams grid_field(FieldParams field, const Ramp &ramp, const PrintConfig &config,
                              const BoundingBoxf3 &object_bbox, const EngineeringLayerField *stress,
                              const indexed_triangle_set *mesh)
{
    if (stress == nullptr || ! layer_field_matches(*stress, object_bbox)) {
        field.mode = Mode::Disabled;
        return field;
    }
    field.layers = std::make_shared<LayerGrid>(*stress, object_bbox.min);
    const HeadLimits limits = head_limits(config);
    auto acceptable = [&](double s) {
        FieldParams f = field;
        f.layers_scale = s;
        const Deformation d(f, ramp);
        const Deformation::Check c = d.check(object_bbox);
        return c.j_min >= J_MIN && c.j_max <= J_MAX &&
               (limits.tan_x <= 0. || limits.tan_y <= 0. || d.head_slope_ratio(object_bbox, limits.tan_x, limits.tan_y) <= 1.);
    };
    if (! acceptable(1.)) {
        double lo = 0., hi = 1.;
        for (int i = 0; i < 20; ++ i) {
            const double mid = 0.5 * (lo + hi);
            (acceptable(mid) ? lo : hi) = mid;
        }
        field.layers_scale = lo;
    }
    // The print head must not hit the part already printed (a slower check: only once the rest is acceptable).
    const HeadClearance head = head_clearance(config);
    if (mesh != nullptr && (head.height > 0. || ! head.profile.empty()) && head.radius > 0.) {
        auto clear = [&](double s) {
            FieldParams f = field;
            f.layers_scale = s;
            return ! check_head_collision(Deformation(f, ramp), *mesh, head).collides;
        };
        if (! clear(field.layers_scale)) {
            double lo = 0., hi = field.layers_scale;
            for (int i = 0; i < 12; ++ i) {
                const double mid = 0.5 * (lo + hi);
                (clear(mid) ? lo : hi) = mid;
            }
            field.layers_scale = lo;
        }
    }
    return field;
}

// Weights of the fit of the surface mode (tuned on sloped blocks of 9 and 24 mm: the higher the part, the closer the
// layers get to its top).
static constexpr double SURFACE_TARGET_WEIGHT = 100.;
static constexpr double SURFACE_SMOOTH_WEIGHT = 3.;

// Surface mode: the layer offset whose slopes fit the top surfaces of the mesh. The same mesh and settings come back
// several times (validation, slicing, G-code, analysis): the last result is kept.
static std::shared_ptr<const EngineeringLayerField> surface_layer_field(const PrintConfig &config, const BoundingBoxf3 &object_bbox,
                                                                         const indexed_triangle_set &mesh)
{
    const HeadLimits limits = head_limits(config);
    LayerFitParams params;
    params.z_flat        = config.nonplanar_flat_below.value;
    // The top surfaces are few nodes of the grid: they must win against the smoothness of the rest.
    params.target_weight = SURFACE_TARGET_WEIGHT;
    params.smooth_weight = SURFACE_SMOOTH_WEIGHT;
    // Key: the settings and the mesh.
    size_t key = std::hash<double>()(limits.tan_x) ^ (std::hash<double>()(limits.tan_y) << 1) ^ (std::hash<double>()(params.z_flat) << 2);
    auto mix = [&key](size_t h) { key ^= h + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2); };
    mix(mesh.vertices.size());
    mix(mesh.indices.size());
    for (const stl_vertex &v : mesh.vertices)
        for (int i = 0; i < 3; ++ i)
            mix(std::hash<float>()(v[i]));
    static std::mutex cache_mutex;
    static size_t cache_key = 0;
    static std::shared_ptr<const EngineeringLayerField> cache;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (cache && cache_key == key)
            return cache;
    }
    auto field = std::make_shared<EngineeringLayerField>();
    const std::vector<SlopeTarget> targets = surface_slope_targets(mesh, limits.tan_x, limits.tan_y, params.z_flat);
    if (! fit_layer_field(targets, object_bbox, params, *field))
        field.reset();
    std::lock_guard<std::mutex> lock(cache_mutex);
    cache_key = key;
    cache     = field;
    return field;
}

Deformation make_deformation(const PrintConfig &config, const BoundingBoxf3 &object_bbox, const indexed_triangle_set *mesh,
                             const EngineeringLayerField *stress)
{
    FieldParams field = field_params(config);
    if (object_bbox.defined) {
        field.center   = 0.5 * (object_bbox.min.head<2>() + object_bbox.max.head<2>());
        field.slope_x0 = object_bbox.min.x();
    }
    if (field.mode == Mode::Overhangs && ! calibration_test(config)) {
        Ramp ramp;
        ramp.z_flat = -1e6;
        ramp.z_ramp = 1.;
        return Deformation(overhang_field(field, config, object_bbox, mesh), ramp);
    }
    // Tisma: automatic, the layers tilted under the overhangs (as in the overhangs mode) plus the layers following the
    // top surfaces, the latter reduced where the sum would make the layers too thick or thin, too steep or would let the
    // print head hit the part. Both bring their deformation in and out by themselves.
    if (field.mode == Mode::Automatic && ! calibration_test(config)) {
        Ramp none;
        none.z_flat = -1e6;
        none.z_ramp = 1.;
        FieldParams overhangs = field;
        overhangs.mode = Mode::Overhangs;
        overhangs = overhang_field(overhangs, config, object_bbox, mesh);
        if (overhangs.mode == Mode::Overhangs) {
            field.overhangs      = overhangs.overhangs;
            field.overhang_scale = overhangs.overhang_scale;
        }
        if (mesh != nullptr && object_bbox.defined) {
            const std::shared_ptr<const EngineeringLayerField> surface = surface_layer_field(config, object_bbox, *mesh);
            const FieldParams both = grid_field(field, none, config, object_bbox, surface.get(), mesh);
            if (both.mode == Mode::Automatic)
                field = both;
        }
        return Deformation(field, none);
    }
    Ramp ramp;
    ramp.z_flat     = config.nonplanar_flat_below.value;
    ramp.z_ramp     = std::max(0.1, config.nonplanar_ramp_height.value);
    ramp.z_ramp_top = ramp.z_ramp;
    if (calibration_test(config)) {
        // Gentle transition (the layer thickness stays within J_MIN..J_MAX), curved up to the top.
        ramp.z_ramp = ramp.z_ramp_top = TEST_RAMP_HEIGHT;
    } else {
        if (config.nonplanar_flat_top.value && object_bbox.defined)
            ramp.z_top = object_bbox.max.z();
        // Tisma: the layers computed by the structural analysis, within the limits of the print head.
        if (field.mode == Mode::Stress)
            return Deformation(grid_field(field, ramp, config, object_bbox, stress, mesh), ramp);
        // Tisma: the upper layers follow the top surfaces (the whole height takes the transition).
        if (field.mode == Mode::Surface) {
            if (mesh == nullptr || ! object_bbox.defined)
                return Deformation(FieldParams(), ramp);
            const std::shared_ptr<const EngineeringLayerField> surface = surface_layer_field(config, object_bbox, *mesh);
            // The fit brings the layers in from the flat first layers over the whole height by itself.
            Ramp none;
            none.z_flat = -1e6;
            none.z_ramp = 1.;
            return Deformation(grid_field(field, none, config, object_bbox, surface.get(), mesh), none);
        }
        // Tisma: as large as the print head allows instead of the amplitude / cone angle set by the user, and smaller
        // still where the print head would hit the part already printed.
        if (config.nonplanar_fit_head.value) {
            field = fit_to_head(field, ramp, object_bbox, head_limits(config));
            if (mesh != nullptr)
                field = clear_print_head(field, ramp, config, *mesh);
        }
    }
    return Deformation(field, ramp);
}

double mesh_max_edge(const PrintConfig &config)
{
    if (config.calib_mode.value == CalibMode::NonPlanarSlope)
        return SLOPE_TEST_WAVELENGTH / 10.;
    if (config.calib_mode.value == CalibMode::NonPlanarZSpeed)
        return Z_SPEED_TEST_WAVELENGTH / 10.;
    if (config.nonplanar_mode.value == NonPlanarMode::Wave)
        return std::clamp(wave_wavelength(config) / 10., 0.3, 1.5);
    if (config.nonplanar_mode.value == NonPlanarMode::Overhangs || config.nonplanar_mode.value == NonPlanarMode::Automatic)
        return 1.;
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
    if (config.calib_mode.value == CalibMode::NonPlanarZSpeed) {
        p.z_speed_test_start = config.calib_start.value;
        p.z_speed_test_step  = config.calib_step.value;
        p.z_speed_test_end   = config.calib_end.value;
        p.z_speed_test_band  = config.calib_band_height.value;
        p.z_speed_test_offset = nonplanar_test_start_height(config);
    }
    return p;
}

} // namespace NonPlanar
} // namespace Slic3r
