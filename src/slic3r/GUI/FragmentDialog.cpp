///|/ Tisma Slicer: dialog of "Split to fit the printer" (pieces joined by connectors).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FragmentDialog.hpp"

#include <string>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

namespace {

// Remembered between uses in the application settings.
void load_params(Fragment::Params &params)
{
    const AppConfig *config = wxGetApp().app_config;
    auto number = [config](const char *key, double &value) {
        const std::string s = config->get(key);
        if (!s.empty())
            try { value = std::stod(s); } catch (...) {}
    };
    const std::string joint = config->get("fragment_joint");
    if (joint == "plug")
        params.joint = Fragment::Joint::Plug;
    else if (joint == "none")
        params.joint = Fragment::Joint::None;
    else
        params.joint = Fragment::Joint::Dowel;
    number("fragment_joint_diameter", params.joint_diameter);
    number("fragment_joint_depth", params.joint_depth);
    number("fragment_clearance", params.clearance);
}

void save_params(const Fragment::Params &params)
{
    AppConfig *config = wxGetApp().app_config;
    config->set("fragment_joint", params.joint == Fragment::Joint::Plug ? "plug" : params.joint == Fragment::Joint::None ? "none" : "dowel");
    config->set("fragment_joint_diameter", std::to_string(params.joint_diameter));
    config->set("fragment_joint_depth", std::to_string(params.joint_depth));
    config->set("fragment_clearance", std::to_string(params.clearance));
}

class FragmentDialog : public wxDialog
{
public:
    FragmentDialog(wxWindow *parent, const ModelObject &object, size_t instance, Fragment::Params &params)
        : wxDialog(parent, wxID_ANY, _L("Split to fit the printer"), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_object(object), m_instance(instance), m_params(params)
    {
        auto *top = new wxBoxSizer(wxVERTICAL);
        const Vec3d &s = params.max_size;
        auto *intro = new wxStaticText(this, wxID_ANY,
            format_wxstr(_L("The object is cut into pieces that fit the printable volume (%1% x %2% x %3% mm), where its "
                            "section is smallest, and every cut gets connectors to align the pieces when gluing them."),
                         int(s.x()), int(s.y()), int(s.z())));
        intro->Wrap(FromDIP(440));
        top->Add(intro, 0, wxALL, FromDIP(10));

        auto *grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
        grid->AddGrowableCol(1);
        grid->Add(new wxStaticText(this, wxID_ANY, _L("Connectors")), 0, wxALIGN_CENTER_VERTICAL);
        m_joint = new wxChoice(this, wxID_ANY);
        m_joint->Append(_L("Pins (separate, in holes of both pieces)"));
        m_joint->Append(_L("Pegs (on one piece, hole in the other)"));
        m_joint->Append(_L("None (glue only)"));
        m_joint->SetSelection(params.joint == Fragment::Joint::Dowel ? 0 : params.joint == Fragment::Joint::Plug ? 1 : 2);
        grid->Add(m_joint, 1, wxEXPAND);
        auto spin = [this, grid](const wxString &label, double value, double min, double max, double inc) {
            grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            auto *ctrl = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
                                              min, max, value, inc);
            ctrl->SetDigits(2);
            grid->Add(ctrl, 1, wxEXPAND);
            ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent &) { update(); });
            return ctrl;
        };
        m_diameter  = spin(_L("Diameter (mm)"), params.joint_diameter, 2., 40., 0.5);
        m_depth     = spin(_L("Depth into each piece (mm)"), params.joint_depth, 2., 40., 0.5);
        m_clearance = spin(_L("Clearance of the holes (mm)"), params.clearance, 0., 1., 0.05);
        top->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

        m_summary = new wxStaticText(this, wxID_ANY, wxEmptyString);
        top->Add(m_summary, 1, wxEXPAND | wxALL, FromDIP(10));

        auto *buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
        if (auto *ok = dynamic_cast<wxButton*>(FindWindow(wxID_OK)))
            ok->SetLabel(_L("Split"));
        top->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(10));
        SetSizerAndFit(top);
        SetMinSize(wxSize(FromDIP(480), -1));
        Fit();
        CenterOnParent();

        m_joint->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { update(); });
        update();
    }

    const Fragment::Plan &plan() const { return m_plan; }

private:
    const ModelObject &m_object;
    size_t             m_instance;
    Fragment::Params  &m_params;
    Fragment::Plan     m_plan;
    wxChoice          *m_joint{ nullptr };
    wxSpinCtrlDouble  *m_diameter{ nullptr };
    wxSpinCtrlDouble  *m_depth{ nullptr };
    wxSpinCtrlDouble  *m_clearance{ nullptr };
    wxStaticText      *m_summary{ nullptr };

    void update()
    {
        const int joint          = m_joint->GetSelection();
        m_params.joint           = joint == 0 ? Fragment::Joint::Dowel : joint == 1 ? Fragment::Joint::Plug : Fragment::Joint::None;
        m_params.joint_diameter  = m_diameter->GetValue();
        m_params.joint_depth     = m_depth->GetValue();
        m_params.clearance       = m_clearance->GetValue();
        for (wxSpinCtrlDouble *c : { m_diameter, m_depth, m_clearance })
            c->Enable(m_params.joint != Fragment::Joint::None);
        {
            wxBusyCursor wait;
            m_plan = Fragment::plan(m_object, m_instance, m_params);
        }
        wxString text;
        bool     can_split = false;
        if (!m_plan.error.empty())
            text = _L("It cannot be split:") + " " + from_u8(m_plan.error) + ".";
        else if (!m_plan.needed())
            text = _L("The object already fits the printer.");
        else {
            can_split = true;
            text = format_wxstr(_L("%1% pieces (%2% x %3% x %4%), %5% cuts, %6% connectors."), m_plan.piece_count(),
                                m_plan.pieces[0], m_plan.pieces[1], m_plan.pieces[2], m_plan.cuts.size(), m_plan.joint_count());
            int without = 0;
            for (const Fragment::CutPlane &cut : m_plan.cuts)
                without += cut.sections_without_joint;
            if (m_params.joint != Fragment::Joint::None && without > 0)
                text += "\n" + format_wxstr(_L("%1% parts of the cuts are too thin for a connector: glue only."), without);
            if (m_params.joint == Fragment::Joint::Dowel && m_plan.joint_count() > 0)
                text += "\n" + _L("The pins are added as separate objects.");
            if (size_t(m_plan.piece_count()) > MAX_NUMBER_OF_BEDS)
                text += "\n" + format_wxstr(_L("More pieces than the %1% beds of a project: some may be left outside "
                                                "the beds, to be printed in another project."), MAX_NUMBER_OF_BEDS);
        }
        m_summary->SetLabel(text);
        m_summary->Wrap(FromDIP(440));
        if (wxWindow *ok = FindWindow(wxID_OK))
            ok->Enable(can_split);
        Layout();
        Fit();
    }
};

} // namespace

bool show_fragment_dialog(wxWindow *parent, const ModelObject &object, size_t instance, Fragment::Params &params,
                          Fragment::Plan &plan)
{
    load_params(params);
    FragmentDialog dlg(parent, object, instance, params);
    if (dlg.ShowModal() != wxID_OK)
        return false;
    save_params(params);
    plan = dlg.plan();
    return !plan.cuts.empty();
}

} // namespace GUI
} // namespace Slic3r
