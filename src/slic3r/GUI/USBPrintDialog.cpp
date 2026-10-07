#include "USBPrintDialog.hpp"

#include <fstream>
#include <optional>
#include <sstream>

#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>

#include <LibBGCode/convert/convert.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/filedlg.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "../Utils/Serial.hpp"
#include "../Utils/USBPrinter.hpp"

namespace Slic3r {
namespace GUI {

static USBPrintDialog *s_instance = nullptr;

// Saved connection (application config).
static const char *CONFIG_PORT        = "usb_print_port";
static const char *CONFIG_BAUD        = "usb_print_baud";
static const char *CONFIG_AUTOCONNECT = "usb_print_autoconnect";

// Reads a G-code file (a binary G-code is converted to text) and returns the commands to send.
static bool load_gcode(const std::string &path, std::vector<std::string> &commands, wxString &error)
{
    std::string gcode;
    {
        std::ifstream in(boost::filesystem::path(path).string(), std::ios::binary);
        if (! in) {
            error = format_wxstr(_L("Cannot read %1%"), from_u8(path));
            return false;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        gcode = buffer.str();
    }
    if (gcode.rfind("GCDE", 0) == 0) {
        // Binary G-code: convert it to text in a temporary file.
        const boost::filesystem::path ascii = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("tisma_usb_%%%%%%%%.gcode");
        bgcode::core::EResult res = bgcode::core::EResult::ReadError;
        {
            FILE *src = boost::nowide::fopen(path.c_str(), "rb");
            FILE *dst = boost::nowide::fopen(ascii.string().c_str(), "wb");
            if (src != nullptr && dst != nullptr)
                res = bgcode::convert::from_binary_to_ascii(*src, *dst, true);
            if (src) fclose(src);
            if (dst) fclose(dst);
        }
        if (res == bgcode::core::EResult::Success) {
            std::ifstream in(ascii.string(), std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            gcode = buffer.str();
        }
        boost::system::error_code ec;
        boost::filesystem::remove(ascii, ec);
        if (res != bgcode::core::EResult::Success) {
            error = format_wxstr(_L("The binary G-code cannot be converted to text: %1%"), std::string(bgcode::core::translate_result(res)));
            return false;
        }
    }
    commands = USB::GCodeSender::prepare(gcode);
    if (commands.empty()) {
        error = _L("The G-code is empty.");
        return false;
    }
    return true;
}

struct USBPrintDialog::priv
{
    USBPrintDialog          *q;
    USB::USBPrinterConnection connection;

    wxComboBox  *port      { nullptr };
    wxComboBox  *baud      { nullptr };
    wxButton    *btn_connect { nullptr };
    wxTextCtrl  *file      { nullptr };
    wxButton    *btn_start { nullptr };
    wxButton    *btn_pause { nullptr };
    wxButton    *btn_cancel { nullptr };
    wxStaticText *status   { nullptr };
    wxStaticText *temps    { nullptr };
    wxGauge     *progress  { nullptr };
    wxTextCtrl  *console   { nullptr };
    wxTextCtrl  *command   { nullptr };
    wxCheckBox  *autoconnect { nullptr };
    wxTimer      timer;
    // A print waiting for the printer to be ready (connecting).
    std::optional<std::vector<std::string>> pending_print;
    wxString     pending_name;

    explicit priv(USBPrintDialog *q) : q(q) {}

    void scan_ports()
    {
        const wxString current = port->GetValue();
        port->Clear();
        int printer_idx = -1;
        for (const Utils::SerialPortInfo &info : Utils::scan_serial_ports_extended()) {
            port->Append(from_u8(info.port));
            if (info.is_printer && printer_idx < 0)
                printer_idx = int(port->GetCount()) - 1;
        }
        const std::string saved = wxGetApp().app_config->get(CONFIG_PORT);
        if (! current.IsEmpty())
            port->SetValue(current);
        else if (! saved.empty())
            port->SetValue(from_u8(saved));
        else if (printer_idx >= 0)
            port->SetSelection(printer_idx);
        else if (port->GetCount() > 0)
            port->SetSelection(0);
    }

    void toggle_connection()
    {
        if (connection.is_open()) {
            bool printing = false;
            connection.with_sender([&](USB::GCodeSender &s, double) {
                printing = s.state() == USB::State::Printing || s.state() == USB::State::Paused;
            });
            if (printing &&
                MessageDialog(q, _L("A print is in progress. Disconnecting stops it. Disconnect anyway?"),
                              _L("Print via USB"), wxYES_NO | wxICON_WARNING).ShowModal() != wxID_YES)
                return;
            connection.close();
            pending_print.reset();
            append_console(_L("Disconnected"));
            return;
        }
        unsigned long baud_rate = 115200;
        baud->GetValue().ToULong(&baud_rate);
        try {
            connection.open(into_u8(port->GetValue()), unsigned(baud_rate));
            append_console(format_wxstr(_L("Connecting to %1% at %2% baud..."), port->GetValue(), baud_rate));
            // Remember the printer: the next time it connects by itself.
            wxGetApp().app_config->set(CONFIG_PORT, into_u8(port->GetValue()));
            wxGetApp().app_config->set(CONFIG_BAUD, std::to_string(baud_rate));
        } catch (const std::exception &ex) {
            ErrorDialog(q, from_u8(ex.what()), false).ShowModal();
        }
    }

    void browse()
    {
        wxFileDialog dlg(q, _L("G-code to print"), from_u8(wxGetApp().app_config->get_last_output_dir("")), "",
                         "G-code (*.gcode;*.gco;*.g;*.ngc)|*.gcode;*.GCODE;*.gco;*.GCO;*.g;*.G;*.ngc;*.NGC",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK)
            file->SetValue(dlg.GetPath());
    }

    void start()
    {
        std::vector<std::string> commands;
        wxString error;
        if (! load_gcode(into_u8(file->GetValue()), commands, error)) {
            ErrorDialog(q, error, false).ShowModal();
            return;
        }
        connection.with_sender([&](USB::GCodeSender &s, double now) { s.start(std::move(commands), now); });
    }

    bool printing()
    {
        bool printing = false;
        if (connection.is_open())
            connection.with_sender([&](USB::GCodeSender &s, double) {
                printing = s.state() == USB::State::Printing || s.state() == USB::State::Paused;
            });
        return printing;
    }

    // Connects to the saved printer (when the window opens or a print is sent).
    void auto_connect()
    {
        if (connection.is_open() || port->GetValue().IsEmpty())
            return;
        try {
            unsigned long baud_rate = 115200;
            baud->GetValue().ToULong(&baud_rate);
            connection.open(into_u8(port->GetValue()), unsigned(baud_rate));
            append_console(format_wxstr(_L("Connecting to %1% at %2% baud..."), port->GetValue(), baud_rate));
        } catch (const std::exception &ex) {
            append_console(format_wxstr(_L("Cannot connect to %1%: %2%"), port->GetValue(), from_u8(ex.what())));
        }
    }

    void pause_resume()
    {
        connection.with_sender([](USB::GCodeSender &s, double now) {
            if (s.state() == USB::State::Paused)
                s.resume(now);
            else
                s.pause();
        });
    }

    void cancel()
    {
        if (MessageDialog(q, _L("Cancel the print?"), _L("Print via USB"), wxYES_NO | wxICON_QUESTION).ShowModal() == wxID_YES)
            connection.with_sender([](USB::GCodeSender &s, double now) { s.cancel(now); });
    }

    void send_command()
    {
        const std::string cmd = into_u8(command->GetValue());
        if (cmd.empty())
            return;
        append_console("> " + from_u8(cmd));
        connection.with_sender([&](USB::GCodeSender &s, double now) { s.send_manual(cmd, now); });
        command->Clear();
    }

    void append_console(const wxString &text)
    {
        console->AppendText(text + "\n");
        if (console->GetNumberOfLines() > 3000)
            console->Remove(0, console->XYToPosition(0, 1000));
    }

    void update()
    {
        USB::State state = USB::State::Disconnected;
        size_t sent = 0, total = 0;
        USB::Temperatures t;
        std::vector<std::string> log;
        if (connection.is_open())
            connection.with_sender([&](USB::GCodeSender &s, double) {
                state = s.state();
                sent  = s.sent();
                total = s.total();
                t     = s.temperatures();
                log   = s.take_log();
            });
        for (const std::string &line : log)
            append_console(from_u8(line));
        if (pending_print && state == USB::State::Idle) {
            append_console(format_wxstr(_L("Printing %1%"), pending_name));
            connection.with_sender([&](USB::GCodeSender &s, double now) { s.start(std::move(*pending_print), now); });
            pending_print.reset();
            state = USB::State::Printing;
        } else if (pending_print && (state == USB::State::Error || ! connection.is_open())) {
            append_console(_L("The printer is not connected: the print was not started."));
            pending_print.reset();
        }

        wxString state_text;
        switch (state) {
        case USB::State::Disconnected: state_text = _L("Disconnected"); break;
        case USB::State::Connecting:   state_text = _L("Connecting"); break;
        case USB::State::Idle:         state_text = _L("Ready"); break;
        case USB::State::Printing:     state_text = _L("Printing"); break;
        case USB::State::Paused:       state_text = _L("Paused"); break;
        case USB::State::Error:        state_text = _L("Error"); break;
        }
        const bool printing = state == USB::State::Printing || state == USB::State::Paused;
        if (pending_print)
            state_text += "  " + _L("(the print starts when the printer is ready)");
        if (printing && total > 0)
            state_text += format_wxstr("  %1%/%2% (%3%%%)", sent, total, int(100. * double(sent) / double(total)));
        status->SetLabel(state_text);
        progress->SetValue(total > 0 ? int(1000. * double(sent) / double(total)) : 0);

        auto fmt = [](const std::optional<double> &v) { return v ? wxString::Format("%.1f", *v) : wxString("-"); };
        temps->SetLabel(format_wxstr(_L("Hotend %1% / %2% °C    Bed %3% / %4% °C"),
                                     fmt(t.hotend), fmt(t.hotend_target), fmt(t.bed), fmt(t.bed_target)));

        const bool open = connection.is_open();
        btn_connect->SetLabel(open ? _L("Disconnect") : _L("Connect"));
        port->Enable(! open);
        baud->Enable(! open);
        btn_start->Enable(open && state == USB::State::Idle);
        btn_pause->Enable(printing);
        btn_pause->SetLabel(state == USB::State::Paused ? _L("Resume") : _L("Pause"));
        btn_cancel->Enable(printing);
        command->Enable(open && state != USB::State::Connecting);
    }
};

USBPrintDialog::USBPrintDialog(wxWindow *parent) :
    DPIDialog(parent, wxID_ANY, _L("Print via USB"), wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    p(new priv(this))
{
    SetFont(wxGetApp().normal_font());
    const int em = em_unit();
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _L("Port") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    p->port = new wxComboBox(this, wxID_ANY, "", wxDefaultPosition, wxSize(18 * em, -1));
    row->Add(p->port, 1, wxALIGN_CENTER_VERTICAL);
    auto *btn_rescan = new wxButton(this, wxID_ANY, _L("Rescan"));
    row->Add(btn_rescan, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 2);
    row->Add(new wxStaticText(this, wxID_ANY, _L("Baud rate") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, em / 2);
    const wxString bauds[] = { "115200", "250000", "230400", "500000", "1000000", "57600" };
    const std::string saved_baud = wxGetApp().app_config->get(CONFIG_BAUD);
    p->baud = new wxComboBox(this, wxID_ANY, saved_baud.empty() ? wxString("115200") : from_u8(saved_baud),
                             wxDefaultPosition, wxSize(10 * em, -1), 6, bauds);
    row->Add(p->baud, 0, wxALIGN_CENTER_VERTICAL);
    p->btn_connect = new wxButton(this, wxID_ANY, _L("Connect"));
    row->Add(p->btn_connect, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em);
    sizer->Add(row, 0, wxEXPAND | wxALL, em);
    p->autoconnect = new wxCheckBox(this, wxID_ANY, _L("Connect automatically to this printer (the connection stays open when this window is closed)"));
    p->autoconnect->SetValue(wxGetApp().app_config->get(CONFIG_AUTOCONNECT) != "0");
    sizer->Add(p->autoconnect, 0, wxLEFT | wxRIGHT | wxBOTTOM, em);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _L("G-code") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    p->file = new wxTextCtrl(this, wxID_ANY, from_u8(wxGetApp().app_config->get("last_output_path")));
    row->Add(p->file, 1, wxALIGN_CENTER_VERTICAL);
    auto *btn_browse = new wxButton(this, wxID_ANY, _L("Browse") + dots);
    row->Add(btn_browse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 2);
    sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, em);

    row = new wxBoxSizer(wxHORIZONTAL);
    p->btn_start  = new wxButton(this, wxID_ANY, _L("Start print"));
    p->btn_pause  = new wxButton(this, wxID_ANY, _L("Pause"));
    p->btn_cancel = new wxButton(this, wxID_ANY, _L("Cancel print"));
    row->Add(p->btn_start, 0, wxRIGHT, em / 2);
    row->Add(p->btn_pause, 0, wxRIGHT, em / 2);
    row->Add(p->btn_cancel, 0);
    sizer->Add(row, 0, wxALL, em);

    p->status = new wxStaticText(this, wxID_ANY, _L("Disconnected"));
    sizer->Add(p->status, 0, wxLEFT | wxRIGHT, em);
    p->progress = new wxGauge(this, wxID_ANY, 1000);
    sizer->Add(p->progress, 0, wxEXPAND | wxALL, em);
    p->temps = new wxStaticText(this, wxID_ANY, "");
    sizer->Add(p->temps, 0, wxLEFT | wxRIGHT | wxBOTTOM, em);

    p->console = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(60 * em, 20 * em),
                                wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
    sizer->Add(p->console, 1, wxEXPAND | wxLEFT | wxRIGHT, em);
    row = new wxBoxSizer(wxHORIZONTAL);
    p->command = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    row->Add(p->command, 1, wxALIGN_CENTER_VERTICAL);
    auto *btn_send = new wxButton(this, wxID_ANY, _L("Send"));
    row->Add(btn_send, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 2);
    sizer->Add(row, 0, wxEXPAND | wxALL, em);

    SetSizerAndFit(sizer);
    SetMinSize(GetSize());

    btn_rescan->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->scan_ports(); });
    p->btn_connect->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->toggle_connection(); p->update(); });
    btn_browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->browse(); });
    p->btn_start->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->start(); p->update(); });
    p->btn_pause->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->pause_resume(); p->update(); });
    p->btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->cancel(); p->update(); });
    btn_send->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->send_command(); });
    p->command->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { p->send_command(); });
    p->timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { p->update(); });
    p->autoconnect->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
        wxGetApp().app_config->set(CONFIG_AUTOCONNECT, p->autoconnect->GetValue() ? "1" : "0");
    });
    // Closing the window only hides it: the connection and the print go on (the printer stays ready for the next
    // print). It is destroyed with the main window.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &evt) {
        if (! evt.CanVeto()) {
            p->timer.Stop();
            p->connection.close();
            s_instance = nullptr;
            Destroy();
            return;
        }
        Hide();
    });

    p->scan_ports();
    p->update();
    p->timer.Start(200);
}

USBPrintDialog::~USBPrintDialog()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void USBPrintDialog::run(wxWindow *parent)
{
    const bool created = s_instance == nullptr;
    if (created)
        s_instance = new USBPrintDialog(parent);
    s_instance->Show();
    s_instance->Raise();
    if (created && s_instance->p->autoconnect->GetValue() && has_saved_printer())
        s_instance->p->auto_connect();
}

bool USBPrintDialog::has_saved_printer()
{
    return ! wxGetApp().app_config->get(CONFIG_PORT).empty();
}

void USBPrintDialog::print_file(wxWindow *parent, const std::string &path, const std::string &name)
{
    std::vector<std::string> commands;
    wxString error;
    if (! load_gcode(path, commands, error)) {
        ErrorDialog(parent, error, false).ShowModal();
        return;
    }
    run(parent);
    priv &p = *s_instance->p;
    if (p.printing()) {
        ErrorDialog(s_instance, _L("A print is already in progress on the printer."), false).ShowModal();
        return;
    }
    p.file->SetValue(from_u8(path));
    p.pending_name  = from_u8(name);
    p.pending_print = std::move(commands);
    p.auto_connect();
    p.update();
}

void USBPrintDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
