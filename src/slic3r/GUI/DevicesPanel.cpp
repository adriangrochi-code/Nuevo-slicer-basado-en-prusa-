///|/ Tisma Slicer: Devices workspace.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "DevicesPanel.hpp"

#include <boost/filesystem.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "NotificationManager.hpp"
#include "PhysicalPrinterDialog.hpp"
#include "Plater.hpp"
#include "TismaTheme.hpp"
#include "USBPrintDialog.hpp"
#include "WebViewPanel.hpp"
#include "format.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/BambuLan.hpp"

#include <thread>

namespace Slic3r {
namespace GUI {

DevicesPanel::DevicesPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY)
{
    const int  em   = wxGetApp().em_unit();
    const bool dark = wxGetApp().dark_mode();
    SetBackgroundColour(TismaTheme::panel_bg(dark));

    auto *main = new wxBoxSizer(wxHORIZONTAL);

    // Left: the printers.
    auto *left = new wxBoxSizer(wxVERTICAL);
    auto *caption = new wxStaticText(this, wxID_ANY, _L("Devices").Upper());
    caption->SetFont(wxGetApp().small_font().Bold());
    caption->SetForegroundColour(TismaTheme::text_muted(dark));
    left->Add(caption, 0, wxBOTTOM, em / 2);
    m_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(24 * em, -1));
    wxGetApp().UpdateDarkUI(m_list);
    left->Add(m_list, 1, wxEXPAND | wxBOTTOM, em / 2);
    auto *btn_add = new wxButton(this, wxID_ANY, "+ " + _L("Add network printer"));
    btn_add->SetToolTip(_L("OctoPrint, Klipper (Moonraker: Mainsail, Fluidd), PrusaLink, Duet, Repetier, MKS, FlashAir, AstroBox. "
                           "The printers of the local network can be searched in the dialog."));
    left->Add(btn_add, 0, wxEXPAND | wxBOTTOM, em / 4);
    m_btn_edit = new wxButton(this, wxID_ANY, _L("Edit printer") + dots);
    left->Add(m_btn_edit, 0, wxEXPAND | wxBOTTOM, em / 4);
    auto *btn_usb = new wxButton(this, wxID_ANY, _L("Print via USB") + dots);
    left->Add(btn_usb, 0, wxEXPAND);
    main->Add(left, 0, wxEXPAND | wxALL, em);

    // Right: the selected printer.
    auto *right = new wxBoxSizer(wxVERTICAL);
    auto *bar = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(this, wxID_ANY, "");
    m_title->SetFont(wxGetApp().bold_font());
    bar->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
    m_start = new wxCheckBox(this, wxID_ANY, _L("Start printing"));
    m_start->SetValue(wxGetApp().app_config->get("tisma_devices_start_print") != "0");
    bar->Add(m_start, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);
    m_btn_send = new wxButton(this, wxID_ANY, _L("Send sliced G-code"));
    bar->Add(m_btn_send, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    m_btn_browser = new wxButton(this, wxID_ANY, _L("Open in browser"));
    bar->Add(m_btn_browser, 0, wxALIGN_CENTER_VERTICAL);
    right->Add(bar, 0, wxEXPAND | wxBOTTOM, em / 2);
    m_hint = new wxStaticText(this, wxID_ANY, "");
    m_hint->SetForegroundColour(TismaTheme::text_muted(dark));
    right->Add(m_hint, 0, wxEXPAND | wxBOTTOM, em / 2);
    m_web = new PrinterWebViewPanel(this, L"");
    m_web->Hide();
    right->Add(m_web, 1, wxEXPAND);
    main->Add(right, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, em);

    SetSizer(main);

    m_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { on_select(); });
    m_list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent &) { edit_printer(); });
    btn_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { add_network_printer(); });
    m_btn_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { edit_printer(); });
    btn_usb->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { USBPrintDialog::run(wxGetApp().mainframe); });
    m_btn_send->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { send_sliced(); });
    m_btn_browser->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { open_in_browser(); });
    m_start->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
        wxGetApp().app_config->set("tisma_devices_start_print", m_start->GetValue() ? "1" : "0");
    });
    // The list follows the printers added or edited elsewhere (the printer combo of the sidebar).
    Bind(wxEVT_SHOW, [this](wxShowEvent &evt) {
        if (evt.IsShown())
            update_list();
        evt.Skip();
    });

    update_list();
}

void DevicesPanel::update_list()
{
    // Keep the selection by name.
    std::string selected;
    if (int sel = m_list->GetSelection(); sel != wxNOT_FOUND && size_t(sel) < m_entries.size())
        selected = m_entries[sel].type == Entry::Type::USB ? std::string("\\usb") : m_entries[sel].name;

    m_entries.clear();
    m_list->Clear();
    const std::string usb_port = wxGetApp().app_config->get("usb_print_port");
    m_entries.push_back({ Entry::Type::USB, {} });
    m_list->Append(usb_port.empty() ? _L("USB printer (not connected yet)") : format_wxstr(_L("USB printer (%1%)"), from_u8(usb_port)));
    for (const PhysicalPrinter &printer : wxGetApp().preset_bundle->physical_printers) {
        const auto *host = printer.config.option<ConfigOptionEnum<PrintHostType>>("host_type");
        const std::string address = printer.config.opt_string("print_host");
        m_entries.push_back({ Entry::Type::Network, printer.name });
        m_list->Append(from_u8(printer.name) + (address.empty() ? wxString() : "  —  " + from_u8(address)) +
                       (host && (host->value == htPrusaConnect || host->value == htPrusaConnectNew) ? "  (Prusa Connect)" : wxString()));
    }
    int sel = 0;
    for (size_t i = 0; i < m_entries.size(); ++ i)
        if ((m_entries[i].type == Entry::Type::USB ? std::string("\\usb") : m_entries[i].name) == selected)
            sel = int(i);
    // The first network printer if nothing was selected.
    if (selected.empty() && m_entries.size() > 1)
        sel = 1;
    m_list->SetSelection(sel);
    on_select();
}

std::string DevicesPanel::web_url(const std::string &print_host, int host_type)
{
    std::string url = print_host;
    if (url.find("http://") != 0 && url.find("https://") != 0)
        url = "http://" + url;
    // Moonraker: the API port (7125) is not the web interface (Mainsail / Fluidd on port 80).
    if (host_type == htMoonraker) {
        const size_t scheme_end = url.find("://") + 3;
        const size_t colon = url.find(':', scheme_end);
        if (colon != std::string::npos && url.compare(colon, 5, ":7125") == 0)
            url.erase(colon, 5);
    }
    return url;
}

void DevicesPanel::on_select()
{
    const int sel = m_list->GetSelection();
    const bool network = sel != wxNOT_FOUND && size_t(sel) < m_entries.size() && m_entries[sel].type == Entry::Type::Network;
    m_btn_edit->Enable(network);
    m_btn_send->Enable(network);
    m_btn_browser->Enable(network);
    m_start->Enable(network);
    if (! network) {
        m_title->SetLabel(_L("USB printer"));
        m_hint->SetLabel(_L("The USB printer is controlled from the \"Print via USB\" window: connection, temperatures, console and "
                            "printing. Add a network printer to see its web interface here (camera, temperatures, calibrations)."));
        m_web->Hide();
        m_url.clear();
        Layout();
        return;
    }
    const PhysicalPrinter *printer = wxGetApp().preset_bundle->physical_printers.find_printer(m_entries[sel].name);
    if (printer == nullptr)
        return;
    const DynamicPrintConfig &cfg = printer->config;
    const auto *host = cfg.option<ConfigOptionEnum<PrintHostType>>("host_type");
    const int host_type = host ? int(host->value) : int(htOctoPrint);
    m_title->SetLabel(from_u8(printer->name));
    if (host_type == htPrusaConnect || host_type == htPrusaConnectNew) {
        m_hint->SetLabel(_L("Prusa Connect printers are shown in the Prusa Connect page; the G-code can be sent from here."));
        m_web->Hide();
        m_url.clear();
        Layout();
        return;
    }
    if (host_type == htBambuLan) {
        // No web interface: the status comes from the MQTT reports of the printer.
        m_web->Hide();
        m_url.clear();
        m_btn_browser->Enable(false);
        m_hint->SetLabel(_L("Bambu Lab printer in LAN mode: asking for its status..."));
        Layout();
        const int request = ++m_status_request;
        DynamicPrintConfig config = cfg;
        std::thread([this, request, config]() mutable {
            BambuLan host(&config);
            BambuStatus status;
            std::string error;
            const bool ok = host.client().query_status(status, error);
            wxGetApp().CallAfter([this, request, ok, status, error]() {
                if (request != m_status_request)
                    return;
                wxString text;
                if (!ok)
                    text = format_wxstr(_L("Could not read the status of the printer: %1%"),
                                        error == "access code" ? _L("the printer rejected the access code") : from_u8(error));
                else {
                    text = format_wxstr(_L("Serial number %1%. State: %2%."), from_u8(status.serial), from_u8(status.gcode_state));
                    if (status.nozzle_temp >= 0. && status.bed_temp >= 0.)
                        text += " " + format_wxstr(_L("Nozzle %1% °C, bed %2% °C."), int(status.nozzle_temp + 0.5), int(status.bed_temp + 0.5));
                    if (status.gcode_state == "RUNNING" || status.gcode_state == "PAUSE")
                        text += " " + format_wxstr(_L("Progress %1% %%, %2% min left."), status.percent, status.remaining_min);
                }
                m_hint->SetLabel(text + "\n" + _L("Select it again to refresh. Tisma does not show the camera of Bambu Lab printers yet."));
                Layout();
            });
        }).detach();
        return;
    }
    m_hint->SetLabel(_L("Web interface of the printer: camera, temperatures, console and the calibrations of its firmware "
                        "(for example input shaping in Klipper)."));
    const std::string url = web_url(cfg.opt_string("print_host"), host_type);
    // Authentication: API key (OctoPrint) or user and password (PrusaLink, digest).
    const auto *auth = dynamic_cast<const ConfigOptionEnum<AuthorizationType>*>(cfg.option("printhost_authorization_type"));
    if (auth == nullptr || auth->value == AuthorizationType::atKeyPassword)
        m_web->set_api_key(cfg.opt_string("printhost_apikey"));
    else
        m_web->set_credentials(cfg.opt_string("printhost_user"), cfg.opt_string("printhost_password"));
    if (url == m_url && m_web->IsShown())
        return;
    m_url = url;
    if (! m_web_created) {
        // The browser is created when the panel is shown for the first time, then it loads the default URL.
        m_web_created = true;
        m_web->set_default_url(from_u8(url));
        m_web->set_create_browser();
        m_web->Show();
    } else {
        m_web->set_default_url(from_u8(url));
        m_web->Show();
        m_web->load_url(from_u8(url));
    }
    Layout();
}

void DevicesPanel::add_network_printer()
{
    PhysicalPrinterDialog dlg(this, wxEmptyString);
    if (dlg.ShowModal() == wxID_OK) {
        wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
        update_list();
        // Select the new printer (the last one in the list).
        if (m_entries.size() > 1) {
            m_list->SetSelection(int(m_entries.size()) - 1);
            on_select();
        }
    }
}

void DevicesPanel::edit_printer()
{
    const int sel = m_list->GetSelection();
    if (sel == wxNOT_FOUND || size_t(sel) >= m_entries.size())
        return;
    if (m_entries[sel].type == Entry::Type::USB) {
        USBPrintDialog::run(wxGetApp().mainframe);
        return;
    }
    PhysicalPrinterDialog dlg(this, from_u8(m_entries[sel].name));
    if (dlg.ShowModal() == wxID_OK) {
        wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
        m_url.clear();
        update_list();
    }
}

void DevicesPanel::send_sliced()
{
    const int sel = m_list->GetSelection();
    if (sel == wxNOT_FOUND || size_t(sel) >= m_entries.size() || m_entries[sel].type != Entry::Type::Network)
        return;
    const PhysicalPrinter *printer = wxGetApp().preset_bundle->physical_printers.find_printer(m_entries[sel].name);
    if (printer == nullptr)
        return;
    Plater *plater = wxGetApp().plater();
    const std::string sliced = plater->sliced_gcode_path();
    if (sliced.empty()) {
        ErrorDialog(this, _L("Slice the plate first: there is no sliced G-code to send."), false).ShowModal();
        return;
    }
    // The upload queue deletes its source file: a copy of the temporary G-code of the slicing.
    namespace fs = boost::filesystem;
    const fs::path copy = fs::temp_directory_path() / fs::unique_path(".TismaSlicer.upload.%%%%-%%%%-%%%%.gcode");
    boost::system::error_code ec;
    fs::copy_file(sliced, copy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        ErrorDialog(this, format_wxstr(_L("Cannot copy the G-code: %1%"), from_u8(ec.message())), false).ShowModal();
        return;
    }
    DynamicPrintConfig config = printer->config;
    PrintHostJob job(&config);
    if (job.empty()) {
        ErrorDialog(this, _L("The printer has no network address: edit it."), false).ShowModal();
        return;
    }
    std::string filename = plater->get_upload_filename();
    if (filename.empty())
        filename = "tisma.gcode";
    job.upload_data.source_path = copy;
    job.upload_data.upload_path = fs::path(filename).filename();
    if (m_start->GetValue() && (job.printhost->get_post_upload_actions() & PrintHostPostUploadAction::StartPrint))
        job.upload_data.post_action = PrintHostPostUploadAction::StartPrint;
    wxGetApp().printhost_job_queue().enqueue(std::move(job));
    if (NotificationManager *nm = plater->get_notification_manager())
        nm->push_notification(NotificationType::CustomNotification, NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
                              into_u8(format_wxstr(_L("Sending %1% to %2%."), from_u8(filename), from_u8(printer->name))));
}

void DevicesPanel::open_in_browser()
{
    if (! m_url.empty())
        wxLaunchDefaultBrowser(from_u8(m_url));
}

void DevicesPanel::sys_color_changed()
{
    const bool dark = wxGetApp().dark_mode();
    SetBackgroundColour(TismaTheme::panel_bg(dark));
    m_hint->SetForegroundColour(TismaTheme::text_muted(dark));
    wxGetApp().UpdateDarkUI(m_list);
    m_web->sys_color_changed();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
