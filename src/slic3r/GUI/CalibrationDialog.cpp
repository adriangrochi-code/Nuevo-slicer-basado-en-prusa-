///|/ Tisma Slicer: calibration suite (temperature, pressure advance, retraction, speeds, ...).
///|/ Inspired by the calibrations of OrcaSlicer (AGPLv3). The models are generated, not loaded from files.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationDialog.hpp"

#include <cmath>
#include <functional>
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

    return tests;
}

// --- Dialog --------------------------------------------------------------------------------------------------

class CalibrationDialog : public wxDialog
{
public:
    CalibrationDialog(wxWindow* parent, const Test& test)
        : wxDialog(parent, wxID_ANY, test.title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {
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
        m_start = add_field(_L("Start value"),         test.defaults.start, 0., test.max_value, test.increment, test.digits, test.unit);
        m_end   = add_field(_L("End value"),           test.defaults.end,   0., test.max_value, test.increment, test.digits, test.unit);
        m_step  = add_field(_L("Step"),                test.defaults.step,  test.increment / 10., test.max_value, test.increment, test.digits + 1, test.unit);
        m_band  = add_field(test.band_label.empty() ? _L("Height of each step") : test.band_label,
                            test.defaults.band,  0.2, 50., 0.5, 1, _L("mm"));
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

void run_test(wxWindow* parent, const Test& test)
{
    CalibrationDialog dlg(parent, test);
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

} // namespace

wxMenu* create_calibration_menu(wxWindow* parent)
{
    auto menu = new wxMenu();
    const std::vector<Test>& all = tests();
    for (size_t i = 0; i < all.size(); ++i) {
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
