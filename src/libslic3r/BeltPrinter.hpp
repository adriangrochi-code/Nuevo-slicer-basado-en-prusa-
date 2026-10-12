///|/ Tisma Slicer: belt printers (infinite Z), with a configurable angle of the gantry.
///|/
///|/ The method follows the one of Cura BlackBelt (LGPLv3), reimplemented: the part is placed on the belt as on a bed,
///|/ it is rotated so that the inclined printing planes become horizontal, it is sliced as usual and the G-code is
///|/ converted to the axes of the machine.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_BeltPrinter_hpp_
#define slic3r_BeltPrinter_hpp_

#include <string>
#include <string_view>

#include "Point.hpp"

namespace Slic3r {

class Model;
class DynamicPrintConfig;

namespace Belt {

// Coordinates:
//  * world: the part placed on the belt as on a bed: X across the belt, Y along the belt, Z up from the belt.
//  * slicing: world rotated about X by the angle of the gantry, so that the printing planes are horizontal; the
//    slicer works in it (a = X, b = in the printing plane, c = across the layers).
//  * machine: the axes of the firmware: X across the belt, Y the belt, Z along the inclined gantry. A layer is
//    printed with Y fixed; the belt moves between layers.
// The angle is the one between the printing plane (the gantry) and the belt, in degrees (usually 45).
struct Frame
{
    explicit Frame(double angle_deg, double c_offset = 0.);

    // Rotation from world to slicing (proper rotation, no mirroring).
    Transform3d world_to_slicing() const;
    // From the slicing frame (as written by the slicer, after the shift c_offset) to the machine axes.
    Vec3d       slicing_to_machine(const Vec3d &p) const;
    // From the machine axes to the world (preview of the G-code).
    Vec3d       machine_to_world(const Vec3d &m) const;
    // Normal of the printing planes in the world (the direction in which the layers are stacked).
    Vec3d       layer_normal_world() const;

    double angle;
    double sin_a;
    double cos_a;
    // The slicing frame is shifted along c so that the part starts at c = 0 (the first layer): c_world = c + c_offset.
    double c_offset;
};

// Belt printing enabled in a print configuration, and its angle.
bool   enabled(const DynamicPrintConfig &config);
double angle(const DynamicPrintConfig &config);

// Prepares a copy of the model for slicing on a belt printer: the instances are rotated into the slicing frame and
// shifted so that the lowest point of the parts is at c = 0 (returned in c_offset). The settings which do not apply to
// belt printing are disabled on the objects (supports, raft, brim).
void transform_model(Model &model, const Frame &frame_without_offset, double &c_offset);

// Converts the moves of a block of G-code (complete lines) from the slicing frame to the machine axes. Keeps the
// position between calls. Only absolute G0 / G1 moves are converted; other commands are copied.
class GCodeTransform
{
public:
    explicit GCodeTransform(const Frame &frame) : m_frame(frame) {}
    std::string process(std::string_view gcode);

private:
    void process_line(std::string_view line, std::string &out);

    Frame m_frame;
    // Current position in the slicing frame (as written by the slicer).
    Vec3d m_pos { Vec3d::Zero() };
    // Last position emitted in machine axes (to emit only the axes which change).
    Vec3d m_last_machine { Vec3d::Zero() };
    bool  m_emitted[3] { false, false, false };
};

} // namespace Belt
} // namespace Slic3r

#endif // slic3r_BeltPrinter_hpp_
