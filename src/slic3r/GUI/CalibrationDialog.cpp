///|/ Tisma Slicer: calibration suite (temperature, pressure advance, retraction, speeds, ...).
///|/ Inspired by the calibrations of OrcaSlicer (AGPLv3). The models are generated, not loaded from files.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationDialog.hpp"

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include "libslic3r/Model.hpp"
#include "libslic3r/NonPlanar.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

namespace {

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
    // Non-planar tests: height where the curved layers are complete (NonPlanar::nonplanar_test_start_height()).
    double nonplanar_start { 8.6 };
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

// Block [0, size_x] x [0, size_y] whose top is at height + top(x, y): a closed mesh on a grid of the given step.
void add_heightfield_block(ModelObject& obj, double size_x, double size_y, double step, const std::function<double(double, double)>& top)
{
    const int nx = std::max(2, int(std::ceil(size_x / step)) + 1);
    const int ny = std::max(2, int(std::ceil(size_y / step)) + 1);
    indexed_triangle_set its;
    // Top grid, then bottom grid.
    for (int level = 0; level < 2; ++level)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const double x = size_x * i / (nx - 1), y = size_y * j / (ny - 1);
                its.vertices.emplace_back(float(x), float(y), level == 0 ? float(top(x, y)) : 0.f);
            }
    const int bottom = nx * ny;
    auto v = [nx](int i, int j) { return j * nx + i; };
    for (int j = 0; j + 1 < ny; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            // Top faces up, bottom faces down.
            its.indices.emplace_back(v(i, j), v(i + 1, j), v(i + 1, j + 1));
            its.indices.emplace_back(v(i, j), v(i + 1, j + 1), v(i, j + 1));
            its.indices.emplace_back(bottom + v(i, j), bottom + v(i + 1, j + 1), bottom + v(i + 1, j));
            its.indices.emplace_back(bottom + v(i, j), bottom + v(i, j + 1), bottom + v(i + 1, j + 1));
        }
    // Side walls, outwards.
    auto wall = [&](int a, int b) { // a -> b along the boundary, counter-clockwise seen from above
        its.indices.emplace_back(a, bottom + a, bottom + b);
        its.indices.emplace_back(a, bottom + b, b);
    };
    for (int i = 0; i + 1 < nx; ++i) {
        wall(v(i, 0), v(i + 1, 0));                      // y = 0
        wall(v(i + 1, ny - 1), v(i, ny - 1));            // y = size_y
    }
    for (int j = 0; j + 1 < ny; ++j) {
        wall(v(nx - 1, j), v(nx - 1, j + 1));            // x = size_x
        wall(v(0, j + 1), v(0, j));                      // x = 0
    }
    obj.add_volume(TriangleMesh(std::move(its)));
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
        _L("Prints a tower of blocks with bridges and gaps. The nozzle temperature changes at each block."),
        _L("Choose the block with the best bridge, the least stringing and good layer adhesion. "
           "Its temperature is the start value plus the block number (from the bottom, starting at 0) times the step."),
        "°C", CalibMode::Temperature, { 230., 190., 5., 10. }, 0, 1., 350.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            const double b = p.band;
            for (int i = 0; i < p.steps(); ++i) {
                const double z = i * b;
                add_box(obj, 0, 0, z, 30, 10, 1.);                 // floor of the block
                add_box(obj, 0, 0, z, 8, 10, b - 1.5);            // left pillar
                add_box(obj, 22, 0, z, 8, 10, b - 1.5);           // right pillar
                add_box(obj, 0, 0, z + b - 1.5, 30, 10, 1.5);      // bridge over the gap
                add_cylinder(obj, 15, 5, z + 1., 1.5, b - 2.5);    // pin in the gap: stringing
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

    // --- Non-planar layers (Tisma): what the non-planar slicing needs to know about the printer.

    tests.push_back({ _L("Non-planar: free nozzle angle gauge (static)"),
        _L("Prints, with normal flat layers, a gauge of ramps whose angle grows in steps (from left to right). It is "
           "measured with the printer cold, nothing curved is printed: the first, safe step of the calibration of the "
           "maximum layer slope."),
        _L("With the printer cold and the nozzle clean, put the gauge on the bed and lower the nozzle onto the middle "
           "of a ramp until it touches. The ramp is free if only the nozzle tip touches it: no other part of the nozzle, "
           "the heater block, its sock, the cooling duct or the probe. The steepest free ramp is the free angle: start + "
           "step × ramp number (from the left, starting at 0). Check it with the gauge turned 90°, 180° and 270° (the "
           "print head is not symmetric) and keep the smallest. Enter it in the non-planar calibration assistant."),
        "°", CalibMode::Disabled, { 15., 50., 5., 12. }, 0, 1., 75.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            // Ramps 30 mm long (longer than the nozzle and the heater block), 2 mm apart, rising along Y.
            const double length = 30., base = 1.5, gap = 2.;
            for (int i = 0; i < p.steps(); ++i) {
                const double angle = std::clamp(p.start + i * p.signed_step(), 1., 75.);
                const double top   = base + length * std::tan(angle * M_PI / 180.);
                indexed_triangle_set its = its_make_cube(p.band - gap, length, base);
                for (stl_vertex& v : its.vertices)
                    if (v.y() > float(0.5 * length) && v.z() > float(0.5 * base))
                        v.z() = float(top);
                its_translate(its, Vec3f(float(i * p.band), 0.f, 0.f));
                obj.add_volume(TriangleMesh(std::move(its)));
            }
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "2");
            set(obj, "fill_density", "10%");
        }, _L("Width of each step") });
    tests.back().key = "nonplanar_free_angle";

    tests.push_back({ _L("Non-planar: maximum layer slope"),
        _L("Prints a block whose top layers are curved ridges. The slope of the ridges grows in steps along X "
           "(from left to right). Uses its own curved layers, not the non-planar settings of the print."),
        _L("Look at the top surface from the left: find the first step where the nozzle scrapes or drags the "
           "surface, or the lines stop sticking. The step before it is the steepest one your nozzle prints well: "
           "its slope is start + step × step number (from the left, starting at 0). Enter it as \"Maximum layer "
           "slope\" (Print Settings > Non-planar)."),
        "°", CalibMode::NonPlanarSlope, { 10., 40., 5., 10. }, 0, 1., 60.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            // The top follows the curved layers of the test, so that the last layers are whole curved layers.
            const double size_x = std::max(20., p.steps() * p.band), size_y = 24., height = m.nonplanar_start + 4.;
            NonPlanar::FieldParams field = NonPlanar::slope_test_field(p.start, p.signed_step(), p.end, p.band);
            field.center   = Vec2d(0.5 * size_x, 0.5 * size_y);
            field.slope_x0 = 0.;
            const NonPlanar::Field f(field);
            add_heightfield_block(obj, size_x, size_y, 0.4, [&](double x, double y) { return height + f.g(x, y, height); });
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "2");
            set(obj, "fill_density", "15%");
            set(obj, "top_solid_layers", "4");
        }, _L("Width of each step") });

    tests.back().key = "nonplanar_slope";

    tests.push_back({ _L("Non-planar: Z axis speed"),
        _L("Prints a cylinder with wavy layers. The Z speed allowed to the curved moves grows at each step above "
           "the transition (about 9 mm). Uses its own curved layers, not the non-planar settings of the print."),
        _L("Find the first step where the layers look squashed or shifted, the wall gets rough, or the Z motor "
           "skips (the top ends lower than the rest). The step before it is the fastest Z speed that works: start + "
           "step × floor((height − 9 mm) / step height). Enter it as the maximum Z feed rate of the machine limits "
           "(Printer Settings > Machine limits). If the firmware limits Z (M203), the steps above that limit look "
           "the same."),
        "mm/s", CalibMode::NonPlanarZSpeed, { 2., 12., 2., 5. }, 1, 0.5, 100.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            add_cylinder(obj, 0, 0, 0, 20., m.nonplanar_start + p.height());
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "2");
            set(obj, "fill_density", "15%");
            set(obj, "perimeter_speed", "60");
            set(obj, "infill_speed", "80");
        } });

    tests.back().key = "nonplanar_z_speed";

    tests.push_back({ _L("Non-planar: print head clearance gauge"),
        _L("Prints a staircase gauge (nothing is printed near the print head). Put it on the cold bed with the nozzle "
           "touching the bed and slide it under the print head."),
        _L("The highest step that fits under the lowest point of the print head (heater block with its sock, cooling "
           "duct, probe) without touching it is the head clearance height: start + step × step number (from the "
           "lowest, starting at 0). Measure also the farthest point of those parts from the nozzle: it is the head "
           "clearance radius. Enter both in Printer Settings > General > Print head."),
        "mm", CalibMode::Disabled, { 1., 8., 1., 8. }, 1, 0.5, 30.,
        [](ModelObject& obj, const Params& p, const Machine& m) {
            for (int i = 0; i < p.steps(); ++i)
                add_box(obj, i * p.band, 0, 0, p.band, 25., p.start + i * p.signed_step());
            set_layer_height(obj, m.layer);
            set(obj, "perimeters", "2");
            set(obj, "fill_density", "20%");
        }, _L("Width of each step") });

    tests.back().key = "nonplanar_head_gauge";

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
            auto ctrl = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(14 * em, -1),
                                             wxSP_ARROW_KEYS, min, max, value, inc);
            ctrl->SetDigits(digits);
            grid->Add(ctrl, 1, wxEXPAND);
            grid->Add(new wxStaticText(this, wxID_ANY, unit), 0, wxALIGN_CENTER_VERTICAL);
            return ctrl;
        };
        m_start = add_field(_L("Start value"),         defaults.start, 0., test.max_value, test.increment, test.digits, test.unit);
        m_end   = add_field(_L("End value"),           defaults.end,   0., test.max_value, test.increment, test.digits, test.unit);
        m_step  = add_field(_L("Step"),                defaults.step,  test.increment / 10., test.max_value, test.increment, test.digits + 1, test.unit);
        m_band  = add_field(test.band_label.empty() ? _L("Height of each step") : test.band_label,
                            defaults.band,  0.2, 50., 0.5, 1, _L("mm"));
        main->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, em);

        m_along_x = ! test.band_label.empty();
        m_summary = new wxStaticText(this, wxID_ANY, "");
        main->Add(m_summary, 0, wxALL, em);

        auto how = new wxStaticText(this, wxID_ANY, test.how_to_read);
        how->Wrap(45 * em);
        how->SetFont(wxGetApp().small_font());
        main->Add(how, 0, wxLEFT | wxRIGHT | wxBOTTOM, em);

        if (wxSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL))
            main->Add(btns, 0, wxEXPAND | wxALL, em);

        for (wxSpinCtrlDouble* ctrl : { m_start, m_end, m_step, m_band })
            ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { update_summary(); });
        update_summary();

        SetSizerAndFit(main);
        CenterOnParent();
    }

    Params params() const { return { m_start->GetValue(), m_end->GetValue(), m_step->GetValue(), m_band->GetValue() }; }

private:
    void update_summary()
    {
        const Params p = params();
        m_summary->SetLabel(m_along_x ?
            format_wxstr(_L("%1% steps, %2% mm long."), p.steps(), wxString::Format("%.1f", p.height())) :
            format_wxstr(_L("%1% steps, tower height %2% mm."), p.steps(), wxString::Format("%.1f", p.height())));
        Layout();
    }

    wxSpinCtrlDouble* m_start { nullptr };
    wxSpinCtrlDouble* m_end   { nullptr };
    wxSpinCtrlDouble* m_step  { nullptr };
    wxSpinCtrlDouble* m_band  { nullptr };
    wxStaticText*     m_summary { nullptr };
    bool              m_along_x { false };
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
    {
        PrintConfig print_config;
        print_config.apply(wxGetApp().preset_bundle->full_config(), true);
        machine.nonplanar_start = NonPlanar::nonplanar_test_start_height(print_config);
    }

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
}

const std::vector<Test>& tests()
{
    static std::vector<Test> all = make_tests();
    return all;
}

const Test* find_test(const std::string& key)
{
    for (const Test& t : tests())
        if (t.key == key)
            return &t;
    return nullptr;
}

// --- Non-planar calibration assistant ------------------------------------------------------------------------
// Two steps for the maximum layer slope: a static gauge measured with the printer cold (safe), then the printed test
// in fine steps around that value; the result is applied with a tolerance. It also manages the print head clearance
// and the Z speed. The measured values are kept in the application settings between the prints.

class NonPlanarAssistant : public wxDialog
{
public:
    explicit NonPlanarAssistant(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, _L("Non-planar calibration assistant"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {
        wxGetApp().UpdateDlgDarkUI(this);
        const int em = wxGetApp().em_unit();
        const DynamicPrintConfig& printer = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        const DynamicPrintConfig& print   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        AppConfig& app = *wxGetApp().app_config;
        auto stored = [&app](const char* key, double fallback) {
            const std::string v = app.get(key);
            return v.empty() ? fallback : std::atof(v.c_str());
        };

        auto main = new wxBoxSizer(wxVERTICAL);
        auto intro = new wxStaticText(this, wxID_ANY,
            _L("Measure what the non-planar layers need to know about your printer. The maximum layer slope is found in "
               "two steps: first a static gauge measured with the printer cold, then a printed test in small steps around "
               "that value. The result is applied with a safety tolerance."));
        intro->Wrap(55 * em);
        main->Add(intro, 0, wxALL, em);

        auto grid = new wxFlexGridSizer(4, em / 2, em);
        grid->AddGrowableCol(0);
        auto add_row = [&](const wxString& label, double value, double min, double max, double inc, int digits,
                           const wxString& unit, const wxString& button, std::function<void()> on_button) {
            grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            auto ctrl = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(10 * em, -1),
                                             wxSP_ARROW_KEYS, min, max, value, inc);
            ctrl->SetDigits(digits);
            grid->Add(ctrl, 0);
            grid->Add(new wxStaticText(this, wxID_ANY, unit), 0, wxALIGN_CENTER_VERTICAL);
            if (on_button) {
                auto btn = new wxButton(this, wxID_ANY, button);
                btn->Bind(wxEVT_BUTTON, [this, on_button](wxCommandEvent&) { this->save(); on_button(); });
                grid->Add(btn, 0, wxEXPAND);
            } else
                grid->AddSpacer(1);
            return ctrl;
        };
        // The test is loaded once the assistant is closed (see run_after()).
        auto print_test = [this](const std::string& key, std::optional<Params> params) {
            if (const Test* t = find_test(key)) {
                wxWindow* parent = GetParent();
                m_after = [parent, t, params]() { run_test(parent, *t, params ? &*params : nullptr); };
                this->EndModal(wxID_CANCEL);
            }
        };

        // Print head.
        m_head_height = add_row(_L("Head clearance height"), printer.opt_float("nonplanar_head_clearance_height"), 0., 50., 0.5, 1,
                                _L("mm"), _L("Print the gauge"),
                                [print_test]() { print_test("nonplanar_head_gauge", {}); });
        m_head_radius = add_row(_L("Head clearance radius"), printer.opt_float("nonplanar_head_clearance_radius"), 0., 100., 0.5, 1,
                                _L("mm"), wxString(), nullptr);
        // Maximum slope, step 1: static gauge.
        m_free_angle = add_row(_L("Step 1: free angle on the static gauge"), stored("nonplanar_calib_free_angle", 0.), 0., 75., 1., 0,
                               "°", _L("Print the gauge"),
                               [print_test]() { print_test("nonplanar_free_angle", {}); });
        // Step 2: printed test around the free angle.
        m_printed_angle = add_row(_L("Step 2: steepest good step of the printed test"), stored("nonplanar_calib_printed_angle", 0.), 0., 75., 0.5, 1,
                                  "°", _L("Print the test"), [this, print_test]() {
                                      const double a = m_free_angle->GetValue();
                                      if (a <= 0.) {
                                          wxMessageBox(_L("Measure the free angle on the static gauge first (step 1)."),
                                                       _L("Non-planar calibration assistant"), wxOK | wxICON_INFORMATION, this);
                                          return;
                                      }
                                      // Small steps from 10° below the free angle to 5° above it.
                                      print_test("nonplanar_slope",
                                                 Params{ std::max(5., a - 10.), std::min(75., a + 5.), 2.5, 10. });
                                  });
        m_tolerance = add_row(_L("Safety tolerance"), stored("nonplanar_calib_tolerance", 3.), 0., 20., 0.5, 1, "°", wxString(), nullptr);
        // Z speed.
        double z_speed = 0.;
        if (const ConfigOptionFloats* z = printer.option<ConfigOptionFloats>("machine_max_feedrate_z"); z && ! z->values.empty())
            z_speed = z->values.front();
        m_z_speed = add_row(_L("Fastest good Z speed of the printed test"), z_speed, 0., 200., 0.5, 1, _L("mm/s"), _L("Print the test"),
                            [print_test]() { print_test("nonplanar_z_speed", {}); });
        main->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, em);

        m_result = new wxStaticText(this, wxID_ANY, wxEmptyString);
        main->Add(m_result, 0, wxALL, em);
        m_current_slope = print.opt_float("nonplanar_max_slope");

        auto buttons = new wxBoxSizer(wxHORIZONTAL);
        auto apply = new wxButton(this, wxID_APPLY, _L("Apply to the presets"));
        apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { this->save(); this->apply(); });
        buttons->Add(apply, 0, wxRIGHT, em);
        buttons->Add(new wxButton(this, wxID_CANCEL, _L("Close")));
        main->Add(buttons, 0, wxALIGN_RIGHT | wxALL, em);

        for (wxSpinCtrlDouble* ctrl : { m_free_angle, m_printed_angle, m_tolerance }) {
            ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { update_result(); });
            ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update_result(); });
        }
        update_result();
        SetSizerAndFit(main);
        CenterOnParent();
    }

    void run_after() { if (m_after) m_after(); }

private:
    std::function<void()> m_after;

    // Maximum layer slope from the measurements: the smaller of the two steps minus the tolerance, 0 = unknown.
    double slope() const
    {
        const double a = m_free_angle->GetValue(), b = m_printed_angle->GetValue();
        const double measured = a > 0. && b > 0. ? std::min(a, b) : std::max(a, b);
        return measured > 0. ? std::max(1., measured - m_tolerance->GetValue()) : 0.;
    }

    void update_result()
    {
        const double s = this->slope();
        wxString text = s <= 0. ? _L("Maximum layer slope: not measured yet.") :
            format_wxstr(_L("Maximum layer slope to apply: %1%° (now %2%°)."), wxString::Format("%.1f", s),
                         wxString::Format("%.1f", m_current_slope));
        if (m_free_angle->GetValue() > 0. && m_printed_angle->GetValue() <= 0.)
            text += "\n" + _L("Not confirmed with the printed test (step 2) yet.");
        m_result->SetLabel(text);
        Layout();
        Fit();
    }

    void save()
    {
        AppConfig& app = *wxGetApp().app_config;
        app.set("nonplanar_calib_free_angle",    float_to_string_decimal_point(m_free_angle->GetValue(), 1));
        app.set("nonplanar_calib_printed_angle", float_to_string_decimal_point(m_printed_angle->GetValue(), 1));
        app.set("nonplanar_calib_tolerance",     float_to_string_decimal_point(m_tolerance->GetValue(), 1));
    }

    void apply()
    {
        if (Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINTER)) {
            DynamicPrintConfig conf = *tab->get_config();
            conf.set_key_value("nonplanar_head_clearance_height", new ConfigOptionFloat(m_head_height->GetValue()));
            conf.set_key_value("nonplanar_head_clearance_radius", new ConfigOptionFloat(m_head_radius->GetValue()));
            if (m_z_speed->GetValue() > 0.)
                // Normal and silent (stealth) modes.
                conf.set_key_value("machine_max_feedrate_z", new ConfigOptionFloats({ m_z_speed->GetValue(), m_z_speed->GetValue() }));
            tab->load_config(conf);
        }
        if (const double s = this->slope(); s > 0.)
            if (Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
                DynamicPrintConfig conf = *tab->get_config();
                conf.set_key_value("nonplanar_max_slope", new ConfigOptionFloat(s));
                tab->load_config(conf);
                m_current_slope = s;
            }
        update_result();
        wxMessageBox(_L("The values were applied to the print and printer settings. Save the presets to keep them."),
                     _L("Non-planar calibration assistant"), wxOK | wxICON_INFORMATION, this);
    }

    wxSpinCtrlDouble* m_head_height   { nullptr };
    wxSpinCtrlDouble* m_head_radius   { nullptr };
    wxSpinCtrlDouble* m_free_angle    { nullptr };
    wxSpinCtrlDouble* m_printed_angle { nullptr };
    wxSpinCtrlDouble* m_tolerance     { nullptr };
    wxSpinCtrlDouble* m_z_speed       { nullptr };
    wxStaticText*     m_result        { nullptr };
    double            m_current_slope { 0. };
};

} // namespace

wxMenu* create_calibration_menu(wxWindow* parent)
{
    auto menu = new wxMenu();
    const std::vector<Test>& all = tests();
    for (size_t i = 0; i < all.size(); ++i) {
        if (i > 0 && all[i].key.rfind("nonplanar", 0) == 0 && all[i - 1].key.rfind("nonplanar", 0) != 0) {
            // Non-planar group: the assistant first.
            menu->AppendSeparator();
            const int aid = wxWindow::NewControlId();
            menu->Append(aid, _L("Non-planar calibration assistant") + dots,
                         _L("Measures and applies the maximum layer slope (in two steps), the print head clearance and the Z speed."));
            menu->Bind(wxEVT_MENU, [parent](wxCommandEvent&) {
                NonPlanarAssistant dlg(parent);
                dlg.ShowModal();
                dlg.run_after();
            }, aid);
        }
        const int id = wxWindow::NewControlId();
        menu->Append(id, all[i].title + dots, all[i].description);
        menu->Bind(wxEVT_MENU, [parent, i](wxCommandEvent&) { run_test(parent, tests()[i]); }, id);
    }
    return menu;
}

void show_calibration_menu(wxWindow* parent)
{
    wxMenu* menu = create_calibration_menu(parent);
    parent->PopupMenu(menu, parent->ScreenToClient(wxGetMousePosition()));
    delete menu;
}

} // namespace GUI
} // namespace Slic3r
