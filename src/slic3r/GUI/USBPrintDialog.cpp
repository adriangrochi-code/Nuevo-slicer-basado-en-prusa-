#include "USBPrintDialog.hpp"

#include <fstream>
#include <sstream>

#include <boost/filesystem.hpp>

#include <wx/button.h>
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
    wxTimer      timer;

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
        if (! current.IsEmpty())
            port->SetValue(current);
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
            append_console(_L("Disconnected"));
            return;
        }
        unsigned long baud_rate = 115200;
        baud->GetValue().ToULong(&baud_rate);
        try {
            connection.open(into_u8(port->GetValue()), unsigned(baud_rate));
            append_console(format_wxstr(_L("Connecting to %1% at %2% baud..."), port->GetValue(), baud_rate));
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
        const std::string path = into_u8(file->GetValue());
        std::ifstream in(boost::filesystem::path(path).string(), std::ios::binary);
        if (! in) {
            ErrorDialog(q, format_wxstr(_L("Cannot read %1%"), file->GetValue()), false).ShowModal();
            return;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string gcode = buffer.str();
        if (gcode.rfind("GCDE", 0) == 0) {
            ErrorDialog(q, _L("Binary G-code cannot be printed over USB. Disable \"Supports binary G-code\" "
                              "in the printer settings and export the G-code again."), false).ShowModal();
            return;
        }
        std::vector<std::string> commands = USB::GCodeSender::prepare(gcode);
        connection.with_sender([&](USB::GCodeSender &s, double now) { s.start(std::move(commands), now); });
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
    p->baud = new wxComboBox(this, wxID_ANY, "115200", wxDefaultPosition, wxSize(10 * em, -1), 6, bauds);
    row->Add(p->baud, 0, wxALIGN_CENTER_VERTICAL);
    p->btn_connect = new wxButton(this, wxID_ANY, _L("Connect"));
    row->Add(p->btn_connect, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em);
    sizer->Add(row, 0, wxEXPAND | wxALL, em);

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
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &evt) {
        bool printing = false;
        if (p->connection.is_open())
            p->connection.with_sender([&](USB::GCodeSender &s, double) {
                printing = s.state() == USB::State::Printing || s.state() == USB::State::Paused;
            });
        if (printing && evt.CanVeto() &&
            MessageDialog(this, _L("A print is in progress. Closing this window stops it. Close anyway?"),
                          _L("Print via USB"), wxYES_NO | wxICON_WARNING).ShowModal() != wxID_YES) {
            evt.Veto();
            return;
        }
        p->timer.Stop();
        p->connection.close();
        s_instance = nullptr;
        Destroy();
    });

    p->scan_ports();
    p->update();
    p->timer.Start(200);
}

USBPrintDialog::~USBPrintDialog() = default;

void USBPrintDialog::run(wxWindow *parent)
{
    if (s_instance == nullptr)
        s_instance = new USBPrintDialog(parent);
    s_instance->Show();
    s_instance->Raise();
}

void USBPrintDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
