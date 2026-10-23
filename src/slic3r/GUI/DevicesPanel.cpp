///|/ Tisma Slicer: Devices workspace.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "DevicesPanel.hpp"
#include <wx/treectrl.h>
#include "Widgets/SwitchButton.hpp"
#include "Widgets/Button.hpp"
#include "TismaWidgets.hpp"

#include <algorithm>
#include <memory>

#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcbuffer.h>
#include <wx/image.h>
#include <wx/listbox.h>
#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
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
#include "USBMonitorPanel.hpp"
#include "WebViewPanel.hpp"
#include "format.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/BambuLan.hpp"

#include <thread>

namespace Slic3r {
namespace GUI {

// The last image of the camera of a Bambu Lab printer, scaled to the panel keeping its proportions, or a message while
// there is none.
class BambuCameraView : public wxPanel
{
public:
    explicit BambuCameraView(wxWindow *parent) : wxPanel(parent, wxID_ANY)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(-1, 16 * wxGetApp().em_unit()));
        Bind(wxEVT_PAINT, [this](wxPaintEvent &) { paint(); });
        Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) { Refresh(); evt.Skip(); });
    }

    // A JPEG of the printer; false when it cannot be read.
    bool set_jpeg(const std::string &jpeg)
    {
        wxLogNull no_log;
        wxMemoryInputStream in(jpeg.data(), jpeg.size());
        wxImage image;
        if (!image.LoadFile(in, wxBITMAP_TYPE_JPEG) || !image.IsOk())
            return false;
        m_image = image;
        m_scaled = wxBitmap();
        Refresh();
        return true;
    }

    void set_message(const wxString &message)
    {
        m_image = wxImage();
        m_scaled = wxBitmap();
        m_message = message;
        Refresh();
    }

private:
    wxImage  m_image;
    wxBitmap m_scaled;
    wxString m_message;

    void paint()
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        dc.SetBackground(wxBrush(wxColour(32, 33, 36)));
        dc.Clear();
        if (m_image.IsOk() && size.x > 0 && size.y > 0) {
            const double scale = std::min(double(size.x) / m_image.GetWidth(), double(size.y) / m_image.GetHeight());
            const int w = std::max(1, int(m_image.GetWidth() * scale));
            const int h = std::max(1, int(m_image.GetHeight() * scale));
            if (!m_scaled.IsOk() || m_scaled.GetWidth() != w || m_scaled.GetHeight() != h)
                m_scaled = wxBitmap(m_image.Scale(w, h, wxIMAGE_QUALITY_BILINEAR));
            dc.DrawBitmap(m_scaled, (size.x - w) / 2, (size.y - h) / 2);
            return;
        }
        const int em = wxGetApp().em_unit();
        dc.SetTextForeground(wxColour(200, 200, 205));
        dc.SetFont(GetFont());
        // Wrapped by words to the width of the panel.
        wxArrayString lines;
        wxString line;
        for (const wxString &word : wxSplit(m_message, ' ')) {
            const wxString longer = line.empty() ? word : line + " " + word;
            if (!line.empty() && dc.GetTextExtent(longer).x > size.x - 2 * em) {
                lines.Add(line);
                line = word;
            } else
                line = longer;
        }
        if (!line.empty())
            lines.Add(line);
        const int line_h = dc.GetCharHeight() * 5 / 4;
        int y = (size.y - line_h * int(lines.size())) / 2;
        for (const wxString &l : lines) {
            dc.DrawText(l, (size.x - dc.GetTextExtent(l).x) / 2, y);
            y += line_h;
        }
    }
};

DevicesPanel::DevicesPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY)
{
    const int  em   = wxGetApp().em_unit();
    const bool dark = wxGetApp().dark_mode();
    SetBackgroundColour(TismaTheme::panel_bg(dark));

    auto *main = new wxBoxSizer(wxHORIZONTAL);

    // Left: the printers.
    auto *left = new wxBoxSizer(wxVERTICAL);
    m_caption = new TismaTitle(this, _L("Devices"));
    left->Add(m_caption, 0, wxBOTTOM, em / 2);
    m_list = new TismaPageList(this, wxSize(24 * em, -1));
    {
        wxVector<wxBitmapBundle> icons;
        icons.push_back(*get_bmp_bundle("plug"));
        icons.push_back(*get_bmp_bundle("tisma_printer"));
        m_list->SetImages(icons);
    }
    left->Add(m_list, 1, wxEXPAND | wxBOTTOM, em / 2);
    m_btn_add = new ::Button(this, "+ " + _L("Add network printer"));
    m_btn_add->SetToolTip(_L("Bambu Lab (LAN), Flashforge, OctoPrint, Klipper (Moonraker: Mainsail, Fluidd), PrusaLink, Duet, "
                             "Repetier, MKS, FlashAir, AstroBox. The printers of the local network can be searched in the dialog."));
    left->Add(m_btn_add, 0, wxEXPAND | wxBOTTOM, em / 2);
    m_btn_edit = new ::Button(this, _L("Edit printer") + dots);
    left->Add(m_btn_edit, 0, wxEXPAND | wxBOTTOM, em / 2);
    m_btn_code = new ::Button(this, _L("Change access code") + dots);
    m_btn_code->SetToolTip(_L("When the printer shows a new access code or API key: type it here, Tisma tests the connection "
                              "and saves it."));
    left->Add(m_btn_code, 0, wxEXPAND);
    main->Add(left, 0, wxEXPAND | wxALL, em);

    // Right: the selected printer.
    auto *right = new wxBoxSizer(wxVERTICAL);
    auto *bar = new wxBoxSizer(wxHORIZONTAL);
    m_title = new TismaTitle(this, "");
    bar->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
    m_start_label = new wxStaticText(this, wxID_ANY, _L("Start printing"));
    bar->Add(m_start_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    m_start = new ::SwitchButton(this);
    m_start->SetValue(wxGetApp().app_config->get("tisma_devices_start_print") != "0");
    bar->Add(m_start, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);
    m_btn_send = new ::Button(this, _L("Send sliced G-code"));
    bar->Add(m_btn_send, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    m_btn_browser = new ::Button(this, _L("Open in browser"));
    bar->Add(m_btn_browser, 0, wxALIGN_CENTER_VERTICAL);
    right->Add(bar, 0, wxEXPAND | wxBOTTOM, em / 2);
    m_hint = new wxStaticText(this, wxID_ANY, "");
    m_hint->SetForegroundColour(TismaTheme::text_muted(dark));
    right->Add(m_hint, 0, wxEXPAND | wxBOTTOM, em / 2);
    m_web = new PrinterWebViewPanel(this, L"");
    m_web->Hide();
    right->Add(m_web, 1, wxEXPAND);
    // The USB printer: detected by itself, controlled and printed from here.
    m_usb = new USBMonitorPanel(this);
    m_usb->Hide();
    right->Add(m_usb, 1, wxEXPAND);
    // Bambu Lab printers (no web interface): their camera.
    m_camera = new BambuCameraView(this);
    m_camera->Hide();
    right->Add(m_camera, 1, wxEXPAND);
    main->Add(right, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, em);

    SetSizer(main);

    m_list->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent &) { on_select(); });
    m_list->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent &) { edit_printer(); });
    m_btn_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { add_network_printer(); });
    m_btn_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { edit_printer(); });
    m_btn_code->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { change_access_code(); });
    m_btn_send->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { send_sliced(); });
    m_btn_browser->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { open_in_browser(); });
    m_start->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
        wxGetApp().app_config->set("tisma_devices_start_print", m_start->GetValue() ? "1" : "0");
    });
    // The list follows the printers added or edited elsewhere (the printer combo of the sidebar).
    Bind(wxEVT_SHOW, [this](wxShowEvent &evt) {
        if (evt.IsShown())
            update_list();
        else
            stop_camera();
        evt.Skip();
    });

    style_controls();
    update_list();
}

void DevicesPanel::style_controls()
{
    const bool dark = wxGetApp().dark_mode();
    m_caption->sys_color_changed();
    m_title->sys_color_changed();
    m_start_label->SetForegroundColour(TismaTheme::text(dark));
    for (::Button *btn : { m_btn_add, m_btn_edit, m_btn_code, m_btn_browser })
        style_tisma_button(btn, false);
    style_tisma_button(m_btn_send, true);
}

DevicesPanel::~DevicesPanel()
{
    // Only the stream: the windows are going away.
    if (m_camera_stop)
        *m_camera_stop = true;
}

void DevicesPanel::update_list()
{
    // Keep the selection by name.
    std::string selected;
    if (int sel = m_list->GetSelectionIndex(); sel >= 0 && size_t(sel) < m_entries.size())
        selected = m_entries[sel].type == Entry::Type::USB ? std::string("\\usb") : m_entries[sel].name;

    m_entries.clear();
    m_list->Clear();
    const std::string usb_port = wxGetApp().app_config->get("usb_print_port");
    m_entries.push_back({ Entry::Type::USB, {} });
    m_list->AppendItem(m_list->GetRootItem(), usb_port.empty() ? _L("USB printer (detected when connected)") :
                                                                 format_wxstr(_L("USB printer (%1%)"), from_u8(usb_port)), 0);
    for (const PhysicalPrinter &printer : wxGetApp().preset_bundle->physical_printers) {
        const auto *host = printer.config.option<ConfigOptionEnum<PrintHostType>>("host_type");
        const std::string address = printer.config.opt_string("print_host");
        m_entries.push_back({ Entry::Type::Network, printer.name });
        m_list->AppendItem(m_list->GetRootItem(), from_u8(printer.name) + (address.empty() ? wxString() : "  —  " + from_u8(address)) +
                           (host && (host->value == htPrusaConnect || host->value == htPrusaConnectNew) ? "  (Prusa Connect)" : wxString()), 1);
    }
    int sel = 0;
    for (size_t i = 0; i < m_entries.size(); ++ i)
        if ((m_entries[i].type == Entry::Type::USB ? std::string("\\usb") : m_entries[i].name) == selected)
            sel = int(i);
    // Nothing selected: the USB printer if one was connected, else the first network printer.
    if (selected.empty() && m_entries.size() > 1 && usb_port.empty())
        sel = 1;
    m_list->SetSelectionIndex(sel);
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
    const int sel = m_list->GetSelectionIndex();
    const bool network = sel >= 0 && size_t(sel) < m_entries.size() && m_entries[sel].type == Entry::Type::Network;
    m_btn_edit->Enable(network);
    m_btn_code->Enable(network);
    m_btn_send->Enable(network);
    m_btn_browser->Enable(network);
    m_start->Enable(network);
    m_start_label->Enable(network);
    m_usb->Show(! network);
    stop_camera();
    if (! network) {
        m_title->SetLabel(_L("USB printer"));
        m_hint->SetLabel(_L("Plug the printer in with its USB cable: it is detected by itself. After slicing, \"Print via USB\" "
                            "sends the plate here."));
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
                m_hint->SetLabel(text + "\n" + _L("Select it again to refresh."));
                Layout();
            });
        }).detach();
        start_camera(cfg);
        return;
    }
    if (host_type == htFlashforge) {
        // No web interface: the connection is tested in the background and its result shown.
        m_web->Hide();
        m_url.clear();
        m_btn_browser->Enable(false);
        m_hint->SetLabel(_L("Flashforge printer: testing the connection..."));
        Layout();
        const int request = ++m_status_request;
        DynamicPrintConfig config = cfg;
        std::thread([this, request, config]() mutable {
            std::unique_ptr<PrintHost> host(PrintHost::get_print_host(&config));
            wxString msg;
            const bool ok = host && host->test(msg);
            const wxString text = !host ? wxString() : ok ? host->get_test_ok_msg() : host->get_test_failed_msg(msg);
            wxGetApp().CallAfter([this, request, text]() {
                if (request != m_status_request)
                    return;
                m_hint->SetLabel(text + "\n" + _L("Select it again to test again. If the access code changed, use \"Change access code\"."));
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

void DevicesPanel::select_usb()
{
    // The USB printer is always the first entry.
    if (! m_entries.empty() && m_list->GetSelectionIndex() != 0) {
        m_list->SetSelectionIndex(0);
        on_select();
    }
}

void DevicesPanel::add_network_printer()
{
    PhysicalPrinterDialog dlg(this, wxEmptyString);
    if (dlg.ShowModal() == wxID_OK) {
        wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
        update_list();
        // Select the new printer (the last one in the list).
        if (m_entries.size() > 1) {
            m_list->SetSelectionIndex(int(m_entries.size()) - 1);
            on_select();
        }
    }
}

void DevicesPanel::edit_printer()
{
    const int sel = m_list->GetSelectionIndex();
    if (sel < 0 || size_t(sel) >= m_entries.size())
        return;
    if (m_entries[sel].type == Entry::Type::USB)
        return;
    PhysicalPrinterDialog dlg(this, from_u8(m_entries[sel].name));
    if (dlg.ShowModal() == wxID_OK) {
        wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
        m_url.clear();
        update_list();
    }
}

void DevicesPanel::change_access_code()
{
    const int sel = m_list->GetSelectionIndex();
    if (sel < 0 || size_t(sel) >= m_entries.size() || m_entries[sel].type != Entry::Type::Network)
        return;
    PhysicalPrinterCollection &printers = wxGetApp().preset_bundle->physical_printers;
    const PhysicalPrinter *found = printers.find_printer(m_entries[sel].name);
    if (found == nullptr)
        return;
    PhysicalPrinter printer = *found;
    const auto *auth = dynamic_cast<const ConfigOptionEnum<AuthorizationType>*>(printer.config.option("printhost_authorization_type"));
    const auto *host_type = printer.config.option<ConfigOptionEnum<PrintHostType>>("host_type");
    if (auth != nullptr && auth->value == AuthorizationType::atUserPassword &&
        (host_type == nullptr || host_type->value == htPrusaLink)) {
        // User and password (PrusaLink, digest): the full dialog.
        edit_printer();
        return;
    }
    wxTextEntryDialog dlg(this, _L("New access code or API key of the printer:"), from_u8(printer.name),
                          from_u8(printer.config.opt_string("printhost_apikey")));
    if (dlg.ShowModal() != wxID_OK)
        return;
    const std::string code = boost::trim_copy(into_u8(dlg.GetValue()));
    printer.config.opt_string("printhost_apikey") = code;

    // Test before saving: a wrong code is saved only if asked.
    std::unique_ptr<PrintHost> host(PrintHost::get_print_host(&printer.config));
    if (host && host->can_test()) {
        wxString msg;
        bool ok;
        {
            wxBusyCursor wait;
            ok = host->test(msg);
        }
        if (!ok) {
            MessageDialog ask(this, host->get_test_failed_msg(msg) + "\n\n" + _L("Save the new code anyway?"),
                              _L("Change access code"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
            if (ask.ShowModal() != wxID_YES)
                return;
        }
    }
    printers.save_printer(printer);
    wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
    m_url.clear();
    update_list();
    on_select();
}

void DevicesPanel::send_sliced()
{
    const int sel = m_list->GetSelectionIndex();
    if (sel < 0 || size_t(sel) >= m_entries.size() || m_entries[sel].type != Entry::Type::Network)
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

void DevicesPanel::start_camera(const DynamicPrintConfig &cfg)
{
    stop_camera();
    auto stop = std::make_shared<std::atomic<bool>>(false);
    m_camera_stop = stop;
    m_camera->set_message(_L("Connecting to the camera of the printer..."));
    m_camera->Show();
    Layout();
    DynamicPrintConfig config = cfg;
    std::thread([this, stop, config]() mutable {
        BambuLan host(&config);
        std::string error;
        // The images go to the GUI thread as they come (about one a second); the GUI thread decodes them.
        const bool ok = host.client().stream_camera([this, stop](const std::string &jpeg) {
            wxGetApp().CallAfter([this, stop, jpeg]() {
                if (!*stop)
                    m_camera->set_jpeg(jpeg);
            });
            return !*stop;
        }, *stop, error);
        wxGetApp().CallAfter([this, stop, ok, error]() {
            // ok: stopped from here.
            if (*stop || ok)
                return;
            wxString message;
            if (error == "no camera")
                message = _L("This printer sends its camera by RTSPS (X1, P2S and H2 series), which Tisma does not show yet. "
                             "The camera of the A1 and P1 series is shown here.");
            else if (error == "refused")
                message = _L("The printer closed the camera connection before sending an image: check the access code, and "
                             "that the camera is on and not in use by another program.");
            else if (error == "closed")
                message = _L("The printer ended the camera stream. Select the printer again to reconnect.");
            else
                message = format_wxstr(_L("Could not show the camera: %1%"), from_u8(error));
            m_camera->set_message(message);
        });
    }).detach();
}

void DevicesPanel::stop_camera()
{
    if (m_camera_stop) {
        *m_camera_stop = true;
        m_camera_stop.reset();
    }
    if (m_camera != nullptr && m_camera->IsShown()) {
        m_camera->Hide();
        Layout();
    }
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
    style_controls();
    m_list->Refresh();
    m_web->sys_color_changed();
    m_usb->sys_color_changed();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
