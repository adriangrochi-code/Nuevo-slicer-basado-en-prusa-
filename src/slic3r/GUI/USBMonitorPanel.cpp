///|/ Tisma Slicer: the USB printer in the Device page.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "USBMonitorPanel.hpp"

#include <algorithm>
#include <deque>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>

#include <LibBGCode/convert/convert.hpp>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "LocalesUtils.hpp"

#include "DevicesPanel.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "TismaTheme.hpp"
#include "format.hpp"
#include "../Utils/Serial.hpp"
#include "../Utils/USBPrinter.hpp"

namespace Slic3r {
namespace GUI {

static USBMonitorPanel *s_instance = nullptr;

// Saved connection (application config). "usb_print_port" empty or "auto": detected.
static const char *CONFIG_PORT        = "usb_print_port";
static const char *CONFIG_BAUD        = "usb_print_baud";
static const char *CONFIG_AUTOCONNECT = "usb_print_autoconnect";

// Baud rates tried by the detection (Marlin uses 115200 or 250000).
static const unsigned DETECT_BAUDS[] = { 115200, 250000 };
// How long a port is given to answer M110 (the board resets when the port opens).
static constexpr double DETECT_TIMEOUT = 7.;
// Temperature history shown in the graph.
static constexpr double GRAPH_SECONDS = 600.;
// Jog feed rates (mm/min).
static constexpr int FEED_XY = 3000;
static constexpr int FEED_Z  = 300;
static constexpr int FEED_E  = 300;

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

// Serial ports worth probing, the likely printers first.
static std::vector<std::string> printer_ports()
{
    std::vector<Utils::SerialPortInfo> ports = Utils::scan_serial_ports_extended();
    ports.erase(std::remove_if(ports.begin(), ports.end(), [](const Utils::SerialPortInfo &info) {
        return info.port.find("Bluetooth") != std::string::npos || info.port.find("debug-console") != std::string::npos ||
               info.port.find("wlan") != std::string::npos;
    }), ports.end());
    auto rank = [](const Utils::SerialPortInfo &info) { return info.is_printer ? 0 : info.id_vendor != unsigned(-1) ? 1 : 2; };
    std::stable_sort(ports.begin(), ports.end(), [&rank](const auto &a, const auto &b) { return rank(a) < rank(b); });
    std::vector<std::string> out;
    for (const Utils::SerialPortInfo &info : ports)
        out.push_back(info.port);
    return out;
}

struct TempSample
{
    double t;
    std::optional<double> hotend, hotend_target, bed, bed_target;
};

// Temperatures of the last minutes: actual (solid) and target (dashed), hotend and bed.
class TempGraph : public wxPanel
{
public:
    TempGraph(wxWindow *parent, const std::deque<TempSample> &samples) :
        wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 13 * wxGetApp().em_unit())), m_samples(samples)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent &) { this->render(); });
    }

private:
    void render()
    {
        wxAutoBufferedPaintDC dc(this);
        const bool dark = wxGetApp().dark_mode();
        const wxSize size = GetClientSize();
        dc.SetBackground(wxBrush(TismaTheme::card_bg(dark)));
        dc.Clear();
        const int em = wxGetApp().em_unit();
        const int left = 4 * em, right = size.x - em, top = em, bottom = size.y - 2 * em;
        if (right <= left || bottom <= top)
            return;
        double t_max = 60.;
        for (const TempSample &s : m_samples)
            for (const std::optional<double> &v : { s.hotend, s.hotend_target, s.bed, s.bed_target })
                if (v)
                    t_max = std::max(t_max, *v);
        t_max = 50. * std::ceil((t_max + 10.) / 50.);
        dc.SetFont(wxGetApp().small_font());
        dc.SetTextForeground(TismaTheme::text_muted(dark));
        dc.SetPen(wxPen(TismaTheme::separator(dark)));
        for (double t = 0.; t <= t_max; t += 50.) {
            const int y = bottom - int((bottom - top) * t / t_max);
            dc.DrawLine(left, y, right, y);
            const wxString label = wxString::Format("%d", int(t));
            dc.DrawText(label, left - dc.GetTextExtent(label).x - em / 2, y - dc.GetCharHeight() / 2);
        }
        const double now = USB::now_seconds();
        auto x_of = [&](double t) { return right - int((right - left) * (now - t) / GRAPH_SECONDS); };
        auto y_of = [&](double v) { return bottom - int((bottom - top) * v / t_max); };
        auto plot = [&](const wxColour &colour, bool target, std::optional<double> TempSample::*member) {
            dc.SetPen(wxPen(colour, target ? 1 : 2, target ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
            std::optional<wxPoint> last;
            for (const TempSample &s : m_samples) {
                if (!(s.*member)) {
                    last.reset();
                    continue;
                }
                const wxPoint pt(x_of(s.t), y_of(*(s.*member)));
                if (last)
                    dc.DrawLine(*last, pt);
                last = pt;
            }
        };
        const wxColour hotend(0xE0, 0x4F, 0x3A), bed(0x3A, 0x7B, 0xE0);
        plot(hotend, true, &TempSample::hotend_target);
        plot(bed, true, &TempSample::bed_target);
        plot(hotend, false, &TempSample::hotend);
        plot(bed, false, &TempSample::bed);
        // Legend.
        int x = left;
        for (const auto &[colour, label] : { std::make_pair(hotend, _L("Extruder")), std::make_pair(bed, _L("Bed")) }) {
            dc.SetPen(wxPen(colour, 2));
            dc.DrawLine(x, bottom + em, x + 2 * em, bottom + em);
            dc.DrawText(label, x + 2 * em + em / 2, bottom + em - dc.GetCharHeight() / 2);
            x += 3 * em + dc.GetTextExtent(label).x + 2 * em;
        }
        const wxString span = _L("last 10 minutes, dashed: target");
        dc.DrawText(span, right - dc.GetTextExtent(span).x, bottom + em - dc.GetCharHeight() / 2);
    }

    const std::deque<TempSample> &m_samples;
};

struct USBMonitorPanel::priv
{
    USBMonitorPanel          *q;
    USB::USBPrinterConnection connection;
    wxTimer                   timer;

    // Detection: (port, baud) tried one after the other.
    std::vector<std::pair<std::string, unsigned>> candidates;
    size_t                    candidate { 0 };
    std::optional<double>     probe_start;
    std::set<std::string>     last_ports;       // ports when the last detection ended
    bool                      user_disconnected { false };
    double                    last_port_check { 0. };
    std::string               port_open;        // port of the open connection
    unsigned                  baud_open { 0 };

    // Print.
    std::optional<std::vector<std::string>> pending_print;
    wxString                  job_name;
    std::optional<double>     job_start;
    double                    job_paused_time { 0. };
    std::optional<double>     paused_at;

    std::deque<TempSample>    samples;
    double                    last_sample { -1e10 };

    // Controls.
    wxComboBox   *port { nullptr };
    wxComboBox   *baud { nullptr };
    wxButton     *btn_connect { nullptr };
    wxCheckBox   *autoconnect { nullptr };
    wxStaticText *conn_status { nullptr };
    wxStaticText *hotend_now { nullptr };
    wxSpinCtrl   *hotend_target { nullptr };
    wxStaticText *bed_now { nullptr };
    wxSpinCtrl   *bed_target { nullptr };
    wxSpinCtrl   *fan { nullptr };
    std::vector<wxToggleButton*> jog_steps;
    std::vector<wxWindow*> motion;              // disabled while printing
    std::vector<wxWindow*> connected_only;      // disabled while disconnected
    wxSpinCtrlDouble *extrude_len { nullptr };
    wxTextCtrl   *gcode { nullptr };
    wxStaticText *job { nullptr };
    wxStaticText *job_times { nullptr };
    wxGauge      *progress { nullptr };
    wxButton     *btn_print_sliced { nullptr };
    wxButton     *btn_print_file { nullptr };
    wxButton     *btn_pause { nullptr };
    wxButton     *btn_cancel { nullptr };
    TempGraph    *graph { nullptr };
    wxTextCtrl   *console { nullptr };
    wxCheckBox   *hide_temps { nullptr };

    explicit priv(USBMonitorPanel *q) : q(q) {}

    USB::State state()
    {
        USB::State st = USB::State::Disconnected;
        if (connection.is_open())
            connection.with_sender([&](USB::GCodeSender &s, double) { st = s.state(); });
        return st;
    }
    bool printing()
    {
        const USB::State st = state();
        return st == USB::State::Printing || st == USB::State::Paused;
    }

    void send(std::initializer_list<std::string> cmds)
    {
        if (! connection.is_open())
            return;
        connection.with_sender([&](USB::GCodeSender &s, double now) {
            for (const std::string &c : cmds)
                s.send_manual(c, now);
        });
    }

    void append_console(const wxString &text)
    {
        console->AppendText(text + "\n");
        if (console->GetNumberOfLines() > 3000)
            console->Remove(0, console->XYToPosition(0, 1000));
    }

    // ---------------------------------------------------------------- connection and detection

    void open(const std::string &port_name, unsigned baud_rate, bool log)
    {
        connection.open(port_name, baud_rate);
        port_open = port_name;
        baud_open = baud_rate;
        if (log)
            append_console(format_wxstr(_L("Connecting to %1% at %2% baud..."), from_u8(port_name), baud_rate));
    }

    void close()
    {
        connection.close();
        port_open.clear();
        probe_start.reset();
    }

    // Starts the detection over all the likely ports, the chosen (or saved) port and baud rate first.
    void start_detection()
    {
        candidates.clear();
        candidate = 0;
        const std::string chosen_port = port->GetSelection() > 0 ? into_u8(port->GetValue()) : std::string();
        unsigned long chosen_baud = 0;
        if (baud->GetSelection() != 0)
            baud->GetValue().ToULong(&chosen_baud);
        std::vector<unsigned> bauds;
        const std::string saved_baud = wxGetApp().app_config->get(CONFIG_BAUD);
        if (chosen_baud)
            bauds.push_back(unsigned(chosen_baud));
        else {
            if (! saved_baud.empty())
                bauds.push_back(unsigned(std::stoul(saved_baud)));
            for (unsigned b : DETECT_BAUDS)
                if (std::find(bauds.begin(), bauds.end(), b) == bauds.end())
                    bauds.push_back(b);
        }
        std::vector<std::string> ports = printer_ports();
        last_ports = std::set<std::string>(ports.begin(), ports.end());
        if (! chosen_port.empty())
            ports = { chosen_port };
        else if (const std::string saved = wxGetApp().app_config->get(CONFIG_PORT); ! saved.empty() && last_ports.count(saved)) {
            ports.erase(std::remove(ports.begin(), ports.end(), saved), ports.end());
            ports.insert(ports.begin(), saved);
        }
        for (const std::string &p : ports)
            for (unsigned b : bauds)
                candidates.emplace_back(p, b);
        if (candidates.empty())
            return;
        append_console(_L("Looking for the printer on the USB ports..."));
        try_candidate();
    }

    void try_candidate()
    {
        while (candidate < candidates.size()) {
            const auto &[port_name, baud_rate] = candidates[candidate];
            try {
                open(port_name, baud_rate, false);
                probe_start = USB::now_seconds();
                return;
            } catch (const std::exception &) {
                ++ candidate;
            }
        }
        probe_start.reset();
        candidates.clear();
        append_console(_L("No printer answered on the USB ports. Check the cable and that the printer is on."));
    }

    // Called by the timer while a port is being probed.
    void probe(USB::State st, double now)
    {
        if (st == USB::State::Idle) {
            probe_start.reset();
            candidates.clear();
            append_console(format_wxstr(_L("Printer found on %1% at %2% baud."), from_u8(port_open), baud_open));
            wxGetApp().app_config->set(CONFIG_PORT, port_open);
            wxGetApp().app_config->set(CONFIG_BAUD, std::to_string(baud_open));
            // The "Print via USB" button of the sidebar.
            if (Plater *plater = wxGetApp().plater())
                plater->show_action_buttons();
            return;
        }
        if (now - *probe_start > DETECT_TIMEOUT) {
            connection.close();
            port_open.clear();
            ++ candidate;
            try_candidate();
        }
    }

    void toggle_connection()
    {
        if (connection.is_open() || probe_start) {
            if (printing() &&
                MessageDialog(q, _L("A print is in progress. Disconnecting stops it. Disconnect anyway?"),
                              _L("USB printer"), wxYES_NO | wxICON_WARNING).ShowModal() != wxID_YES)
                return;
            close();
            candidates.clear();
            pending_print.reset();
            user_disconnected = true;
            append_console(_L("Disconnected"));
            return;
        }
        user_disconnected = false;
        // A port and baud rate chosen: connect to them directly; otherwise detect.
        unsigned long baud_rate = 0;
        if (port->GetSelection() != 0 && baud->GetSelection() != 0 && baud->GetValue().ToULong(&baud_rate)) {
            try {
                open(into_u8(port->GetValue()), unsigned(baud_rate), true);
                probe_start = USB::now_seconds();
                candidates = { { port_open, unsigned(baud_rate) } };
                candidate = 0;
            } catch (const std::exception &ex) {
                ErrorDialog(q, from_u8(ex.what()), false).ShowModal();
            }
        } else
            start_detection();
    }

    void fill_ports()
    {
        const wxString current = port->GetSelection() > 0 ? port->GetValue() : wxString();
        port->Clear();
        port->Append(_L("Automatic"));
        for (const std::string &p : printer_ports())
            port->Append(from_u8(p));
        if (! current.IsEmpty() && port->FindString(current) != wxNOT_FOUND)
            port->SetStringSelection(current);
        else
            port->SetSelection(0);
    }

    // ---------------------------------------------------------------- printing

    void start_print(std::vector<std::string> commands, const wxString &name)
    {
        job_name = name;
        if (state() == USB::State::Idle) {
            append_console(format_wxstr(_L("Printing %1%"), name));
            connection.with_sender([&](USB::GCodeSender &s, double now) { s.start(std::move(commands), now); });
            job_start = USB::now_seconds();
            job_paused_time = 0.;
            paused_at.reset();
        } else {
            pending_print = std::move(commands);
            append_console(_L("The print starts when the printer is connected."));
            if (! connection.is_open() && ! probe_start) {
                user_disconnected = false;
                start_detection();
            }
        }
    }

    void print_sliced()
    {
        const std::string path = wxGetApp().plater()->sliced_gcode_path();
        if (path.empty()) {
            ErrorDialog(q, _L("Slice the plate first: there is no sliced G-code to print."), false).ShowModal();
            return;
        }
        std::vector<std::string> commands;
        wxString error;
        if (! load_gcode(path, commands, error)) {
            ErrorDialog(q, error, false).ShowModal();
            return;
        }
        start_print(std::move(commands), from_u8(wxGetApp().plater()->get_upload_filename()));
    }

    void print_file()
    {
        wxFileDialog dlg(q, _L("G-code to print"), from_u8(wxGetApp().app_config->get_last_output_dir("")), "",
                         "G-code (*.gcode;*.gco;*.g;*.ngc;*.bgcode)|*.gcode;*.GCODE;*.gco;*.GCO;*.g;*.G;*.ngc;*.NGC;*.bgcode;*.BGCODE",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK)
            return;
        std::vector<std::string> commands;
        wxString error;
        if (! load_gcode(into_u8(dlg.GetPath()), commands, error)) {
            ErrorDialog(q, error, false).ShowModal();
            return;
        }
        start_print(std::move(commands), wxFileName(dlg.GetPath()).GetFullName());
    }

    void pause_resume()
    {
        connection.with_sender([this](USB::GCodeSender &s, double now) {
            if (s.state() == USB::State::Paused) {
                s.resume(now);
                if (paused_at)
                    job_paused_time += now - *paused_at;
                paused_at.reset();
            } else {
                s.pause();
                paused_at = now;
            }
        });
    }

    void cancel()
    {
        if (MessageDialog(q, _L("Cancel the print?"), _L("USB printer"), wxYES_NO | wxICON_QUESTION).ShowModal() == wxID_YES)
            connection.with_sender([](USB::GCodeSender &s, double now) { s.cancel(now); });
    }

    // ---------------------------------------------------------------- controls

    double jog_step() const
    {
        static const double steps[] = { 0.1, 1., 10., 100. };
        for (size_t i = 0; i < jog_steps.size(); ++ i)
            if (jog_steps[i]->GetValue())
                return steps[i];
        return 10.;
    }

    void jog(char axis, int direction)
    {
        const int feed = axis == 'Z' ? FEED_Z : FEED_XY;
        send({ "G91", format("G1 %1%%2% F%3%", axis, float_to_string_decimal_point(direction * jog_step(), 2), feed), "G90" });
    }

    void extrude(int direction)
    {
        send({ "G91", format("G1 E%1% F%2%", float_to_string_decimal_point(direction * extrude_len->GetValue(), 2), FEED_E), "G90" });
    }

    void send_gcode()
    {
        const std::string cmd = into_u8(gcode->GetValue());
        if (cmd.empty() || ! connection.is_open())
            return;
        append_console("> " + from_u8(cmd));
        send({ cmd });
        gcode->Clear();
    }

    // ---------------------------------------------------------------- periodic update

    void update()
    {
        const double now = USB::now_seconds();
        USB::State st = USB::State::Disconnected;
        size_t sent = 0, total = 0;
        USB::Temperatures t;
        std::vector<std::string> log;
        if (connection.is_open())
            connection.with_sender([&](USB::GCodeSender &s, double) {
                st    = s.state();
                sent  = s.sent();
                total = s.total();
                t     = s.temperatures();
                log   = s.take_log();
            });
        const bool hide = hide_temps->GetValue();
        for (const std::string &line : log)
            if (! (hide && (boost::starts_with(line, "T:") || boost::starts_with(line, " T:") || boost::starts_with(line, "ok T:"))))
                append_console(from_u8(line));

        if (probe_start)
            probe(st, now);

        // Unplugged printer, or a new port to look at.
        if (now - last_port_check > 2.) {
            last_port_check = now;
            const std::vector<std::string> ports = printer_ports();
            const std::set<std::string> port_set(ports.begin(), ports.end());
            if (connection.is_open() && ! probe_start && ! port_set.count(port_open)) {
                append_console(_L("The printer was disconnected."));
                close();
            }
            if (port_set != last_ports && port->GetCount() != ports.size() + 1)
                fill_ports();
            if (! connection.is_open() && ! probe_start && ! user_disconnected && autoconnect->GetValue() &&
                ! port_set.empty() && port_set != last_ports)
                start_detection();
        }

        if (pending_print && st == USB::State::Idle) {
            std::vector<std::string> commands = std::move(*pending_print);
            pending_print.reset();
            start_print(std::move(commands), job_name);
            st = USB::State::Printing;
        }
        if (st == USB::State::Paused && ! paused_at)
            paused_at = now;

        // Temperatures.
        auto fmt = [](const std::optional<double> &v) { return v ? wxString::Format("%.1f °C", *v) : wxString("-"); };
        auto target = [](const std::optional<double> &v) { return v && *v > 0. ? wxString::Format(" / %.0f °C", *v) : wxString(); };
        hotend_now->SetLabel(fmt(t.hotend) + target(t.hotend_target));
        bed_now->SetLabel(fmt(t.bed) + target(t.bed_target));
        if (connection.is_open() && now - last_sample >= 1.) {
            last_sample = now;
            samples.push_back({ now, t.hotend, t.hotend_target, t.bed, t.bed_target });
            while (! samples.empty() && samples.front().t < now - GRAPH_SECONDS)
                samples.pop_front();
            graph->Refresh();
        }

        // Connection.
        wxString status;
        switch (st) {
        case USB::State::Disconnected: status = probe_start ? _L("Looking for the printer...") : _L("Disconnected"); break;
        case USB::State::Connecting:   status = probe_start && candidates.size() > 1 ?
                                                 format_wxstr(_L("Trying %1% at %2% baud..."), from_u8(port_open), baud_open) :
                                                 _L("Connecting"); break;
        case USB::State::Idle:         status = format_wxstr(_L("Connected: %1%, %2% baud"), from_u8(port_open), baud_open); break;
        case USB::State::Printing:     status = _L("Printing"); break;
        case USB::State::Paused:       status = _L("Paused"); break;
        case USB::State::Error:        status = _L("Error: the printer stopped (see the terminal)"); break;
        }
        conn_status->SetLabel(status);
        const bool open = connection.is_open() || probe_start.has_value();
        btn_connect->SetLabel(open ? _L("Disconnect") : _L("Connect"));
        port->Enable(! open);
        baud->Enable(! open);

        // Job.
        const bool is_printing = st == USB::State::Printing || st == USB::State::Paused;
        if (is_printing && total > 0) {
            const double fraction = double(sent) / double(total);
            progress->SetValue(int(1000. * fraction));
            job->SetLabel(format_wxstr("%1%  —  %2%%%", job_name, int(100. * fraction)));
            if (job_start) {
                const double elapsed = now - *job_start - job_paused_time - (paused_at ? now - *paused_at : 0.);
                wxString times = format_wxstr(_L("Printing time %1%"), from_u8(short_time(get_time_dhms(float(elapsed)))));
                if (fraction > 0.02)
                    times += "   " + format_wxstr(_L("Remaining about %1%"), from_u8(short_time(get_time_dhms(float(elapsed * (1. - fraction) / fraction)))));
                job_times->SetLabel(times);
            }
        } else {
            progress->SetValue(total > 0 && sent >= total ? 1000 : 0);
            job->SetLabel(pending_print ? format_wxstr(_L("%1% (waiting for the printer)"), job_name) :
                          job_name.IsEmpty() ? _L("No print") : format_wxstr(_L("%1% (finished)"), job_name));
            if (! (total > 0 && sent >= total))
                job_times->SetLabel("");
        }
        btn_pause->SetLabel(st == USB::State::Paused ? _L("Resume") : _L("Pause"));
        btn_pause->Enable(is_printing);
        btn_cancel->Enable(is_printing);
        btn_print_sliced->Enable(! is_printing);
        btn_print_file->Enable(! is_printing);
        const bool ready = st == USB::State::Idle || is_printing;
        for (wxWindow *w : connected_only)
            w->Enable(ready);
        for (wxWindow *w : motion)
            w->Enable(st == USB::State::Idle);
    }
};

USBMonitorPanel::USBMonitorPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY), p(new priv(this))
{
    s_instance = this;
    const int  em   = wxGetApp().em_unit();
    const bool dark = wxGetApp().dark_mode();
    SetBackgroundColour(TismaTheme::panel_bg(dark));

    auto section = [this](wxSizer *parent_sizer, const wxString &title, int proportion = 0) {
        auto *box = new wxStaticBoxSizer(wxVERTICAL, this, title);
        parent_sizer->Add(box, proportion, wxEXPAND | wxBOTTOM, wxGetApp().em_unit());
        return box;
    };
    auto button = [this](const wxString &label, int width_em = 0) {
        auto *b = new wxButton(this, wxID_ANY, label, wxDefaultPosition,
                               width_em > 0 ? wxSize(width_em * wxGetApp().em_unit(), -1) : wxDefaultSize, width_em > 0 ? wxBU_EXACTFIT : 0);
        return b;
    };

    auto *main = new wxBoxSizer(wxHORIZONTAL);
    auto *left = new wxBoxSizer(wxVERTICAL);
    auto *right = new wxBoxSizer(wxVERTICAL);
    main->Add(left, 0, wxEXPAND | wxRIGHT, em);
    main->Add(right, 1, wxEXPAND);

    // ---- Connection
    auto *box = section(left, _L("Connection"));
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    p->port = new wxComboBox(this, wxID_ANY, "", wxDefaultPosition, wxSize(15 * em, -1), 0, nullptr, wxCB_READONLY);
    row->Add(p->port, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    p->baud = new wxComboBox(this, wxID_ANY, "", wxDefaultPosition, wxSize(10 * em, -1), 0, nullptr, wxCB_READONLY);
    p->baud->Append(_L("Automatic"));
    for (const char *b : { "115200", "250000", "230400", "500000", "1000000", "57600" })
        p->baud->Append(b);
    p->baud->SetSelection(0);
    row->Add(p->baud, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    p->btn_connect = button(_L("Connect"));
    row->Add(p->btn_connect, 0, wxALIGN_CENTER_VERTICAL);
    box->Add(row, 0, wxEXPAND | wxALL, em / 2);
    p->conn_status = new wxStaticText(this, wxID_ANY, _L("Disconnected"));
    box->Add(p->conn_status, 0, wxLEFT | wxRIGHT, em / 2);
    p->autoconnect = new wxCheckBox(this, wxID_ANY, _L("Detect and connect the printer automatically"));
    p->autoconnect->SetValue(wxGetApp().app_config->get(CONFIG_AUTOCONNECT) != "0");
    box->Add(p->autoconnect, 0, wxALL, em / 2);

    // ---- Temperatures
    const DynamicPrintConfig &filament = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    auto first_int = [&filament](const char *key, int fallback) {
        const auto *opt = filament.option<ConfigOptionInts>(key);
        return opt && ! opt->values.empty() ? opt->values.front() : fallback;
    };
    auto heater = [&](const wxString &title, wxStaticText *&now_text, wxSpinCtrl *&target, int value, int max, const char *cmd) {
        auto *b = section(left, title);
        auto *r = new wxBoxSizer(wxHORIZONTAL);
        // Wide enough for "250.0 °C / 250 °C".
        now_text = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxSize(15 * em, -1), wxST_NO_AUTORESIZE);
        now_text->SetFont(wxGetApp().bold_font().Scaled(1.2f));
        r->Add(now_text, 1, wxALIGN_CENTER_VERTICAL);
        target = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, spin_ctrl_size(this, 7), wxSP_ARROW_KEYS, 0, max, value);
        r->Add(target, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);
        r->Add(new wxStaticText(this, wxID_ANY, "°C"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
        auto *on = button(_L("Pre-heat"));
        auto *off = button(_L("Off"));
        r->Add(on, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);
        r->Add(off, 0, wxALIGN_CENTER_VERTICAL);
        b->Add(r, 0, wxEXPAND | wxALL, em / 2);
        wxSpinCtrl *spin = target;
        const std::string command = cmd;
        on->Bind(wxEVT_BUTTON, [this, spin, command](wxCommandEvent &) { p->send({ command + " S" + std::to_string(spin->GetValue()) }); });
        off->Bind(wxEVT_BUTTON, [this, command](wxCommandEvent &) { p->send({ command + " S0" }); });
        p->connected_only.insert(p->connected_only.end(), { on, off });
    };
    heater(_L("Extruder"), p->hotend_now, p->hotend_target, first_int("temperature", 210), 350, "M104");
    heater(_L("Bed"), p->bed_now, p->bed_target, first_int("bed_temperature", 60), 150, "M140");

    // ---- Printer control
    box = section(left, _L("Printer control"));
    auto *jog = new wxBoxSizer(wxHORIZONTAL);
    auto *xy = new wxFlexGridSizer(3, 3, em / 4, em / 4);
    auto arrow = [&](const wxString &label, std::function<void()> action) {
        auto *b = button(label, 5);
        b->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
        p->motion.push_back(b);
        return b;
    };
    xy->AddSpacer(1);
    xy->Add(arrow("Y+", [this]() { p->jog('Y', 1); }));
    xy->AddSpacer(1);
    xy->Add(arrow("X-", [this]() { p->jog('X', -1); }));
    xy->Add(arrow(_L("Home"), [this]() { p->send({ "G28 X Y" }); }));
    xy->Add(arrow("X+", [this]() { p->jog('X', 1); }));
    xy->AddSpacer(1);
    xy->Add(arrow("Y-", [this]() { p->jog('Y', -1); }));
    xy->AddSpacer(1);
    auto *xy_box = new wxBoxSizer(wxVERTICAL);
    xy_box->Add(new wxStaticText(this, wxID_ANY, "X / Y"), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, em / 4);
    xy_box->Add(xy);
    jog->Add(xy_box, 0, wxRIGHT, em * 2);
    auto *z = new wxBoxSizer(wxVERTICAL);
    z->Add(new wxStaticText(this, wxID_ANY, "Z"), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, em / 4);
    z->Add(arrow("Z+", [this]() { p->jog('Z', 1); }), 0, wxBOTTOM, em / 4);
    z->Add(arrow(_L("Home"), [this]() { p->send({ "G28 Z" }); }), 0, wxBOTTOM, em / 4);
    z->Add(arrow("Z-", [this]() { p->jog('Z', -1); }));
    jog->Add(z, 0, wxRIGHT, em * 2);
    auto *other = new wxBoxSizer(wxVERTICAL);
    auto *home_all = button(_L("Home all"));
    home_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->send({ "G28" }); });
    auto *motors_off = button(_L("Motors off"));
    motors_off->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->send({ "M18" }); });
    other->Add(home_all, 0, wxEXPAND | wxBOTTOM, em / 4);
    other->Add(motors_off, 0, wxEXPAND);
    p->motion.insert(p->motion.end(), { home_all, motors_off });
    jog->Add(other, 0, wxALIGN_CENTER_VERTICAL);
    box->Add(jog, 0, wxALL, em / 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _L("Jog distance") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    for (const char *label : { "0.1", "1", "10", "100" }) {
        auto *t = new wxToggleButton(this, wxID_ANY, label, wxDefaultPosition, wxSize(4 * em, -1), wxBU_EXACTFIT);
        t->SetValue(std::string(label) == "10");
        t->Bind(wxEVT_TOGGLEBUTTON, [this, t](wxCommandEvent &) {
            for (wxToggleButton *other : p->jog_steps)
                other->SetValue(other == t);
        });
        p->jog_steps.push_back(t);
        row->Add(t, 0, wxRIGHT, em / 4);
    }
    row->Add(new wxStaticText(this, wxID_ANY, "mm"), 0, wxALIGN_CENTER_VERTICAL);
    box->Add(row, 0, wxALL, em / 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _L("Extruder") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    p->extrude_len = new wxSpinCtrlDouble(this, wxID_ANY, "", wxDefaultPosition, spin_ctrl_size(this, 7), wxSP_ARROW_KEYS, 0.1, 200., 5., 1.);
    row->Add(p->extrude_len, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);
    row->Add(new wxStaticText(this, wxID_ANY, "mm"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    auto *btn_extrude = button(_L("Extrude"));
    auto *btn_retract = button(_L("Retract"));
    btn_extrude->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->extrude(1); });
    btn_retract->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->extrude(-1); });
    btn_extrude->SetToolTip(_L("The firmware refuses to extrude with a cold nozzle: pre-heat it first."));
    row->Add(btn_extrude, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);
    row->Add(btn_retract, 0, wxALIGN_CENTER_VERTICAL);
    p->motion.insert(p->motion.end(), { btn_extrude, btn_retract });
    box->Add(row, 0, wxALL, em / 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _L("Fan") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    p->fan = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, spin_ctrl_size(this, 7), wxSP_ARROW_KEYS, 0, 100, 100);
    row->Add(p->fan, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);
    row->Add(new wxStaticText(this, wxID_ANY, "%"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    auto *fan_on = button(_L("Set"));
    auto *fan_off = button(_L("Off"));
    fan_on->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->send({ "M106 S" + std::to_string(int(std::lround(2.55 * p->fan->GetValue()))) }); });
    fan_off->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->send({ "M107" }); });
    row->Add(fan_on, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);
    row->Add(fan_off, 0, wxALIGN_CENTER_VERTICAL);
    p->connected_only.insert(p->connected_only.end(), { fan_on, fan_off });
    box->Add(row, 0, wxALL, em / 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _L("Send G-code") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    p->gcode = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    row->Add(p->gcode, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);
    auto *btn_send = button(_L("Send"));
    row->Add(btn_send, 0, wxALIGN_CENTER_VERTICAL);
    p->connected_only.insert(p->connected_only.end(), { p->gcode, btn_send });
    box->Add(row, 0, wxEXPAND | wxALL, em / 2);

    // ---- Active print
    box = section(right, _L("Active print"));
    p->job = new wxStaticText(this, wxID_ANY, _L("No print"));
    p->job->SetFont(wxGetApp().bold_font());
    box->Add(p->job, 0, wxLEFT | wxRIGHT | wxTOP, em / 2);
    p->progress = new wxGauge(this, wxID_ANY, 1000);
    box->Add(p->progress, 0, wxEXPAND | wxALL, em / 2);
    p->job_times = new wxStaticText(this, wxID_ANY, "");
    box->Add(p->job_times, 0, wxLEFT | wxRIGHT, em / 2);
    row = new wxBoxSizer(wxHORIZONTAL);
    p->btn_print_sliced = button(_L("Print the sliced plate"));
    p->btn_print_file   = button(_L("Print a file") + dots);
    p->btn_pause        = button(_L("Pause"));
    p->btn_cancel       = button(_L("Cancel print"));
    row->Add(p->btn_print_sliced, 0, wxRIGHT, em / 4);
    row->Add(p->btn_print_file, 0, wxRIGHT, em);
    row->Add(p->btn_pause, 0, wxRIGHT, em / 4);
    row->Add(p->btn_cancel, 0);
    box->Add(row, 0, wxALL, em / 2);

    // ---- Temperature graph
    box = section(right, _L("Temperature"));
    p->graph = new TempGraph(this, p->samples);
    box->Add(p->graph, 0, wxEXPAND | wxALL, em / 4);

    // ---- Terminal
    box = section(right, _L("Terminal"), 1);
    p->console = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(40 * em, 12 * em),
                                wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
    p->console->SetFont(wxGetApp().code_font());
    box->Add(p->console, 1, wxEXPAND | wxALL, em / 4);
    p->hide_temps = new wxCheckBox(this, wxID_ANY, _L("Hide the temperature reports"));
    p->hide_temps->SetValue(true);
    box->Add(p->hide_temps, 0, wxALL, em / 4);

    SetSizer(main);

    for (wxWindow *w : { static_cast<wxWindow*>(this) })
        wxGetApp().UpdateDarkUI(w);

    p->btn_connect->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->toggle_connection(); p->update(); });
    p->port->Bind(wxEVT_COMBOBOX_DROPDOWN, [this](wxCommandEvent &) { p->fill_ports(); });
    p->autoconnect->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
        wxGetApp().app_config->set(CONFIG_AUTOCONNECT, p->autoconnect->GetValue() ? "1" : "0");
    });
    btn_send->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->send_gcode(); });
    p->gcode->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { p->send_gcode(); });
    p->btn_print_sliced->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->print_sliced(); p->update(); });
    p->btn_print_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->print_file(); p->update(); });
    p->btn_pause->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->pause_resume(); p->update(); });
    p->btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { p->cancel(); p->update(); });
    p->timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { p->update(); });

    p->fill_ports();
    // The first update looks for a printer already plugged in (when the detection is on).
    p->update();
    p->timer.Start(200);
}

USBMonitorPanel::~USBMonitorPanel()
{
    p->timer.Stop();
    p->connection.close();
    if (s_instance == this)
        s_instance = nullptr;
}

USBMonitorPanel* USBMonitorPanel::instance() { return s_instance; }

void USBMonitorPanel::show()
{
    if (wxGetApp().mainframe)
        wxGetApp().mainframe->show_usb_printer();
}

bool USBMonitorPanel::has_printer()
{
    if (s_instance && s_instance->p->connection.is_open())
        return true;
    return ! wxGetApp().app_config->get(CONFIG_PORT).empty();
}

void USBMonitorPanel::print_file(const std::string &path, const std::string &name)
{
    std::vector<std::string> commands;
    wxString error;
    if (! load_gcode(path, commands, error)) {
        ErrorDialog(wxGetApp().mainframe, error, false).ShowModal();
        return;
    }
    show();
    if (s_instance == nullptr)
        return;
    priv &p = *s_instance->p;
    if (p.printing()) {
        ErrorDialog(s_instance, _L("A print is already in progress on the printer."), false).ShowModal();
        return;
    }
    p.start_print(std::move(commands), from_u8(name));
    p.update();
}

void USBMonitorPanel::sys_color_changed()
{
    SetBackgroundColour(TismaTheme::panel_bg(wxGetApp().dark_mode()));
    wxGetApp().UpdateDarkUI(this);
    p->graph->Refresh();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
