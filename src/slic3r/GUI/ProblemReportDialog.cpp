///|/ Tisma Slicer: "Report a problem".
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ProblemReportDialog.hpp"

#include <map>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "TismaCloudDialog.hpp"
#include "TismaTheme.hpp"
#include "TismaWidgets.hpp"
#include "format.hpp"
#include "../Utils/TismaCloud.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/TextInput.hpp"

namespace Slic3r {
namespace GUI {

std::string problem_report_attachment()
{
    std::string out = std::string("Tisma Slicer ") + TISMA_VERSION_TEXT + ", build " + SLIC3R_BUILD_ID + "\n";
    out += into_u8(wxGetOsDescription()) + "\n";
    if (const PresetBundle *bundle = wxGetApp().preset_bundle) {
        const Preset &printer = bundle->printers.get_edited_preset();
        out += "Printer: " + printer.name + " (" + printer.config.opt_string("printer_model") + ")";
        if (printer.printer_technology() == ptFFF)
            out += ", print: " + bundle->prints.get_edited_preset().name + ", filament: " +
                   bundle->filaments.get_edited_preset().name;
        else
            out += ", print: " + bundle->sla_prints.get_edited_preset().name + ", material: " +
                   bundle->sla_materials.get_edited_preset().name;
        out += "\n";
    }
    const std::string log = recent_log();
    out += "--- log of this session ---\n" + (log.empty() ? std::string("(empty)\n") : log);
    return out;
}

namespace {

// The kinds of the server (reports.KINDS), in the order of the chips.
const char *KINDS[] = { "bug", "crash", "suggestion" };

class ProblemReportDialog : public wxDialog
{
public:
    explicit ProblemReportDialog(wxWindow *parent)
        : wxDialog(parent, wxID_ANY, _L("Report a problem"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        SetFont(wxGetApp().normal_font());
        const int  em   = wxGetApp().em_unit();
        const bool dark = wxGetApp().dark_mode();
        SetBackgroundColour(TismaTheme::card_bg(dark));

        auto *title = new TismaTitle(this, _L("Report a problem"));
        auto *intro = new wxStaticText(this, wxID_ANY,
            _L("Tell what went wrong, or what you would like Tisma to do. It goes to the Tisma team, which groups the "
               "reports of the same problem to fix the most frequent ones first. E-mails, addresses, the user name in "
               "the paths, keys and serial numbers are removed before it is kept."));
        intro->SetForegroundColour(TismaTheme::text_muted(dark));
        intro->Wrap(44 * em);

        auto *fields = new wxBoxSizer(wxVERTICAL);
        auto caption = [this, fields, em](const wxString &text) {
            fields->Add(new TismaTitle(this, text, TismaTitle::Style::Caption), 0, wxTOP, em);
        };
        auto single = [this, fields, em](const wxString &hint) {
            auto *input = new ::TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(44 * em, -1));
            input->GetTextCtrl()->SetHint(hint);
            wxGetApp().UpdateDarkUI(input);
            fields->Add(input, 0, wxEXPAND | wxTOP, em / 4);
            return input;
        };
        auto multi = [this, fields, em](int lines) {
            auto *input = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(44 * em, lines * 2 * em),
                                         wxTE_MULTILINE | wxBORDER_SIMPLE);
            wxGetApp().UpdateDarkUI(input);
            fields->Add(input, 1, wxEXPAND | wxTOP, em / 4);
            return input;
        };

        caption(_L("Kind"));
        m_kind = new TismaChoiceChips(this, { _L("Something does not work"), _L("Tisma closed"), _L("Suggestion") },
                                      [this](int idx) { m_kind_idx = idx; });
        m_kind->set_selection(0);
        fields->Add(m_kind, 0, wxTOP, em / 4);
        caption(_L("In a few words"));
        m_title = single(_L("e.g. The supports are missing under the overhang"));
        caption(_L("What happened"));
        m_description = multi(4);
        caption(_L("Steps to make it happen again"));
        m_steps = multi(3);
        caption(_L("What you expected"));
        m_expected = single(wxEmptyString);

        // The server: the one of the account, asked only when there is none.
        const CloudAccess access = cloud_access();
        if (access.url.empty()) {
            caption(_L("Tisma Cloud server"));
            m_server = single("https://cloud.example.com");
        }

        m_attach = new wxCheckBox(this, wxID_ANY, _L("Attach the version, the system, the printer in use and the log of this session"));
        m_attach->SetValue(true);
        auto *show = tisma_dialog_button(this, wxID_ANY, _L("See what is attached"));
        auto *attach_row = new wxBoxSizer(wxHORIZONTAL);
        attach_row->Add(m_attach, 1, wxALIGN_CENTER_VERTICAL);
        attach_row->Add(show, 0, wxLEFT, em);

        m_state = new wxStaticText(this, wxID_ANY, access.logged_in() ?
            format_wxstr(_L("Sent with the account %1%: its reports and their state are in the copy of its data."), access.email) :
            _L("Sent without an account."));
        m_state->SetForegroundColour(TismaTheme::text_muted(dark));
        m_state->Wrap(44 * em);

        m_send = tisma_dialog_button(this, wxID_OK, _L("Send"), true);
        auto *cancel = tisma_dialog_button(this, wxID_CANCEL, _L("Cancel"));
        auto *buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer();
        buttons->Add(cancel, 0, wxRIGHT, em);
        buttons->Add(m_send);

        const int border = 2 * em;
        auto *top = new wxBoxSizer(wxVERTICAL);
        top->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, border);
        top->AddSpacer(em / 2);
        top->Add(intro, 0, wxLEFT | wxRIGHT, border);
        top->Add(fields, 1, wxEXPAND | wxLEFT | wxRIGHT, border);
        top->Add(attach_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, border);
        top->Add(m_state, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, border);
        top->Add(buttons, 0, wxEXPAND | wxALL, border);
        SetSizerAndFit(top);
        tisma_dialog_keys(this);
        SetDefaultItem(m_send);

        m_send->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { send(); });
        cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });
        show->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_attachment(); });
        CenterOnParent();
        m_title->GetTextCtrl()->SetFocus();
    }

private:
    void set_state(const wxString &text, bool error)
    {
        m_state->SetLabel(text);
        // What the user has to correct in the text color, the rest muted.
        m_state->SetForegroundColour(error ? TismaTheme::text(wxGetApp().dark_mode()) : TismaTheme::text_muted(wxGetApp().dark_mode()));
        m_state->Wrap(44 * wxGetApp().em_unit());
        Layout();
        Fit();
    }

    void show_attachment()
    {
        const int em = wxGetApp().em_unit();
        wxDialog dlg(this, wxID_ANY, _L("What is attached"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        dlg.SetFont(wxGetApp().normal_font());
        dlg.SetBackgroundColour(TismaTheme::card_bg(wxGetApp().dark_mode()));
        auto *text = new wxTextCtrl(&dlg, wxID_ANY, wxString::FromUTF8(problem_report_attachment()), wxDefaultPosition,
                                    wxSize(70 * em, 30 * em), wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP | wxBORDER_SIMPLE);
        text->SetFont(wxGetApp().code_font());
        wxGetApp().UpdateDarkUI(text);
        auto *close = tisma_dialog_button(&dlg, wxID_CANCEL, _L("Close"));
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(text, 1, wxEXPAND | wxALL, 2 * em);
        sizer->Add(close, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 2 * em);
        dlg.SetSizerAndFit(sizer);
        tisma_dialog_keys(&dlg);
        close->Bind(wxEVT_BUTTON, [&dlg](wxCommandEvent &) { dlg.EndModal(wxID_CANCEL); });
        dlg.CenterOnParent();
        dlg.ShowModal();
    }

    void send()
    {
        const std::string title = into_u8(m_title->GetTextCtrl()->GetValue().Strip(wxString::both));
        if (title.size() < 3) {
            set_state(_L("Write in a few words what happened."), true);
            m_title->GetTextCtrl()->SetFocus();
            return;
        }
        CloudAccess access = cloud_access();
        if (m_server != nullptr) {
            access.url = into_u8(m_server->GetTextCtrl()->GetValue().Strip(wxString::both));
            if (access.url.empty()) {
                set_state(_L("Enter the address of the Tisma Cloud server."), true);
                return;
            }
            if (access.url.rfind("http://", 0) != 0 && access.url.rfind("https://", 0) != 0)
                access.url = "https://" + access.url;
        }
        std::map<std::string, std::string> fields {
            { "app", "pc" }, { "kind", KINDS[std::clamp(m_kind_idx, 0, 2)] }, { "title", title },
            { "description", into_u8(m_description->GetValue()) }, { "steps", into_u8(m_steps->GetValue()) },
            { "expected", into_u8(m_expected->GetTextCtrl()->GetValue()) },
            { "app_version", TISMA_VERSION }, { "os", into_u8(wxGetOsDescription()).substr(0, 80) } };
        if (m_attach->GetValue())
            // The server keeps up to 200 000 characters of it: the end, where the problem is.
            fields["log"] = tail(problem_report_attachment(), 190000);
        int similar = 0;
        std::string error;
        {
            wxBusyCursor wait;
            if (! access.client().report(fields, similar, error)) {
                set_state(format_wxstr(_L("The report could not be sent: %1%"), cloud_error_text(error)), true);
                return;
            }
        }
        if (m_server != nullptr)
            wxGetApp().app_config->set("tisma_cloud_url", access.url);
        wxString thanks = _L("Thank you: the report was sent to the Tisma team.");
        if (similar > 0)
            thanks += "\n" + format_wxstr(_L("The same problem was reported %1% times before: it is among the ones to fix."), similar);
        EndModal(wxID_OK);
        MessageDialog(GetParent(), thanks, _L("Report a problem"), wxOK | wxICON_INFORMATION).ShowModal();
    }

    static std::string tail(const std::string &text, size_t max)
    {
        if (text.size() <= max)
            return text;
        size_t start = text.size() - max;
        // Not in the middle of a UTF-8 character.
        while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80)
            ++ start;
        return text.substr(0, text.find('\n') + 1) + "...\n" + text.substr(start);
    }

    TismaChoiceChips *m_kind { nullptr };
    int               m_kind_idx { 0 };
    ::TextInput      *m_title { nullptr };
    wxTextCtrl       *m_description { nullptr };
    wxTextCtrl       *m_steps { nullptr };
    ::TextInput      *m_expected { nullptr };
    ::TextInput      *m_server { nullptr };
    wxCheckBox       *m_attach { nullptr };
    wxStaticText     *m_state { nullptr };
    ::Button         *m_send { nullptr };
};

} // namespace

void show_problem_report_dialog(wxWindow *parent)
{
    ProblemReportDialog dlg(parent);
    dlg.ShowModal();
}

} // namespace GUI
} // namespace Slic3r
