///|/ Tisma Slicer: pages of the advanced workspaces (Engineering, Structures).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "WorkspacePage.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "TismaTheme.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

WorkspacePage::WorkspacePage(wxWindow* parent, const wxString& title, const wxString& intro, const std::vector<Step>& steps)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL)
{
    SetScrollRate(0, 10);
    const int em    = wxGetApp().em_unit();
    const int wrap  = 70 * em;

    auto main = new wxBoxSizer(wxVERTICAL);

    auto title_text = new wxStaticText(this, wxID_ANY, title);
    title_text->SetFont(wxGetApp().bold_font().Scaled(1.6f));
    main->Add(title_text, 0, wxLEFT | wxRIGHT | wxTOP, 3 * em);

    auto status = new wxStaticText(this, wxID_ANY, _L("In development: not available in this version").Upper());
    status->SetFont(wxGetApp().small_font().Bold());
    status->SetName("status");
    main->Add(status, 0, wxLEFT | wxRIGHT | wxTOP, 3 * em);

    auto intro_text = new wxStaticText(this, wxID_ANY, intro);
    intro_text->Wrap(wrap);
    main->Add(intro_text, 0, wxLEFT | wxRIGHT | wxTOP, 3 * em);

    auto steps_title = new wxStaticText(this, wxID_ANY, _L("Planned workflow").Upper());
    steps_title->SetFont(wxGetApp().small_font().Bold());
    steps_title->SetName("muted");
    main->Add(steps_title, 0, wxLEFT | wxRIGHT | wxTOP, 3 * em);

    for (size_t i = 0; i < steps.size(); ++i) {
        const Step& step = steps[i];
        auto row = new wxBoxSizer(wxVERTICAL);
        auto head = new wxStaticText(this, wxID_ANY, wxString::Format("%d.  ", int(i + 1)) + step.title);
        head->SetFont(wxGetApp().bold_font());
        head->SetName("accent");
        row->Add(head, 0);
        auto desc = new wxStaticText(this, wxID_ANY, step.description);
        desc->Wrap(wrap);
        row->Add(desc, 0, wxTOP, int(0.3 * em));
        auto phase = new wxStaticText(this, wxID_ANY, format_wxstr(_L("Development phase %1%"), step.phase));
        phase->SetFont(wxGetApp().small_font());
        phase->SetName("muted");
        row->Add(phase, 0, wxTOP, int(0.3 * em));
        main->Add(row, 0, wxLEFT | wxRIGHT | wxTOP, 3 * em);
    }

    auto footer = new wxStaticText(this, wxID_ANY,
        _L("The results of the simulations will be estimations based on simplified models and approximate material data, "
           "not guarantees of strength. The plan and its status are in docs/IMPLEMENTATION_ROADMAP.md."));
    footer->Wrap(wrap);
    footer->SetFont(wxGetApp().small_font());
    footer->SetName("muted");
    main->Add(footer, 0, wxALL, 3 * em);

    SetSizer(main);
    FitInside();
    apply_colors();
}

void WorkspacePage::apply_colors()
{
    const bool dark = wxGetApp().dark_mode();
    SetBackgroundColour(TismaTheme::panel_bg(dark));
    for (wxWindow* child : GetChildren()) {
        const wxString name = child->GetName();
        if (name == "status")
            child->SetForegroundColour(TismaTheme::accent_text(dark));
        else if (name == "accent")
            child->SetForegroundColour(TismaTheme::accent_text(dark));
        else if (name == "muted")
            child->SetForegroundColour(TismaTheme::text_muted(dark));
        else
            child->SetForegroundColour(TismaTheme::text(dark));
    }
    Refresh();
}

void WorkspacePage::sys_color_changed()
{
    apply_colors();
}

WorkspacePage* create_structures_page(wxWindow* parent)
{
    return new WorkspacePage(parent, _L("Structures"),
        _L("Internal structures adapted to the loads: the outside of the part is kept and the infill is generated "
           "from a first analysis, then validated with a second analysis of the real structure."),
        {
            { _L("Preliminary analysis"),
              _L("Analysis of the part with the properties of a homogeneous reference infill, to find the critical zones."), 6 },
            { _L("Adaptive infill"),
              _L("Infill density and orientation follow the stresses. First version: internal modifiers by density bands."), 6 },
            { _L("Local reinforcements"),
              _L("More walls and ribs around loads, fixtures and critical zones."), 6 },
            { _L("3D lattice"),
              _L("Cells and struts of variable thickness, connected to the shell."), 6 },
            { _L("Validation"),
              _L("Second analysis of the sliced part with its real walls and infill; weight and estimated stiffness compared "
                 "with a uniform infill."), 6 },
        });
}

} // namespace GUI
} // namespace Slic3r
