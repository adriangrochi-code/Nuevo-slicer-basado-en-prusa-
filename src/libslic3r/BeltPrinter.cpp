///|/ Tisma Slicer: belt printers (infinite Z), with a configurable angle of the gantry.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BeltPrinter.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Geometry.hpp"
#include "Model.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {
namespace Belt {

Frame::Frame(double angle_deg, double c_offset) :
    angle(std::clamp(angle_deg, 10., 80.)),
    sin_a(std::sin(angle * PI / 180.)),
    cos_a(std::cos(angle * PI / 180.)),
    c_offset(c_offset)
{}

Transform3d Frame::world_to_slicing() const
{
    // a = x, b = y cos - z sin, c = y sin + z cos: rotation by +angle about X.
    Transform3d t = Transform3d::Identity();
    t.linear() << 1., 0., 0.,
                  0., cos_a, -sin_a,
                  0., sin_a, cos_a;
    return t;
}

Vec3d Frame::slicing_to_machine(const Vec3d &p) const
{
    const double c = p.z() + c_offset;
    // Y: the belt moves the part by c / sin between layers; Z: along the gantry.
    return { p.x(), c / sin_a, - p.y() + c * cos_a / sin_a };
}

Vec3d Frame::machine_to_world(const Vec3d &m) const
{
    return { m.x(), m.y() - m.z() * cos_a, m.z() * sin_a };
}

Vec3d Frame::layer_normal_world() const
{
    return { 0., sin_a, cos_a };
}

bool enabled(const DynamicPrintConfig &config)
{
    const ConfigOptionBool *opt = config.option<ConfigOptionBool>("belt_printer");
    return opt != nullptr && opt->value;
}

double angle(const DynamicPrintConfig &config)
{
    const ConfigOptionFloat *opt = config.option<ConfigOptionFloat>("belt_angle");
    return opt != nullptr ? opt->value : 45.;
}

void transform_model(Model &model, const Frame &frame, double &c_offset)
{
    const Transform3d rotation = frame.world_to_slicing();
    // Lowest point of the parts across the layers (c = n . p), exact: the minimum over the vertices.
    const Vec3d n = frame.layer_normal_world();
    double c_min = DBL_MAX;
    for (const ModelObject *object : model.objects)
        for (const ModelInstance *instance : object->instances)
            for (const ModelVolume *volume : object->volumes) {
                if (! volume->is_model_part())
                    continue;
                const Transform3d m = instance->get_matrix() * volume->get_matrix();
                const Vec3d w = m.linear().transpose() * n;
                const double offset = n.dot(m.translation());
                for (const Vec3f &v : volume->mesh().its.vertices)
                    c_min = std::min(c_min, w.dot(v.cast<double>()) + offset);
            }
    c_offset = c_min == DBL_MAX ? 0. : c_min;

    Transform3d to_slicing = rotation;
    to_slicing.pretranslate(Vec3d(0., 0., - c_offset));
    for (ModelObject *object : model.objects) {
        for (ModelInstance *instance : object->instances)
            instance->set_transformation(Geometry::Transformation(to_slicing * instance->get_matrix()));
        // Not available on a belt: the supports and the raft would stand on the plane c = 0 instead of the belt, the
        // brim would surround the first layer only.
        object->config.set("support_material", false);
        object->config.set("raft_layers", 0);
        object->config.set("brim_width", 0.);
        object->invalidate_bounding_box();
    }
}

std::string GCodeTransform::process(std::string_view gcode)
{
    std::string out;
    out.reserve(gcode.size() + gcode.size() / 8);
    size_t start = 0;
    while (start < gcode.size()) {
        size_t end = gcode.find('\n', start);
        const bool newline = end != std::string_view::npos;
        if (! newline)
            end = gcode.size();
        process_line(gcode.substr(start, end - start), out);
        if (newline)
            out += '\n';
        start = end + 1;
    }
    return out;
}

void GCodeTransform::process_line(std::string_view line, std::string &out)
{
    // Code and comment.
    const size_t comment_pos = line.find(';');
    std::string_view code    = line.substr(0, comment_pos);
    std::string_view comment = comment_pos == std::string_view::npos ? std::string_view() : line.substr(comment_pos);
    size_t i = 0;
    while (i < code.size() && (code[i] == ' ' || code[i] == '\t'))
        ++ i;
    // G0 / G1 (not G10, G11, G17...).
    const bool move = i + 1 < code.size() && (code[i] == 'G' || code[i] == 'g') && (code[i + 1] == '0' || code[i + 1] == '1') &&
                      (i + 2 == code.size() || code[i + 2] == ' ' || code[i + 2] == '\t');
    if (! move) {
        out.append(line);
        return;
    }
    // Words of the move.
    std::string others;
    bool has_axis = false;
    size_t j = i + 2;
    while (j < code.size()) {
        while (j < code.size() && (code[j] == ' ' || code[j] == '\t'))
            ++ j;
        if (j >= code.size())
            break;
        size_t k = j;
        while (k < code.size() && code[k] != ' ' && code[k] != '\t')
            ++ k;
        const std::string_view word = code.substr(j, k - j);
        const char letter = char(std::toupper(static_cast<unsigned char>(word.front())));
        if (letter == 'X' || letter == 'Y' || letter == 'Z') {
            const std::string value(word.substr(1));
            m_pos[letter - 'X'] = std::atof(value.c_str());
            has_axis = true;
        } else {
            others += ' ';
            others.append(word);
        }
        j = k;
    }
    if (! has_axis) {
        out.append(line);
        return;
    }
    const Vec3d machine = m_frame.slicing_to_machine(m_pos);
    out.append(code.substr(0, i + 2));
    char buf[64];
    for (int axis = 0; axis < 3; ++ axis) {
        // 3 decimals, as the G-code writer of PrusaSlicer; emit only the axes which change.
        const double v = std::round(machine[axis] * 1000.) / 1000.;
        if (m_emitted[axis] && std::abs(v - m_last_machine[axis]) < 0.0005)
            continue;
        std::snprintf(buf, sizeof(buf), " %c%.3f", char('X' + axis), v);
        out += buf;
        m_last_machine[axis] = v;
        m_emitted[axis] = true;
    }
    out += others;
    if (! comment.empty()) {
        out += ' ';
        out.append(comment);
    }
}

} // namespace Belt
} // namespace Slic3r
