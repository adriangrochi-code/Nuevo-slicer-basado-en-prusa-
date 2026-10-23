///|/ Tisma Slicer: calibration suite (temperature, pressure advance, retraction, speeds, ...).
///|/ Inspired by the calibrations of OrcaSlicer (AGPLv3). The models are generated, not loaded from files.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationDialog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "Tab.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

namespace {

// Rows of the V groove gauge of the non-planar calibration, H/3, 2H/3 and H deep.
constexpr int NONPLANAR_GAUGE_ROWS = 3;

struct Params
{
    double start;
    double end;
    double step;  // always positive in the dialog, the sign is given by start / end
    double band;  // height of each step, mm

    int    steps() const { return step > 0. ? int(std::floor(std::abs(end - start) / step + 1e-6)) + 1 : 1; }
    double height() const { return steps() * band; }
    double signed_step() const { return end >= start ? step : -step; }
};

// Context of the generated models.
struct Machine
{
    double nozzle   { 0.4 };
    double layer    { 0.2 };
    double first_layer     { 0.2 };
};

using ModelBuilder = std::function<void(ModelObject&, const Params&, const Machine&)>;

struct Test
{
    wxString      title;
    wxString      description;
    wxString      how_to_read;
    wxString      unit;
    CalibMode     mode;
    Params        defaults;
    int           digits;
    double        increment;
    double        max_value;
    ModelBuilder  build;
    // Meaning of the band: height of each step for the towers, width for the tests along X.
    wxString      band_label { wxString() };
    // Stable identifier (the titles are translated).
    std::string   key;
    // Test pieces with a single value instead of steps: the label of the value (the band, labelled by band_label, is
    // then in the unit of the test too).
    wxString      single_label { wxString() };
    // Print settings the test needs (applied to the print settings, which show them as modified).
    std::vector<std::pair<std::string, std::string>> print_settings;
};

// --- Geometry helpers ----------------------------------------------------------------------------------------

void add_box(ModelObject& obj, double x, double y, double z, double dx, double dy, double dz)
{
    indexed_triangle_set its = its_make_cube(dx, dy, dz);
    its_translate(its, Vec3f(float(x), float(y), float(z)));
    obj.add_volume(TriangleMesh(std::move(its)));
}

void add_cylinder(ModelObject& obj, double cx, double cy, double z, double r, double h)
{
    indexed_triangle_set its = its_make_cylinder(r, h);
    its_translate(its, Vec3f(float(cx), float(cy), float(z)));
    obj.add_volume(TriangleMesh(std::move(its)));
}

// Prism of a profile in the XZ plane (x, z) at x0, extruded along Y from y to y + dy.
void add_prism_y(ModelObject& obj, double x0, double y, double dy, const std::vector<Vec2d>& profile)
{
    indexed_triangle_set its;
    const int n = int(profile.size());
    for (int side = 0; side < 2; ++side)
        for (const Vec2d& p : profile)
            its.vertices.emplace_back(float(x0 + p.x()), float(y + side * dy), float(p.y()));
    for (int i = 1; i + 1 < n; ++i) {
        its.indices.emplace_back(0, i, i + 1);
        its.indices.emplace_back(n, n + i + 1, n + i);
    }
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        its.indices.emplace_back(i, n + j, j);
        its.indices.emplace_back(i, n + i, n + j);
    }
    // Outward normals whatever the orientation of the profile.
    if (its_volume(its) < 0.f)
        its_flip_triangles(its);
    obj.add_volume(TriangleMesh(std::move(its)));
}

// Raised text of seven segment digits on the front face (y = 0, raised towards -y by depth), centered at x, from
// z to z + height. Strokes of 0.8 mm: two lines of a 0.4 mm nozzle.
void add_digits(ModelObject& obj, const std::string& text, double x, double z, double height, double depth)
{
    // The segments of each digit: a top, b top right, c bottom right, d bottom, e bottom left, f top left, g middle.
    static const char* const SEGMENTS[10] = { "abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg", "acdefg", "abc", "abcdefg", "abcdfg" };
    constexpr double stroke = 0.8;
    const double width = std::max(2. * stroke + 0.6, 0.55 * height), spacing = stroke;
    double cx = x - 0.5 * (text.size() * width + (text.size() - 1) * spacing);
    for (char ch : text) {
        if (ch == '-')
            add_box(obj, cx, -depth, z + 0.5 * (height - stroke), width, depth, stroke);
        else if (ch >= '0' && ch <= '9')
            for (const char* seg = SEGMENTS[ch - '0']; *seg != 0; ++seg) {
                const double half = 0.5 * (height + stroke);
                // The right strokes, in the middle of the cell for a 1.
                const double right = ch == '1' ? cx + 0.5 * (width - stroke) : cx + width - stroke;
                switch (*seg) {
                case 'a': add_box(obj, cx, -depth, z + height - stroke, width, depth, stroke); break;
                case 'b': add_box(obj, right, -depth, z + 0.5 * (height - stroke), stroke, depth, half); break;
                case 'c': add_box(obj, right, -depth, z, stroke, depth, half); break;
                case 'd': add_box(obj, cx, -depth, z, width, depth, stroke); break;
                case 'e': add_box(obj, cx, -depth, z, stroke, depth, half); break;
                case 'f': add_box(obj, cx, -depth, z + 0.5 * (height - stroke), stroke, depth, half); break;
                case 'g': add_box(obj, cx, -depth, z + 0.5 * (height - stroke), width, depth, stroke); break;
                }
            }
        cx += width + spacing;
    }
}

void set(ModelObject& obj, const std::string& key, const std::string& value)
{
    obj.config.set_deserialize_strict(key, value);
}

// Numeric value: std::to_string() would use the decimal separator of the locale (0,2 in Spanish).
void set_layer_height(ModelObject& obj, double layer_height)
{
    obj.config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
}

// Thin walled box printed with a single or double perimeter and no infill nor top: the walls show the effect.
void hollow_box(ModelObject& obj, double size_x, double size_y, double height, int perimeters, const Machine& m)
{
    add_box(obj, 0, 0, 0, size_x, size_y, height);
    set_layer_height(obj, m.layer);
    set(obj, "perimeters", std::to_string(perimeters));
    set(obj, "fill_density", "0%");
    set(obj, "top_solid_layers", "0");
    set(obj, "bottom_solid_layers", "3");
    set(obj, "thin_walls", "0");
}

// --- The tests -----------------------------------------------------------------------------------------------

std::vector<Test> make_tests()
{
    std::vector<Test> tests;

    tests.push_back({ _L("Temperature tower"),
        _L("Prints a tower of blocks, one per temperature, with the temperature written on the front of each block. Each "
           "block has a bridge, two pillars and a pin for the strings, an overhang going from vertical to 75° and a pin "
           "to snap off to compare how well the layers stick."),
        _L("Compare the blocks: the bridge should not sag, the gap should be free of strings and the overhang on the "
           "right should stay clean as high as possible: the marks on the pillar next to it are at 30°, 45° and 60° "
           "(its top is 75°). Then push the pin on the left of each block sideways until it snaps: the harder it is to "
           "break, the better the layers stick. Choose the temperature written on the best block."),
        "°C", CalibMode::Temperature, { 230., 190., 5., 10. }, 0, 1., 350.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            constexpr double depth = 12.;                    // Y
            constexpr double pillar_a = 14., gap = 16., pillar_b = 6., raised = 0.6;
            constexpr double max_angle = 75.;                // of the overhang, from the vertical
            const double b = p.band;
            const double x_b = pillar_a + gap, x_overhang = x_b + pillar_b;
            // Overhang: a quarter of a circle of radius r from x_overhang, its angle from the vertical is the angle
            // on the circle, up to max_angle at 1 mm below the next block.
            const double r = std::max(1., b - 1.) / std::sin(max_angle * PI / 180.);
            // A plate below the tower, so that it stands firm.
            add_box(obj, -6., -4., 0., x_overhang + r + 8., depth + 8., 1.);
            for (int i = 0; i < p.steps(); ++i) {
                const double z = i * b;
                add_box(obj, 0., 0., z, pillar_a, depth, b);                       // pillar A, with the temperature
                add_box(obj, x_b, 0., z, pillar_b, depth, b);                       // pillar B
                add_box(obj, pillar_a, 0., z + b - 1., gap, depth, 1.);             // bridge between them
                if (b > 3.)                                                         // stringing pin in the gap
                    add_cylinder(obj, pillar_a + 0.5 * gap, 0.5 * depth, z, 1.2, b - 2.);
                // The temperature, raised on the front of pillar A (seven segment digits).
                const int temperature = int(std::round(p.start + i * p.signed_step()));
                const double h = std::min(7., 0.6 * b);
                add_digits(obj, std::to_string(temperature), 0.5 * pillar_a, z + 0.5 * (b - h), h, raised);
                // The overhang, in steps of 5°.
                std::vector<Vec2d> profile { { 0., z + b - 1. }, { 0., z } };
                for (double a = 5.; a <= max_angle + EPSILON; a += 5.)
                    profile.emplace_back(r * (1. - std::cos(a * PI / 180.)), z + r * std::sin(a * PI / 180.));
                add_prism_y(obj, x_overhang, 0., depth, profile);
                if (b >= 6.) {
                    // The marks of 30°, 45° and 60° on the front of pillar B, longer for a larger angle.
                    for (double a : { 30., 45., 60. }) {
                        const double length = 1.5 + 0.05 * a;
                        add_box(obj, x_overhang - length, -raised, z + r * std::sin(a * PI / 180.) - 0.2, length, raised, 0.4);
                    }
                    // A corbel at 45° on the left of pillar A, and on it the pin to snap off.
                    const double corbel = std::min(4., 0.4 * b);
                    add_prism_y(obj, 0., 0., depth, { { 0., z }, { 0., z + corbel }, { -corbel, z + corbel } });
                    add_cylinder(obj, -0.5 * corbel, 0.5 * depth, z + corbel, 1.2, b - corbel - 0.6);
                }
            }
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "2");
            set(obj, "fill_density", "15%");
        } });

    tests.push_back({ _L("Pressure advance tower"),
        _L("Prints a thin walled square tower at high speed. Pressure advance (linear advance) changes along the height."),
        _L("Look at the corners: choose the height where they are sharp, without bulges nor gaps. "
           "The value is start + step × floor(height / step height). The printer also shows it on its display."),
        "", CalibMode::PressureAdvance, { 0., 0.1, 0.002, 1. }, 4, 0.001, 2.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            hollow_box(obj, 50, 50, p.height(), 2, m);
            set(obj, "perimeter_speed", "100");
            set(obj, "external_perimeter_speed", "100");
        } });

    tests.push_back({ _L("Retraction tower"),
        _L("Prints two thin pillars. The retraction length changes along the height."),
        _L("Find the lowest height without strings between the pillars. "
           "The retraction length is start + step × floor(height / step height)."),
        "mm", CalibMode::Retraction, { 0., 2., 0.1, 1. }, 2, 0.05, 15.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            add_box(obj, 0, 0, 0, 52, 12, 1.);
            add_cylinder(obj, 6, 6, 0, 4, p.height());
            add_cylinder(obj, 46, 6, 0, 4, p.height());
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "2");
            set(obj, "fill_density", "15%");
        } });

    tests.push_back({ _L("Maximum volumetric speed"),
        _L("Prints a single wall cylinder. The volumetric flow of the wall increases along the height "
           "(the speed of the wall is the flow divided by the section of the extrusion)."),
        _L("Find the height where the wall starts to get weak, with gaps or a matte surface. "
           "The flow there is start + step × floor(height / step height) mm³/s: use a slightly lower value "
           "as the maximum volumetric speed of the filament."),
        "mm³/s", CalibMode::VolumetricSpeed, { 5., 25., 1., 2. }, 1, 0.5, 100.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            add_cylinder(obj, 0, 0, 0, 25, p.height());
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "1");
            set(obj, "fill_density", "0%");
            set(obj, "top_solid_layers", "0");
            set(obj, "bottom_solid_layers", "2");
        } });

    tests.push_back({ _L("Vertical fine artifacts (VFA)"),
        _L("Prints a single wall square tower. The speed of the walls increases at each block."),
        _L("Look at the walls against the light: choose the fastest block without fine vertical lines (VFA). "
           "Its speed is start + step × block number (from the bottom, starting at 0)."),
        "mm/s", CalibMode::PerimeterSpeed, { 40., 200., 10., 5. }, 0, 5., 1000.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            hollow_box(obj, 50, 50, p.height(), 1, m);
        } });

    tests.push_back({ _L("Acceleration tower"),
        _L("Prints a thin walled square tower. The print acceleration changes at each block."),
        _L("Look at the ringing (ghosting) after the corners: choose the highest acceleration that still prints clean walls."),
        "mm/s²", CalibMode::Acceleration, { 1000., 10000., 1000., 5. }, 0, 100., 100000.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            hollow_box(obj, 60, 60, p.height(), 1, m);
            set(obj, "perimeter_speed", "120");
            set(obj, "external_perimeter_speed", "120");
        } });

    tests.push_back({ _L("Cornering tower (jerk / junction deviation)"),
        _L("Prints a thin walled square tower. The cornering setting changes at each block: square corner velocity on "
           "Klipper, jerk on Marlin (values of 1 or more) or junction deviation on Marlin (values below 1)."),
        _L("Choose the highest value with sharp corners and no ringing."),
        "", CalibMode::Cornering, { 1., 20., 1., 5. }, 3, 0.01, 100.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            hollow_box(obj, 60, 60, p.height(), 1, m);
            set(obj, "perimeter_speed", "120");
            set(obj, "external_perimeter_speed", "120");
        } });

    tests.push_back({ _L("Input shaping frequency"),
        _L("Prints a thin walled square tower at high speed and acceleration. The input shaper frequency changes at "
           "each block (Klipper SET_INPUT_SHAPER, Marlin M593)."),
        _L("Choose the block with the least ringing after the corners: its frequency is start + step × block number."),
        "Hz", CalibMode::InputShaping, { 15., 85., 5., 5. }, 1, 1., 300.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            hollow_box(obj, 60, 60, p.height(), 1, m);
            set(obj, "perimeter_speed", "150");
            set(obj, "external_perimeter_speed", "150");
        } });

    tests.push_back({ _L("First layer Z offset"),
        _L("Prints a row of single layer patches in one go. Each patch is printed with the nozzle at a different "
           "height (Z offset), from left to right, so the first layer is calibrated in a single print instead of "
           "printing, adjusting and printing again."),
        _L("Choose the patch with the best first layer: smooth and closed, lines joined but not ridged, and well stuck. "
           "Too high: separate round lines with gaps. Too low: rough, transparent or ridged surface. Its offset is "
           "start + step × patch number (from the left, starting at 0): add it to the Z offset of the printer "
           "(Calibration > Apply a calibration result)."),
        _L("mm"), CalibMode::FirstLayerOffset, { -0.10, 0.10, 0.05, 25. }, 2, 0.01, 1.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            for (int i = 0; i < p.steps(); ++i)
                add_box(obj, i * p.band, 0, 0, p.band - 4., 30., m.first_layer);
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "1");
            set(obj, "top_solid_layers", "1");
            set(obj, "bottom_solid_layers", "1");
            set(obj, "fill_density", "100%");
        }, _L("Width of each step") });
    tests.back().key = "first_layer_offset";

    tests.push_back({ _L("Flow rate (%)"),
        _L("Prints a row of solid patches in one go, each with a different flow in percent (from left to right). The "
           "value of the best patch is the flow to use, no calculation needed."),
        _L("Look at the top surface: choose the smoothest patch, with the lines just touching (no gaps, no ridges nor "
           "bulging). Its flow is start + step × patch number (from the left, starting at 0), in percent: it is the "
           "extrusion multiplier of the filament (Calibration > Apply a calibration result)."),
        "%", CalibMode::FlowRate, { 90., 110., 5., 25. }, 1, 0.5, 200.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            for (int i = 0; i < p.steps(); ++i)
                add_box(obj, i * p.band, 0, 0, p.band - 4., 30., 2.);
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "2");
            set(obj, "top_solid_layers", "5");
            set(obj, "bottom_solid_layers", "3");
            set(obj, "fill_density", "100%");
            set(obj, "top_fill_pattern", "monotonic");
        }, _L("Width of each step") });
    tests.back().key = "flow_rate";

    tests.push_back({ _L("Coasting distance"),
        _L("Prints a row of hollow towers in one go, each with a different coasting distance (from left to right): "
           "the extruder stops this many millimeters before the end of each loop and the nozzle finishes it with the "
           "pressure left inside. The seams are aligned at the back of the towers."),
        _L("Look at the seams at the back: choose the tower with the cleanest seam, without a blob or zit and without "
           "a gap or a missing piece where the loop closes. Its distance is start + step × tower number (from the left, "
           "starting at 0): it is the coasting distance of the filament (Calibration > Apply a calibration result). "
           "Tune pressure advance and retraction first: coasting is a fine correction on top of them."),
        _L("mm"), CalibMode::Coasting, { 0., 1., 0.2, 20. }, 2, 0.05, 3.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            for (int i = 0; i < p.steps(); ++i)
                add_box(obj, i * p.band, 0, 0, p.band - 6., 15., 20.);
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "2");
            set(obj, "top_solid_layers", "0");
            set(obj, "bottom_solid_layers", "3");
            set(obj, "fill_density", "0%");
            set(obj, "seam_position", "rear");
        }, _L("Width of each step") });
    tests.back().key = "coasting";

    // Arc overhangs (Tisma): a row of columns, each carrying a slab that sticks out to the back (+Y) with nothing below,
    // printed with arcs growing from the column.
    auto arc_cantilever = [](ModelObject& obj, double x, double width, double length) {
        add_box(obj, x, 0., 0., width, 10., 10.);
        add_box(obj, x, 0., 10., width, 10. + length, 2.);
    };
    auto arc_settings = [](ModelObject& obj, const Machine& m) {
        set_layer_height(obj, m.layer);
        set(obj, "overhang_arcs", "1");
        set(obj, "overhang_arcs_max_distance", "0");
        set(obj, "overhangs", "1");
        set(obj, "support_material", "0");
        set(obj, "perimeters", "2");
        set(obj, "fill_density", "15%");
    };

    tests.push_back({ _L("Arc overhang speed"),
        _L("Prints a row of columns in one go, each carrying a slab that sticks out 15 mm to the back with nothing below. "
           "The first layer of each slab is printed with arc overhangs, at a different speed for each column (from left "
           "to right)."),
        _L("Look at the slabs from below: choose the fastest column whose arcs are round, joined to each other and not "
           "sagging. Its speed is start + step × column number (from the left, starting at 0): it is the arc overhangs "
           "speed (Calibration > Apply a calibration result). Run the fan at full power, as for bridges."),
        _L("mm/s"), CalibMode::ArcOverhangSpeed, { 2., 10., 2., 16. }, 1, 0.5, 50.,
        [arc_cantilever, arc_settings](ModelObject& obj, const Params& p, const Machine& m) {
            for (int i = 0; i < p.steps(); ++i)
                arc_cantilever(obj, i * p.band, std::max(4., p.band - 6.), 15.);
            arc_settings(obj, m);
        }, _L("Width of each step") });
    tests.back().key = "arc_overhang_speed";

    tests.push_back({ _L("Arc overhang maximum distance"),
        _L("Prints a row of columns in one go, each carrying a slab that sticks out to the back with nothing below, "
           "longer for each column (from left to right). The first layer of each slab is printed with arc overhangs at "
           "the arc overhangs speed of the print settings: calibrate the speed first."),
        _L("Look at the slabs from below: choose the longest one whose arcs reach the end without sagging or coming "
           "apart. Its length is start + step × column number (from the left, starting at 0): it is the arc overhangs "
           "maximum distance (Calibration > Apply a calibration result); longer overhangs are printed as usual beyond "
           "it, supports included."),
        _L("mm"), CalibMode::ArcOverhangDistance, { 10., 30., 5., 14. }, 0, 1., 100.,
        [arc_cantilever, arc_settings](ModelObject& obj, const Params& p, const Machine& m) {
            for (int i = 0; i < p.steps(); ++i)
                arc_cantilever(obj, i * p.band, std::max(4., p.band - 6.), std::max(1., p.start + i * p.signed_step()));
            arc_settings(obj, m);
        }, _L("Width of each step") });
    tests.back().key = "arc_overhang_distance";

    tests.push_back({ _L("Arc overhang overlap"),
        _L("Prints a row of columns in one go, each carrying a slab that sticks out 15 mm to the back with nothing below. "
           "The first layer of each slab is printed with arc overhangs, each arc overlapping the previous one by a "
           "different percentage of the extrusion width for each column (from left to right)."),
        _L("Look at the slabs from below and from above: choose the column with the least overlap whose arcs are joined "
           "to each other without gaps, before they start to pile up or bulge. Its overlap is start + step × column "
           "number (from the left, starting at 0): it is the arc overhangs overlap (Calibration > Apply a calibration "
           "result). Calibrate the arc speed first."),
        "%", CalibMode::ArcOverhangOverlap, { 0., 40., 10., 16. }, 0, 1., 50.,
        [arc_cantilever, arc_settings](ModelObject& obj, const Params& p, const Machine& m) {
            for (int i = 0; i < p.steps(); ++i) {
                arc_cantilever(obj, i * p.band, std::max(4., p.band - 6.), 15.);
                // The overlap of the arcs of this column only: a setting of its two volumes.
                const double overlap = std::clamp(p.start + i * p.signed_step(), 0., 50.);
                for (size_t v = obj.volumes.size() - 2; v < obj.volumes.size(); ++v)
                    obj.volumes[v]->config.set_key_value("overhang_arcs_overlap", new ConfigOptionPercent(overlap));
            }
            arc_settings(obj, m);
        }, _L("Width of each step") });
    tests.back().key = "arc_overhang_overlap";

    return tests;
}

// --- Dialog --------------------------------------------------------------------------------------------------

class CalibrationDialog : public wxDialog
{
public:
    CalibrationDialog(wxWindow* parent, const Test& test, const Params* initial = nullptr)
        : wxDialog(parent, wxID_ANY, test.title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {
        const Params defaults = initial ? *initial : test.defaults;
        wxGetApp().UpdateDlgDarkUI(this);
        const int em = wxGetApp().em_unit();

        auto main = new wxBoxSizer(wxVERTICAL);
        auto desc = new wxStaticText(this, wxID_ANY, test.description);
        desc->Wrap(45 * em);
        main->Add(desc, 0, wxALL, em);

        auto grid = new wxFlexGridSizer(3, em, em);
        grid->AddGrowableCol(1);
        auto add_field = [&](const wxString& label, double value, double min, double max, double inc, int digits, const wxString& unit) {
            grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            auto ctrl = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, spin_ctrl_size(this, 14),
                                             wxSP_ARROW_KEYS, min, max, value, inc);
            ctrl->SetDigits(digits);
            grid->Add(ctrl, 1, wxEXPAND);
            grid->Add(new wxStaticText(this, wxID_ANY, unit), 0, wxALIGN_CENTER_VERTICAL);
            return ctrl;
        };
        // Offsets may be negative.
        const double min_value = test.mode == CalibMode::FirstLayerOffset ? - test.max_value : 0.;
        // Test pieces with one value of each (no steps): the value and the band in the unit of the test.
        m_single = ! test.single_label.empty();
        if (m_single) {
            m_start = add_field(test.single_label, defaults.start, min_value, test.max_value, test.increment, test.digits, test.unit);
            m_band  = add_field(test.band_label, defaults.band, 0., test.max_value, test.increment, test.digits, test.unit);
        } else {
            m_start = add_field(_L("Start value"),         defaults.start, min_value, test.max_value, test.increment, test.digits, test.unit);
            m_end   = add_field(_L("End value"),           defaults.end,   min_value, test.max_value, test.increment, test.digits, test.unit);
            m_step  = add_field(_L("Step"),                defaults.step,  test.increment / 10., test.max_value, test.increment, test.digits + 1, test.unit);
            m_band  = add_field(test.band_label.empty() ? _L("Height of each step") : test.band_label,
                                defaults.band,  0.2, 50., 0.5, 1, _L("mm"));
        }
        main->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, em);

        m_along_x = ! test.band_label.empty();
        m_base_band = test.mode == CalibMode::ResinExposure;
        m_summary = new wxStaticText(this, wxID_ANY, "");
        main->Add(m_summary, 0, wxALL, em);

        auto how = new wxStaticText(this, wxID_ANY, test.how_to_read);
        how->Wrap(45 * em);
        how->SetFont(wxGetApp().small_font());
        main->Add(how, 0, wxLEFT | wxRIGHT | wxBOTTOM, em);

        if (wxSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL))
            main->Add(btns, 0, wxEXPAND | wxALL, em);

        for (wxSpinCtrlDouble* ctrl : { m_start, m_end, m_step, m_band })
            if (ctrl != nullptr)
                ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { update_summary(); });
        update_summary();

        SetSizerAndFit(main);
        CenterOnParent();
    }

    Params params() const
    {
        if (m_single)
            return { m_start->GetValue(), m_start->GetValue(), 1., m_band->GetValue() };
        return { m_start->GetValue(), m_end->GetValue(), m_step->GetValue(), m_band->GetValue() };
    }

private:
    void update_summary()
    {
        const Params p = params();
        if (m_single) {
            m_summary->SetLabel(wxString());
            Layout();
            return;
        }
        m_summary->SetLabel(m_along_x ?
            format_wxstr(_L("%1% steps, %2% mm long."), p.steps(), wxString::Format("%.1f", p.height())) :
            format_wxstr(_L("%1% steps, tower height %2% mm."), p.steps(), wxString::Format("%.1f", p.height() + (m_base_band ? p.band : 0.))));
        Layout();
    }

    wxSpinCtrlDouble* m_start { nullptr };
    wxSpinCtrlDouble* m_end   { nullptr };
    wxSpinCtrlDouble* m_step  { nullptr };
    wxSpinCtrlDouble* m_band  { nullptr };
    wxStaticText*     m_summary { nullptr };
    bool              m_along_x { false };
    bool              m_base_band { false }; // a base below the steps (resin exposure tower)
    bool              m_single { false };
};

void run_test(wxWindow* parent, const Test& test, const Params* initial = nullptr)
{
    CalibrationDialog dlg(parent, test, initial);
    if (dlg.ShowModal() != wxID_OK)
        return;
    const Params p = dlg.params();
    if (p.step <= 0. || p.band <= 0.) {
        wxMessageBox(_L("The step and its height must be greater than zero."), test.title, wxOK | wxICON_WARNING, parent);
        return;
    }

    const DynamicPrintConfig& printer = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    Machine machine;
    if (const ConfigOptionFloats* nozzle = printer.option<ConfigOptionFloats>("nozzle_diameter"); nozzle && !nozzle->values.empty())
        machine.nozzle = nozzle->values.front();
    machine.layer = std::round(machine.nozzle * 50.) / 100.; // half of the nozzle diameter, rounded to 0.01 mm
    if (const ConfigOptionFloatOrPercent* fl = wxGetApp().preset_bundle->prints.get_edited_preset().config.option<ConfigOptionFloatOrPercent>("first_layer_height"))
        machine.first_layer = fl->get_abs_value(machine.nozzle);

    Model model;
    ModelObject* obj = model.add_object();
    obj->name = into_u8(test.title);
    test.build(*obj, p, machine);

    DynamicPrintConfig calib;
    calib.set_key_value("calib_mode",        new ConfigOptionEnum<CalibMode>(test.mode));
    calib.set_key_value("calib_start",       new ConfigOptionFloat(p.start));
    calib.set_key_value("calib_end",         new ConfigOptionFloat(p.end));
    calib.set_key_value("calib_step",        new ConfigOptionFloat(p.signed_step()));
    calib.set_key_value("calib_band_height", new ConfigOptionFloat(p.band));

    wxGetApp().plater()->load_calibration(model, calib);

    // Print settings of the test.
    if (! test.print_settings.empty())
        if (Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
            DynamicPrintConfig conf = *tab->get_config();
            ConfigSubstitutionContext ctxt { ForwardCompatibilitySubstitutionRule::Disable };
            for (const auto& [key, value] : test.print_settings)
                conf.set_deserialize(key, value, ctxt);
            tab->load_config(conf);
        }
}

const std::vector<Test>& tests()
{
    static std::vector<Test> all = make_tests();
    return all;
}

// --- Resin (SLA) calibrations ---------------------------------------------------------------------------------

// Exposure tower: a base band with the normal exposure, then one band per exposure time (written per layer by the
// archive, docs/RESINA.md). Each band has horizontal holes, which close with too much exposure, and thin fins on
// the back, which fail with too little; a groove on the front separates the bands.
static const std::array<double, 5> RESIN_HOLES { 0.4, 0.6, 0.8, 1.0, 1.4 };
static const std::array<double, 4> RESIN_FINS  { 0.15, 0.25, 0.35, 0.5 };

static std::vector<Test> make_resin_tests()
{
    std::vector<Test> tests;
    tests.push_back({ _L("Resin exposure tower"),
        _L("Prints a tower in one go with a different exposure time in each band of layers, from the bottom up (the "
           "first band is the base, with the exposure of the material). Each band has holes of 0.4, 0.6, 0.8, 1.0 and "
           "1.4 mm across it and fins of 0.15, 0.25, 0.35 and 0.5 mm on the back. Needs a printer whose archive keeps "
           "the exposure of each layer (Elegoo GOO)."),
        _L("Count the bands from the bottom, above the base (separated by the grooves on the front). Too little exposure: "
           "the thin fins are missing or bent. Too much: the small holes are closed. Choose the band with the most "
           "open holes and the most fins: its time is start + step × band number (from 0) and goes to the exposure "
           "time of the material (Calibration > Apply a calibration result)."),
        "s", CalibMode::ResinExposure, { 1.5, 3.5, 0.25, 2. }, 2, 0.05, 60.,
        [](ModelObject& obj, const Params& p, const Machine&) {
            const double w = 30., d = 4., b = p.band;
            const int    n = p.steps();
            // One mesh, made with two booleans here: a simple object and no CSG in the slicing.
            auto box = [](double x, double y, double z, double dx, double dy, double dz) {
                indexed_triangle_set its = its_make_cube(dx, dy, dz);
                its_translate(its, Vec3f(float(x), float(y), float(z)));
                return its;
            };
            indexed_triangle_set tower = box(0., 0., 0., w, d, b * (n + 1));
            indexed_triangle_set fins, cuts;
            for (int k = 0; k < n; ++ k) {
                const double z0 = b * (k + 1);
                // Groove on the front at the bottom of the band.
                its_merge(cuts, box(-0.5, -0.5, z0 - 0.15, w + 1., 0.8, 0.3));
                for (size_t i = 0; i < RESIN_HOLES.size(); ++ i)
                    if (RESIN_HOLES[i] <= b - 0.6) { // a wall of 0.3 mm above and below the hole
                        indexed_triangle_set hole = its_make_cylinder(0.5 * RESIN_HOLES[i], d + 1., 2. * PI / 48.);
                        // Z axis to Y axis: swap the coordinates (a mirror) and flip the triangles back outwards.
                        for (stl_vertex& v : hole.vertices)
                            v = stl_vertex(v.x() + float(3. + 4. * i), v.z() - 0.5f, v.y() + float(z0 + 0.5 * b));
                        for (stl_triangle_vertex_indices& f : hole.indices)
                            std::swap(f[1], f[2]);
                        its_merge(cuts, hole);
                    }
                for (size_t i = 0; i < RESIN_FINS.size(); ++ i)
                    its_merge(fins, box(22. + 2. * i, d - 0.2, z0 + 0.2, RESIN_FINS[i], 1.7, b - 0.4));
            }
            try {
                MeshBoolean::cgal::plus(tower, fins);
                MeshBoolean::cgal::minus(tower, cuts);
                obj.add_volume(TriangleMesh(std::move(tower)));
            } catch (const std::exception&) {
                // Fallback: the parts and the negative volumes, joined by the slicing.
                obj.clear_volumes();
                add_box(obj, 0., 0., 0., w, d, b * (n + 1));
                obj.add_volume(TriangleMesh(std::move(fins)));
                obj.add_volume(TriangleMesh(std::move(cuts)), ModelVolumeType::NEGATIVE_VOLUME);
            }
            // On the plate, without supports nor pad: the heights of the bands are measured from the plate.
            set(obj, "supports_enable", "0");
            set(obj, "pad_enable", "0");
        } });
    tests.back().key = "resin_exposure";
    return tests;
}

static const std::vector<Test>& resin_tests()
{
    static std::vector<Test> all = make_resin_tests();
    return all;
}

// Block to measure the shrinkage of the resin: printed on the plate, measured with a caliper.
static constexpr double RESIN_BLOCK_XY = 30., RESIN_BLOCK_Z = 10.;

static void load_resin_dimension_block(wxWindow* parent)
{
    Model model;
    ModelObject* obj = model.add_object();
    obj->name = _u8L("Resin dimensions block");
    add_box(*obj, 0., 0., 0., RESIN_BLOCK_XY, RESIN_BLOCK_XY, RESIN_BLOCK_Z);
    set(*obj, "supports_enable", "0");
    set(*obj, "pad_enable", "0");
    obj->center_around_origin();
    const Vec2d center = wxGetApp().plater()->build_volume().bed_center();
    obj->add_instance()->set_offset(Vec3d(center.x(), center.y(), 0.5 * RESIN_BLOCK_Z));
    DynamicPrintConfig calib;
    calib.set_key_value("calib_mode", new ConfigOptionEnum<CalibMode>(CalibMode::Disabled));
    wxGetApp().plater()->load_calibration(model, calib);
    wxMessageBox(format_wxstr(_L("A block of %1% x %1% x %2% mm printed on the plate, without supports.\n\n"
                                 "Wash and cure it as usual, then measure X, Y and Z with a caliper away from the bottom "
                                 "edge (the first layers spread). Enter the values in Calibration > Resin dimensional "
                                 "correction: it calculates the correction of the material."),
                              int(RESIN_BLOCK_XY), int(RESIN_BLOCK_Z)),
                 _L("Resin dimensions block"), wxOK | wxICON_INFORMATION, parent);
}

// The measured sizes of the block give the scaling of the material (material_correction_x/y/z): the new factor is
// the current one times the size wanted over the size printed.
class ResinDimensionsDialog : public wxDialog
{
public:
    explicit ResinDimensionsDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, _L("Resin dimensional correction"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {
        wxGetApp().UpdateDlgDarkUI(this);
        const int em = wxGetApp().em_unit();
        auto main = new wxBoxSizer(wxVERTICAL);
        auto info = new wxStaticText(this, wxID_ANY, format_wxstr(_L("Measured sizes of the dimensions block (%1% x %1% x %2% mm), "
            "printed with the current correction of the material. The new correction is written to the material (save "
            "the preset afterwards)."), int(RESIN_BLOCK_XY), int(RESIN_BLOCK_Z)));
        info->Wrap(42 * em);
        main->Add(info, 0, wxALL, em);

        const DynamicPrintConfig* cfg = config();
        auto grid = new wxFlexGridSizer(3, em, em);
        const char* axes[3] = { "x", "y", "z" };
        const double nominal[3] = { RESIN_BLOCK_XY, RESIN_BLOCK_XY, RESIN_BLOCK_Z };
        for (int i = 0; i < 3; ++ i) {
            m_current[i] = cfg ? cfg->opt_float(std::string("material_correction_") + axes[i]) : 1.;
            grid->Add(new wxStaticText(this, wxID_ANY, format_wxstr(_L("Measured %1%"), wxString(axes[i]).Upper())), 0, wxALIGN_CENTER_VERTICAL);
            m_measured[i] = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, spin_ctrl_size(this, 12),
                                                 wxSP_ARROW_KEYS, 0.5 * nominal[i], 1.5 * nominal[i], nominal[i], 0.01);
            m_measured[i]->SetDigits(2);
            grid->Add(m_measured[i], 0);
            grid->Add(new wxStaticText(this, wxID_ANY, _L("mm")), 0, wxALIGN_CENTER_VERTICAL);
            m_measured[i]->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { update(); });
            m_measured[i]->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update(); });
        }
        main->Add(grid, 0, wxLEFT | wxRIGHT, em);
        m_result = new wxStaticText(this, wxID_ANY, wxEmptyString);
        main->Add(m_result, 0, wxALL, em);
        if (wxSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL))
            main->Add(btns, 0, wxEXPAND | wxALL, em);
        update();
        SetSizerAndFit(main);
        CenterOnParent();
    }

    std::array<double, 3> corrections() const
    {
        const double nominal[3] = { RESIN_BLOCK_XY, RESIN_BLOCK_XY, RESIN_BLOCK_Z };
        std::array<double, 3> out;
        for (int i = 0; i < 3; ++ i)
            out[i] = m_current[i] * nominal[i] / m_measured[i]->GetValue();
        return out;
    }

    void apply()
    {
        Tab* tab = wxGetApp().get_tab(Preset::TYPE_SLA_MATERIAL);
        if (tab == nullptr)
            return;
        DynamicPrintConfig conf = *tab->get_config();
        const std::array<double, 3> c = corrections();
        const char* axes[3] = { "x", "y", "z" };
        for (int i = 0; i < 3; ++ i)
            conf.set_key_value(std::string("material_correction_") + axes[i], new ConfigOptionFloat(std::round(c[i] * 10000.) / 10000.));
        tab->load_config(conf);
    }

private:
    static const DynamicPrintConfig* config()
    {
        Tab* tab = wxGetApp().get_tab(Preset::TYPE_SLA_MATERIAL);
        return tab ? tab->get_config() : nullptr;
    }
    void update()
    {
        const std::array<double, 3> c = corrections();
        m_result->SetLabel(format_wxstr(_L("Correction X %1% (now %2%), Y %3% (now %4%), Z %5% (now %6%)"),
            wxString::Format("%.4f", c[0]), wxString::Format("%.4f", m_current[0]),
            wxString::Format("%.4f", c[1]), wxString::Format("%.4f", m_current[1]),
            wxString::Format("%.4f", c[2]), wxString::Format("%.4f", m_current[2])));
        Layout();
        Fit();
    }

    std::array<wxSpinCtrlDouble*, 3> m_measured { nullptr, nullptr, nullptr };
    std::array<double, 3>            m_current  { 1., 1., 1. };
    wxStaticText*                    m_result   { nullptr };
};

// --- Apply a calibration result ------------------------------------------------------------------------------
// The value read on the print goes to the setting it calibrates, without calculations.

struct ResultTarget
{
    CalibMode    mode;
    wxString     name;
    wxString     unit;
    Preset::Type preset;
    std::function<void(DynamicPrintConfig&, double)> apply;
    std::function<double(const DynamicPrintConfig&)> current;
};

static std::vector<ResultTarget> result_targets()
{
    auto floats_first = [](const DynamicPrintConfig& c, const char* key) {
        const ConfigOptionFloats* o = c.option<ConfigOptionFloats>(key);
        return o && ! o->values.empty() ? o->values.front() : 0.;
    };
    auto set_all = [](DynamicPrintConfig& c, const char* key, double v) {
        if (ConfigOptionFloats* o = c.option<ConfigOptionFloats>(key, true); o != nullptr) {
            if (o->values.empty())
                o->values.push_back(v);
            for (double& x : o->values)
                x = v;
        }
    };
    std::vector<ResultTarget> out;
    out.push_back({ CalibMode::FlowRate, _L("Flow rate (%)"), "%", Preset::TYPE_FILAMENT,
        [set_all](DynamicPrintConfig& c, double v) { set_all(c, "extrusion_multiplier", v / 100.); },
        [floats_first](const DynamicPrintConfig& c) { return floats_first(c, "extrusion_multiplier") * 100.; } });
    out.push_back({ CalibMode::Coasting, _L("Coasting distance"), _L("mm"), Preset::TYPE_FILAMENT,
        [set_all](DynamicPrintConfig& c, double v) { set_all(c, "filament_coast_distance", v); },
        [floats_first](const DynamicPrintConfig& c) { return floats_first(c, "filament_coast_distance"); } });
    out.push_back({ CalibMode::ArcOverhangSpeed, _L("Arc overhang speed"), _L("mm/s"), Preset::TYPE_PRINT,
        [](DynamicPrintConfig& c, double v) { c.set_key_value("overhang_arcs_speed", new ConfigOptionFloat(v)); },
        [](const DynamicPrintConfig& c) { return c.opt_float("overhang_arcs_speed"); } });
    out.push_back({ CalibMode::ArcOverhangDistance, _L("Arc overhang maximum distance"), _L("mm"), Preset::TYPE_PRINT,
        [](DynamicPrintConfig& c, double v) {
            c.set_key_value("overhang_arcs_max_distance", new ConfigOptionFloat(v));
            c.set_key_value("overhang_arcs", new ConfigOptionBool(true));
        },
        [](const DynamicPrintConfig& c) { return c.opt_float("overhang_arcs_max_distance"); } });
    out.push_back({ CalibMode::ArcOverhangOverlap, _L("Arc overhang overlap"), "%", Preset::TYPE_PRINT,
        [](DynamicPrintConfig& c, double v) {
            c.set_key_value("overhang_arcs_overlap", new ConfigOptionPercent(v));
            c.set_key_value("overhang_arcs", new ConfigOptionBool(true));
        },
        [](const DynamicPrintConfig& c) { return c.opt_float("overhang_arcs_overlap"); } });
    out.push_back({ CalibMode::FirstLayerOffset, _L("First layer Z offset (added)"), _L("mm"), Preset::TYPE_PRINTER,
        [](DynamicPrintConfig& c, double v) { c.set_key_value("z_offset", new ConfigOptionFloat(c.opt_float("z_offset") + v)); },
        [](const DynamicPrintConfig& c) { return c.opt_float("z_offset"); } });
    out.push_back({ CalibMode::Temperature, _L("Nozzle temperature"), "°C", Preset::TYPE_FILAMENT,
        [](DynamicPrintConfig& c, double v) {
            if (ConfigOptionInts* o = c.option<ConfigOptionInts>("temperature", true))
                for (int& x : o->values) x = int(std::round(v));
        },
        [](const DynamicPrintConfig& c) {
            const ConfigOptionInts* o = c.option<ConfigOptionInts>("temperature");
            return o && ! o->values.empty() ? double(o->values.front()) : 0.;
        } });
    out.push_back({ CalibMode::Retraction, _L("Retraction length"), _L("mm"), Preset::TYPE_PRINTER,
        [set_all](DynamicPrintConfig& c, double v) { set_all(c, "retract_length", v); },
        [floats_first](const DynamicPrintConfig& c) { return floats_first(c, "retract_length"); } });
    out.push_back({ CalibMode::ResinExposure, _L("Resin exposure time"), "s", Preset::TYPE_SLA_MATERIAL,
        [](DynamicPrintConfig& c, double v) { c.set_key_value("exposure_time", new ConfigOptionFloat(v)); },
        [](const DynamicPrintConfig& c) { return c.opt_float("exposure_time"); } });
    out.push_back({ CalibMode::VolumetricSpeed, _L("Maximum volumetric speed"), _L("mm³/s"), Preset::TYPE_FILAMENT,
        [set_all](DynamicPrintConfig& c, double v) { set_all(c, "filament_max_volumetric_speed", v); },
        [floats_first](const DynamicPrintConfig& c) { return floats_first(c, "filament_max_volumetric_speed"); } });
    return out;
}

class ApplyResultDialog : public wxDialog
{
public:
    explicit ApplyResultDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, _L("Apply a calibration result"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
        , m_targets(result_targets())
    {
        // The settings of the current printer technology only.
        const bool sla = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA;
        m_targets.erase(std::remove_if(m_targets.begin(), m_targets.end(), [sla](const ResultTarget& t) {
            return (t.preset == Preset::TYPE_SLA_MATERIAL) != sla; }), m_targets.end());
        wxGetApp().UpdateDlgDarkUI(this);
        const int em = wxGetApp().em_unit();
        auto main = new wxBoxSizer(wxVERTICAL);
        auto info = new wxStaticText(this, wxID_ANY, _L("Enter the value read on the calibration print: it is written to "
                                                         "the setting it calibrates (save the preset afterwards)."));
        info->Wrap(40 * em);
        main->Add(info, 0, wxALL, em);

        wxArrayString names;
        for (const ResultTarget& t : m_targets)
            names.Add(t.name);
        m_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, names);
        // The test loaded last, if it is one of them.
        int selection = 0;
        if (const auto* mode = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnum<CalibMode>>("calib_mode"))
            for (size_t i = 0; i < m_targets.size(); ++i)
                if (m_targets[i].mode == mode->value)
                    selection = int(i);
        m_choice->SetSelection(selection);
        main->Add(m_choice, 0, wxEXPAND | wxLEFT | wxRIGHT, em);

        auto row = new wxBoxSizer(wxHORIZONTAL);
        m_value = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, spin_ctrl_size(this, 12), wxSP_ARROW_KEYS, -1000., 1000., 0., 0.01);
        m_value->SetDigits(2);
        row->Add(m_value, 0, wxRIGHT, em / 2);
        m_unit = new wxStaticText(this, wxID_ANY, wxEmptyString);
        row->Add(m_unit, 0, wxALIGN_CENTER_VERTICAL);
        main->Add(row, 0, wxALL, em);
        m_current = new wxStaticText(this, wxID_ANY, wxEmptyString);
        main->Add(m_current, 0, wxLEFT | wxRIGHT | wxBOTTOM, em);

        if (wxSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL))
            main->Add(btns, 0, wxEXPAND | wxALL, em);
        m_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { update(); });
        update();
        SetSizerAndFit(main);
        CenterOnParent();
    }

    void apply_result()
    {
        const ResultTarget& t = m_targets[size_t(std::max(0, m_choice->GetSelection()))];
        if (Tab* tab = wxGetApp().get_tab(t.preset)) {
            DynamicPrintConfig conf = *tab->get_config();
            t.apply(conf, m_value->GetValue());
            tab->load_config(conf);
        }
    }

private:
    void update()
    {
        const ResultTarget& t = m_targets[size_t(std::max(0, m_choice->GetSelection()))];
        m_unit->SetLabel(t.unit);
        double current = 0.;
        if (Tab* tab = wxGetApp().get_tab(t.preset))
            current = t.current(*tab->get_config());
        m_current->SetLabel(format_wxstr(_L("Current value: %1%"), wxString::Format("%.3g %s", current, t.unit)));
        // A sensible start: the current value, except for the offset which is added.
        m_value->SetValue(t.mode == CalibMode::FirstLayerOffset ? 0. : current);
        Layout();
    }

    std::vector<ResultTarget> m_targets;
    wxChoice*         m_choice  { nullptr };
    wxSpinCtrlDouble* m_value   { nullptr };
    wxStaticText*     m_unit    { nullptr };
    wxStaticText*     m_current { nullptr };
};

// --- Non-planar calibration assistant ------------------------------------------------------------------------

} // namespace

// Strength coupons of the staggered perimeters (docs/BRICK_LAYERS.md): two identical open tubes, standing, made only
// of perimeters; A with normal perimeters, B with staggered perimeters. Broken across the layers, they compare the
// strength between layers.
static void load_stagger_coupons(wxWindow* parent)
{
    constexpr double size_x = 20., size_y = 10., height = 50., gap = 15.;
    const Vec2d center = wxGetApp().plater()->build_volume().bed_center();
    Model model;
    for (int k = 0; k < 2; ++ k) {
        ModelObject* obj = model.add_object();
        obj->name = k == 0 ? _u8L("A - normal perimeters") : _u8L("B - staggered perimeters");
        add_box(*obj, 0., 0., 0., size_x, size_y, height);
        // Only walls: 4 perimeters, no infill, closed bottom, open top.
        obj->config.set_key_value("perimeters",          new ConfigOptionInt(4));
        obj->config.set_key_value("fill_density",        new ConfigOptionPercent(0.));
        obj->config.set_key_value("top_solid_layers",    new ConfigOptionInt(0));
        obj->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(3));
        obj->config.set_key_value("stagger_perimeters",  new ConfigOptionBool(k == 1));
        obj->center_around_origin();
        ModelInstance* instance = obj->add_instance();
        instance->set_offset(Vec3d(center.x() + (k == 0 ? -1. : 1.) * 0.5 * (size_x + gap), center.y(), 0.5 * height));
    }
    DynamicPrintConfig calib;
    calib.set_key_value("calib_mode", new ConfigOptionEnum<CalibMode>(CalibMode::Disabled));
    wxGetApp().plater()->load_calibration(model, calib);
    wxMessageBox(_L("Two identical tubes made only of walls: A with normal perimeters, B with staggered perimeters.\n\n"
                    "Print them with the same filament. Hold each one by its base and push the top sideways (or hang "
                    "weights from the top) until it breaks between the layers. Compare the force, or the weight, that "
                    "each one held: the difference is what the staggered perimeters add with your printer and filament."),
                 _L("Staggered perimeters: strength coupons"), wxOK | wxICON_INFORMATION, parent);
}

static void apply_result(wxWindow* parent)
{
    ApplyResultDialog dlg(parent);
    if (dlg.ShowModal() == wxID_OK)
        dlg.apply_result();
}

static void append_apply_result(wxMenu* menu, wxWindow* parent)
{
    const int apply_id = wxWindow::NewControlId();
    menu->Append(apply_id, _L("Apply a calibration result") + dots, _L("Writes the value read on a calibration print to its setting."));
    menu->Bind(wxEVT_MENU, [parent](wxCommandEvent&) { apply_result(parent); }, apply_id);
}

static void run_resin_test(wxWindow* parent, size_t i)
{
    // The exposure of each band is written by the archives that keep an exposure per layer.
    const DynamicPrintConfig& printer = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    if (printer.opt_string("sla_archive_format") != "GOO") {
        wxMessageBox(_L("The exposure tower needs a printer whose archive keeps the exposure of each layer: for now "
                        "the Elegoo printers (GOO format). With other printers, print a small test once per exposure "
                        "time."), resin_tests()[i].title, wxOK | wxICON_INFORMATION, parent);
        return;
    }
    run_test(parent, resin_tests()[i]);
}

static void resin_dimensions_correction(wxWindow* parent)
{
    ResinDimensionsDialog dlg(parent);
    if (dlg.ShowModal() == wxID_OK)
        dlg.apply();
}

static wxMenu* create_resin_calibration_menu(wxWindow* parent, bool models_only)
{
    auto menu = new wxMenu();
    const std::vector<Test>& all = resin_tests();
    for (size_t i = 0; i < all.size(); ++ i) {
        const int id = wxWindow::NewControlId();
        menu->Append(id, all[i].title + dots, all[i].description);
        menu->Bind(wxEVT_MENU, [parent, i](wxCommandEvent&) { run_resin_test(parent, i); }, id);
    }
    const int block_id = wxWindow::NewControlId();
    menu->Append(block_id, _L("Resin dimensions block") + dots, _L("A block to measure the shrinkage of the resin in X, Y and Z."));
    menu->Bind(wxEVT_MENU, [parent](wxCommandEvent&) { load_resin_dimension_block(parent); }, block_id);
    if (models_only)
        return menu;
    const int dims_id = wxWindow::NewControlId();
    menu->Append(dims_id, _L("Resin dimensional correction") + dots,
                 _L("Calculates the scaling correction of the material from the measured sizes of the dimensions block."));
    menu->Bind(wxEVT_MENU, [parent](wxCommandEvent&) { resin_dimensions_correction(parent); }, dims_id);
    menu->AppendSeparator();
    append_apply_result(menu, parent);
    return menu;
}

// Whether a test is of use with the printer in use (docs/EXISTING_FEATURES.md, section 3, has the sources):
// - Bambu Lab printers calibrate the nozzle height, the vibration compensation (Bambu Lab wiki, Printer Calibration
//   Guide: "Auto-Calibration ... automatically adjusts ... bed leveling, nozzle height, vibration compensation and motor
//   noise") and the flow dynamics (pressure advance: every printer of Bambu Studio's resources/printers declares
//   auto_pa_cali_thumbnail_image) by themselves.
// - The Prusa CORE One, MK4/S, MK3.9/S and XL calibrate the first layer with their LoadCell (Prusa Knowledge Base,
//   Live adjust Z).
// - Bambu Lab does not document a command for the corners (jerk or junction deviation, M205) and Bambu Studio has no
//   test for them (src/libslic3r/Calib.hpp, CalibMode).
// - Pressure advance, corners and vibrations change the printer by G-code commands that only Klipper, RepRapFirmware
//   and Marlin understand (M593, the input shaping of Marlin, since Marlin 2).
static bool calibration_applies(CalibMode mode)
{
    if (wxGetApp().preset_bundle == nullptr)
        return true;
    const PresetCollection&   printers = wxGetApp().preset_bundle->printers;
    const DynamicPrintConfig& printer  = printers.get_edited_preset().config;
    // The vendor of the system profile the printer is or comes from (none for a printer of a project or without one).
    const Preset*      system = printers.get_selected_preset_parent();
    const std::string  vendor = system != nullptr && system->vendor != nullptr ? system->vendor->id : std::string();
    const std::string  notes  = printer.opt_string("printer_notes");
    const std::string  model  = printer.opt_string("printer_model");
    const bool bambu = vendor == "Orca_BBL" || notes.find("PRINTER_VENDOR_ORCA_BBL") != std::string::npos ||
                       boost::starts_with(model, "Bambu Lab");
    const bool prusa = vendor == "PrusaResearch" || notes.find("PRINTER_VENDOR_PRUSA3D") != std::string::npos;
    const bool loadcell = prusa && (boost::starts_with(model, "COREONE") || boost::starts_with(model, "MK4") ||
                                    boost::starts_with(model, "MK3.9") || boost::starts_with(model, "XL"));
    const auto* flavor_opt = printer.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor");
    const GCodeFlavor flavor = flavor_opt ? flavor_opt->value : gcfMarlinFirmware;
    const bool klipper_rrf = flavor == gcfKlipper || flavor == gcfRepRapFirmware;
    const bool marlin      = flavor == gcfMarlinFirmware || flavor == gcfMarlinLegacy;
    switch (mode) {
    case CalibMode::PressureAdvance:  return ! bambu && (klipper_rrf || marlin);
    case CalibMode::Cornering:        return ! bambu && (klipper_rrf || marlin);
    case CalibMode::InputShaping:     return ! bambu && (klipper_rrf || flavor == gcfMarlinFirmware);
    case CalibMode::FirstLayerOffset: return ! bambu && ! loadcell;
    default:                          return true;
    }
}

wxMenu* create_calibration_menu(wxWindow* parent, bool resin, bool models_only)
{
    if (resin)
        return create_resin_calibration_menu(parent, models_only);
    auto menu = new wxMenu();
    const std::vector<Test>& all = tests();
    for (size_t i = 0; i < all.size(); ++i) {
        const CalibMode mode = all[i].mode;
        // The menu of the plate is made again at each right click: only the tests of the printer in use.
        if (models_only && ! calibration_applies(mode))
            continue;
        const int id = wxWindow::NewControlId();
        menu->Append(id, all[i].title + dots, all[i].description);
        menu->Bind(wxEVT_MENU, [parent, i](wxCommandEvent&) { run_test(parent, tests()[i]); }, id);
        // The Calibration menu is made once: a test the printer in use does not need is disabled, also in the More page
        // (wxMenu::UpdateUI() sends the event to the window, not to the menu).
        if (! models_only)
            parent->Bind(wxEVT_UPDATE_UI, [mode](wxUpdateUIEvent& evt) { evt.Enable(calibration_applies(mode)); }, id);
    }
    menu->AppendSeparator();
    const int coupons_id = wxWindow::NewControlId();
    menu->Append(coupons_id, _L("Staggered perimeters: strength coupons") + dots,
                 _L("Two tubes to break, with and without staggered perimeters, to measure the strength they add between layers."));
    menu->Bind(wxEVT_MENU, [parent](wxCommandEvent&) { load_stagger_coupons(parent); }, coupons_id);
    if (models_only)
        return menu;
    menu->AppendSeparator();
    append_apply_result(menu, parent);
    return menu;
}

std::vector<CalibrationAction> calibration_actions(wxWindow* parent, bool resin)
{
    std::vector<CalibrationAction> out;
    auto add = [&out](const wxString& label, const wxString& title, const wxString& description, const std::string& icon,
                      std::function<void()> run, std::function<bool()> available = nullptr) {
        out.push_back({ label, title + "\n" + description, icon, std::move(run), std::move(available) });
    };
    if (resin) {
        for (size_t i = 0; i < resin_tests().size(); ++ i)
            add(_L("Exposure"), resin_tests()[i].title, resin_tests()[i].description, "resin",
                [parent, i]() { run_resin_test(parent, i); });
        add(_L("Dimensions"), _L("Resin dimensions block"), _L("A block to measure the shrinkage of the resin in X, Y and Z."),
            "measure", [parent]() { load_resin_dimension_block(parent); });
        add(_L("Correction"), _L("Resin dimensional correction"),
            _L("Calculates the scaling correction of the material from the measured sizes of the dimensions block."), "scale",
            [parent]() { resin_dimensions_correction(parent); });
    } else {
        // A short name and an icon for each test (the titles are too long to be read under an icon).
        auto short_name = [](CalibMode mode) -> std::pair<wxString, std::string> {
            switch (mode) {
            case CalibMode::Temperature:         return { _L("Temperature"), "burn" };
            case CalibMode::PressureAdvance:     return { _L("Pressure"), "funnel" };
            case CalibMode::Retraction:          return { _L("Retraction"), "legend_retract" };
            case CalibMode::VolumetricSpeed:     return { _L("Max flow"), "spool" };
            case CalibMode::PerimeterSpeed:      return { "VFA", "layers" };
            case CalibMode::Acceleration:        return { _L("Acceleration"), "move" };
            case CalibMode::Cornering:           return { _L("Corners"), "rotate" };
            case CalibMode::InputShaping:        return { _L("Vibrations"), "tisma_vibration" };
            case CalibMode::FirstLayerOffset:    return { _L("First layer"), "drop_to_bed" };
            case CalibMode::FlowRate:            return { _L("Flow"), "infill" };
            case CalibMode::Coasting:            return { _L("Coasting"), "legend_deretract" };
            case CalibMode::ArcOverhangSpeed:    return { _L("Arc speed"), "time" };
            case CalibMode::ArcOverhangDistance: return { _L("Arc length"), "measure" };
            case CalibMode::ArcOverhangOverlap:  return { _L("Arc overlap"), "legend_shells" };
            default:                             return { wxString(), "tisma_calibrate" };
            }
        };
        for (size_t i = 0; i < tests().size(); ++ i) {
            const auto [label, icon] = short_name(tests()[i].mode);
            const CalibMode mode = tests()[i].mode;
            add(label.empty() ? tests()[i].title : label, tests()[i].title, tests()[i].description, icon,
                [parent, i]() { run_test(parent, tests()[i]); }, [mode]() { return calibration_applies(mode); });
        }
        add(_L("Strength"), _L("Staggered perimeters: strength coupons"),
            _L("Two tubes to break, with and without staggered perimeters, to measure the strength they add between layers."),
            "test", [parent]() { load_stagger_coupons(parent); });
    }
    add(_L("Apply result"), _L("Apply a calibration result"), _L("Writes the value read on a calibration print to its setting."),
        "wrench", [parent]() { apply_result(parent); });
    return out;
}

void show_calibration_menu(wxWindow* parent)
{
    wxMenu* menu = create_calibration_menu(parent, wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
    parent->PopupMenu(menu, parent->ScreenToClient(wxGetMousePosition()));
    delete menu;
}

} // namespace GUI
} // namespace Slic3r
