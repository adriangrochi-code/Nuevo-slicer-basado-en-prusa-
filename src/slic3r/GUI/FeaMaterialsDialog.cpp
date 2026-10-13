///|/ Tisma Slicer: editable material table of the structural analysis (Engineering).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FeaMaterialsDialog.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <tisma_fea/Materials.hpp>

#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

namespace {

// Columns of the table and of the JSON file.
enum Column { cKey, cName, cExy, cEz, cPoisson, cSxy, cSz, cTg, cTmax, cDensity, cPrintMin, cPrintMax, cLinear, cNote, cCount };

const char *json_key(int c)
{
    static const char *keys[cCount] = { "key", "name", "E_xy", "E_z", "poisson", "strength_xy", "strength_z", "glass_transition",
                                        "max_service_temperature", "density", "print_temperature_min", "print_temperature_max",
                                        "linear_analysis_valid", "note" };
    return keys[c];
}

double *number(Fea::Material &m, int c)
{
    switch (c) {
    case cExy:      return &m.E_xy;
    case cEz:       return &m.E_z;
    case cPoisson:  return &m.poisson;
    case cSxy:      return &m.strength_xy;
    case cSz:       return &m.strength_z;
    case cTg:       return &m.glass_transition;
    case cTmax:     return &m.max_service_temperature;
    case cDensity:  return &m.density;
    case cPrintMin: return &m.print_temperature_min;
    case cPrintMax: return &m.print_temperature_max;
    default:        return nullptr;
    }
}

const Fea::Material *builtin(const std::string &key)
{
    for (const Fea::Material &m : Fea::builtin_materials())
        if (m.key == key)
            return &m;
    return nullptr;
}

bool same_values(const Fea::Material &a, const Fea::Material &b)
{
    if (a.name != b.name || a.note != b.note || a.linear_analysis_valid != b.linear_analysis_valid)
        return false;
    for (int c = cExy; c <= cPrintMax; ++ c)
        if (std::abs(*number(const_cast<Fea::Material&>(a), c) - *number(const_cast<Fea::Material&>(b), c)) > 1e-9)
            return false;
    return true;
}

std::string format_number(double v)
{
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

class FeaMaterialsDialog : public wxDialog
{
public:
    explicit FeaMaterialsDialog(wxWindow *parent)
        : wxDialog(parent, wxID_ANY, _L("Materials of the structural analysis"), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        wxGetApp().UpdateDlgDarkUI(this);
        const int em = wxGetApp().em_unit();
        auto main = new wxBoxSizer(wxVERTICAL);

        auto info = new wxStaticText(this, wxID_ANY,
            _L("Values of a solid printed part at 23 °C. Change them to the ones of your filament (data sheet or your own "
               "test pieces) or add materials. XY: along the layers; Z: across them (layer adhesion). "
               "Changed rows are bold; \"Reset / remove\" brings back the values of Tisma or removes your material."));
        info->Wrap(90 * em);
        main->Add(info, 0, wxALL, em);

        m_grid = new wxGrid(this, wxID_ANY, wxDefaultPosition, wxSize(110 * em, 30 * em));
        m_grid->CreateGrid(0, cCount);
        const wxString labels[cCount] = { _L("Key"), _L("Name"), _L("E XY [MPa]"), _L("E Z [MPa]"), _L("Poisson"),
                                          _L("Strength XY [MPa]"), _L("Strength Z [MPa]"), _L("Tg [°C]"),
                                          _L("Max. service [°C]"), _L("Density [g/cm³]"), _L("Print min [°C]"),
                                          _L("Print max [°C]"), _L("Linear"), _L("Note") };
        for (int c = 0; c < cCount; ++ c)
            m_grid->SetColLabelValue(c, labels[c]);
        m_grid->SetColFormatBool(cLinear);
        m_grid->HideRowLabels();
        for (const Fea::Material &m : Fea::materials())
            append_row(m);
        m_grid->AutoSizeColumns(false);
        m_grid->SetColSize(cName, 16 * em);
        m_grid->SetColSize(cNote, 30 * em);
        m_grid->Bind(wxEVT_GRID_CELL_CHANGED, [this](wxGridEvent &evt) { update_row_style(evt.GetRow()); evt.Skip(); });
        main->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, em);

        auto row_buttons = new wxBoxSizer(wxHORIZONTAL);
        auto add = new wxButton(this, wxID_ANY, _L("Add material"));
        auto reset = new wxButton(this, wxID_ANY, _L("Reset / remove"));
        row_buttons->Add(add, 0, wxRIGHT, em);
        row_buttons->Add(reset, 0);
        main->Add(row_buttons, 0, wxALL, em);
        wxGetApp().UpdateDarkUI(add);
        wxGetApp().UpdateDarkUI(reset);
        add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { add_material(); });
        reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reset_or_remove(); });

        if (wxSizer *btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL))
            main->Add(btns, 0, wxEXPAND | wxALL, em);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) { if (accept()) evt.Skip(); }, wxID_OK);

        SetSizerAndFit(main);
        CenterOnParent();
    }

    std::vector<Fea::Material> user() const { return m_user; }

private:
    wxGrid                    *m_grid { nullptr };
    std::vector<Fea::Material> m_user;

    void append_row(const Fea::Material &m)
    {
        const int r = m_grid->GetNumberRows();
        m_grid->AppendRows(1);
        set_row(r, m);
        // The key of a built-in material is fixed (it is the filament type of the profiles).
        m_grid->SetReadOnly(r, cKey, builtin(m.key) != nullptr);
        update_row_style(r);
    }

    void set_row(int r, const Fea::Material &m)
    {
        m_grid->SetCellValue(r, cKey, wxString::FromUTF8(m.key));
        m_grid->SetCellValue(r, cName, wxString::FromUTF8(m.name));
        for (int c = cExy; c <= cPrintMax; ++ c)
            m_grid->SetCellValue(r, c, wxString::FromUTF8(format_number(*number(const_cast<Fea::Material&>(m), c))));
        m_grid->SetCellValue(r, cLinear, m.linear_analysis_valid ? "1" : "");
        m_grid->SetCellValue(r, cNote, wxString::FromUTF8(m.note));
        m_grid->SetCellEditor(r, cLinear, new wxGridCellBoolEditor());
        m_grid->SetCellRenderer(r, cLinear, new wxGridCellBoolRenderer());
    }

    // The material of a row; false and the reason when a number cannot be read.
    bool read_row(int r, Fea::Material &m, std::string &error) const
    {
        m.key  = m_grid->GetCellValue(r, cKey).Trim().Trim(false).Upper().ToUTF8().data();
        m.name = m_grid->GetCellValue(r, cName).Trim().Trim(false).ToUTF8().data();
        m.note = m_grid->GetCellValue(r, cNote).ToUTF8().data();
        m.linear_analysis_valid = ! m_grid->GetCellValue(r, cLinear).empty() && m_grid->GetCellValue(r, cLinear) != "0";
        for (int c = cExy; c <= cPrintMax; ++ c) {
            wxString text = m_grid->GetCellValue(r, c).Trim().Trim(false);
            text.Replace(",", ".");
            double v = 0.;
            if (! text.ToCDouble(&v)) {
                error = format(_u8L("Row %1%, %2%: \"%3%\" is not a number"), r + 1, m_grid->GetColLabelValue(c).ToUTF8().data(),
                               text.ToUTF8().data());
                return false;
            }
            *number(m, c) = v;
        }
        return true;
    }

    void update_row_style(int r)
    {
        Fea::Material m;
        std::string   error;
        const bool    ok      = read_row(r, m, error);
        const Fea::Material *b = builtin(m.key);
        const bool    changed = ! ok || b == nullptr || ! same_values(m, *b);
        wxFont font = m_grid->GetDefaultCellFont();
        if (changed)
            font.MakeBold();
        for (int c = 0; c < cCount; ++ c)
            m_grid->SetCellFont(r, c, font);
        m_grid->ForceRefresh();
    }

    void add_material()
    {
        // A copy of the selected row (or of PLA) as a start.
        const int sel = m_grid->GetGridCursorRow();
        Fea::Material m;
        std::string   error;
        if (sel < 0 || ! read_row(sel, m, error))
            m = *builtin("PLA");
        int n = 1;
        std::string key;
        do {
            key = "MY" + std::to_string(n ++);
        } while (row_of(key) >= 0);
        m.key  = key;
        m.name = m.name + " (" + _u8L("mine") + ")";
        m.user = true;
        append_row(m);
        const int r = m_grid->GetNumberRows() - 1;
        m_grid->MakeCellVisible(r, cName);
        m_grid->SetGridCursor(r, cName);
    }

    int row_of(const std::string &key) const
    {
        for (int r = 0; r < m_grid->GetNumberRows(); ++ r)
            if (m_grid->GetCellValue(r, cKey).Upper().ToUTF8().data() == key)
                return r;
        return -1;
    }

    void reset_or_remove()
    {
        const int r = m_grid->GetGridCursorRow();
        if (r < 0)
            return;
        const std::string key = m_grid->GetCellValue(r, cKey).Upper().ToUTF8().data();
        if (const Fea::Material *b = builtin(key)) {
            set_row(r, *b);
            update_row_style(r);
        } else
            m_grid->DeleteRows(r);
    }

    bool accept()
    {
        if (m_grid->IsCellEditControlEnabled())
            m_grid->SaveEditControlValue();
        std::vector<Fea::Material> user;
        for (int r = 0; r < m_grid->GetNumberRows(); ++ r) {
            Fea::Material m;
            std::string   error;
            const bool read = read_row(r, m, error);
            if (! read || ! Fea::validate_material(m, error)) {
                // read_row() names the row itself; the messages of validate_material() are translated here.
                show_error(read ? format(_u8L("Row %1% (%2%): %3%"), r + 1, m.key, _u8L(error)) : error);
                m_grid->SetGridCursor(r, cKey);
                return false;
            }
            if (row_of(m.key) != r) {
                show_error(format(_u8L("Row %1%: the key %2% is repeated"), r + 1, m.key));
                return false;
            }
            const Fea::Material *b = builtin(m.key);
            if (b == nullptr || ! same_values(m, *b))
                user.push_back(m);
        }
        m_user = std::move(user);
        return true;
    }

    void show_error(const std::string &error)
    {
        wxMessageDialog(this, wxString::FromUTF8(error), _L("Materials of the structural analysis"), wxOK | wxICON_WARNING).ShowModal();
    }
};

} // namespace

std::string fea_materials_path()
{
    return (boost::filesystem::path(Slic3r::data_dir()) / "fea_materials.json").string();
}

bool load_fea_materials(std::string &error, bool force)
{
    static bool loaded = false;
    if (loaded && ! force)
        return true;
    loaded = true;
    const std::string path = fea_materials_path();
    if (! boost::filesystem::exists(path)) {
        Fea::set_user_materials({});
        return true;
    }
    try {
        boost::nowide::ifstream in(path);
        const nlohmann::json j = nlohmann::json::parse(in);
        std::vector<Fea::Material> user;
        for (const nlohmann::json &e : j.at("materials")) {
            Fea::Material m;
            if (const Fea::Material *b = builtin(e.value("key", std::string())))
                m = *b;
            m.key  = e.value(json_key(cKey), m.key);
            m.name = e.value(json_key(cName), m.name);
            m.note = e.value(json_key(cNote), m.note);
            m.linear_analysis_valid = e.value(json_key(cLinear), m.linear_analysis_valid);
            for (int c = cExy; c <= cPrintMax; ++ c)
                *number(m, c) = e.value(json_key(c), *number(m, c));
            std::string why;
            if (! Fea::validate_material(m, why)) {
                error = path + ": " + m.key + ": " + why;
                continue;
            }
            user.push_back(m);
        }
        Fea::set_user_materials(user);
    } catch (const std::exception &ex) {
        error = path + ": " + ex.what();
        return false;
    }
    return error.empty();
}

bool save_fea_materials(std::string &error)
{
    nlohmann::json list = nlohmann::json::array();
    for (Fea::Material &m : Fea::user_materials()) {
        nlohmann::json e;
        e[json_key(cKey)]    = m.key;
        e[json_key(cName)]   = m.name;
        for (int c = cExy; c <= cPrintMax; ++ c)
            e[json_key(c)] = *number(m, c);
        e[json_key(cLinear)] = m.linear_analysis_valid;
        e[json_key(cNote)]   = m.note;
        list.push_back(e);
    }
    const nlohmann::json j = { { "version", 1 }, { "materials", list } };
    const std::string path = fea_materials_path();
    try {
        boost::nowide::ofstream out(path);
        out << j.dump(2) << "\n";
        if (! out)
            throw std::runtime_error("cannot write the file");
    } catch (const std::exception &ex) {
        error = path + ": " + ex.what();
        return false;
    }
    return true;
}

bool edit_fea_materials(wxWindow *parent)
{
    std::string error;
    load_fea_materials(error);
    FeaMaterialsDialog dlg(parent);
    if (dlg.ShowModal() != wxID_OK)
        return false;
    Fea::set_user_materials(dlg.user());
    if (! save_fea_materials(error))
        show_error(parent, error);
    return true;
}

} // namespace GUI
} // namespace Slic3r
