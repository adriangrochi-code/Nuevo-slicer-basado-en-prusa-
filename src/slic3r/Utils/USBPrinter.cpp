#include "USBPrinter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <stdexcept>

#include <boost/asio.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>

namespace Slic3r {
namespace USB {

const char* state_name(State state)
{
    switch (state) {
    case State::Disconnected: return "Disconnected";
    case State::Connecting:   return "Connecting";
    case State::Idle:         return "Idle";
    case State::Printing:     return "Printing";
    case State::Paused:       return "Paused";
    case State::Error:        return "Error";
    }
    return "";
}

double now_seconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Commands that may legitimately take long without an answer (heating, homing, probing ...).
static bool is_long_command(const std::string &cmd)
{
    static const char *long_cmds[] = { "M109", "M190", "M191", "G28", "G29", "G4", "M400", "M600", "M303", "G34", "G80", "G81", "M0", "M1" };
    for (const char *c : long_cmds) {
        const size_t n = std::strlen(c);
        if (cmd.compare(0, n, c) == 0 && (cmd.size() == n || cmd[n] == ' '))
            return true;
    }
    return false;
}

// --------------------------------------------------------------- GCodeSender

GCodeSender::GCodeSender(Transport &transport) : m_transport(transport) {}

std::vector<std::string> GCodeSender::prepare(const std::string &gcode)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= gcode.size()) {
        size_t end = gcode.find('\n', start);
        if (end == std::string::npos)
            end = gcode.size();
        std::string line = gcode.substr(start, end - start);
        if (const size_t semi = line.find(';'); semi != std::string::npos)
            line.erase(semi);
        boost::algorithm::trim(line);
        if (! line.empty())
            out.emplace_back(std::move(line));
        start = end + 1;
    }
    return out;
}

std::string GCodeSender::numbered_line(int line_no, const std::string &cmd)
{
    std::string line = "N" + std::to_string(line_no) + " " + cmd;
    unsigned char checksum = 0;
    for (const char c : line)
        checksum ^= static_cast<unsigned char>(c);
    return line + "*" + std::to_string(int(checksum));
}

void GCodeSender::log(std::string msg)
{
    m_log.emplace_back(std::move(msg));
    if (m_log.size() > 2000)
        m_log.erase(m_log.begin(), m_log.begin() + 1000);
}

std::vector<std::string> GCodeSender::take_log()
{
    std::vector<std::string> out;
    out.swap(m_log);
    return out;
}

void GCodeSender::connect(double now)
{
    m_state         = State::Connecting;
    m_history.clear();
    m_resend_from.reset();
    m_in_flight     = 0;
    m_next_line_no  = 0;
    m_last_activity = now;
    // M110 N0 resets the line number of the firmware; the next line is N1.
    this->send_numbered("M110 N0", now);
}

void GCodeSender::disconnected()
{
    if (m_state == State::Printing || m_state == State::Paused)
        this->log("Disconnected during the print");
    m_state     = State::Disconnected;
    m_in_flight = 0;
    m_manual.clear();
    m_resend_from.reset();
    m_finishing = false;
}

void GCodeSender::start(std::vector<std::string> commands, double now)
{
    m_file     = std::move(commands);
    m_file_pos = 0;
    m_finishing = false;
    m_state    = State::Printing;
    this->log("Print started: " + std::to_string(m_file.size()) + " lines");
    this->send_next(now);
}

void GCodeSender::pause()
{
    if (m_state == State::Printing) {
        m_state = State::Paused;
        this->log("Paused");
    }
}

void GCodeSender::resume(double now)
{
    if (m_state == State::Paused) {
        m_state = State::Printing;
        this->log("Resumed");
        this->send_next(now);
    }
}

void GCodeSender::cancel(double now)
{
    if (m_state != State::Printing && m_state != State::Paused)
        return;
    // M108 is handled by the emergency parser of Marlin even while heating: no line number.
    m_transport.write("M108\n");
    m_file.clear();
    m_file_pos = 0;
    m_state    = State::Idle;
    m_finishing = true;
    for (const std::string &cmd : cancel_commands)
        m_manual.push_back(cmd);
    this->log("Print cancelled");
    this->send_next(now);
}

void GCodeSender::send_manual(const std::string &cmd, double now)
{
    std::string c = cmd;
    if (const size_t semi = c.find(';'); semi != std::string::npos)
        c.erase(semi);
    boost::algorithm::trim(c);
    if (c.empty())
        return;
    m_manual.push_back(c);
    this->send_next(now);
}

void GCodeSender::send_numbered(const std::string &cmd, double now)
{
    const int n = m_next_line_no ++;
    const std::string line = numbered_line(n, cmd);
    m_history.emplace_back(n, cmd);
    while (m_history.size() > 512)
        m_history.pop_front();
    m_transport.write(line + "\n");
    ++ m_in_flight;
    m_last_activity = now;
}

void GCodeSender::send_next(double now)
{
    if (m_in_flight > 0 || m_state == State::Disconnected || m_state == State::Connecting || m_state == State::Error)
        return;
    if (m_resend_from) {
        // Send again the lines the firmware asked for, with their original numbers.
        const int n = *m_resend_from;
        auto it = std::find_if(m_history.begin(), m_history.end(), [n](const auto &h) { return h.first == n; });
        if (it == m_history.end()) {
            this->log("Resend of line " + std::to_string(n) + " requested, but it is no longer known");
            m_resend_from.reset();
        } else {
            m_transport.write(numbered_line(it->first, it->second) + "\n");
            ++ m_in_flight;
            m_last_activity = now;
            if (++ it == m_history.end())
                m_resend_from.reset();
            else
                m_resend_from = it->first;
            return;
        }
    }
    if (! m_manual.empty()) {
        const std::string cmd = m_manual.front();
        m_manual.pop_front();
        this->send_numbered(cmd, now);
        return;
    }
    if (m_finishing) {
        m_finishing = false;
        this->log("Cancel sequence sent");
    }
    if (m_state == State::Printing) {
        if (m_file_pos < m_file.size()) {
            this->send_numbered(m_file[m_file_pos ++], now);
        } else {
            m_state = State::Idle;
            this->log("Print finished");
        }
        return;
    }
    if (now - m_last_poll >= poll_period && now - m_last_report >= poll_period) {
        m_last_poll = now;
        this->send_numbered("M105", now);
    }
}

void GCodeSender::parse_temperatures(const std::string &line)
{
    static const std::regex re_t(R"((?:^|\s)T0?:\s*([-\d.]+)\s*(?:/\s*([-\d.]+))?)");
    static const std::regex re_b(R"((?:^|\s)B:\s*([-\d.]+)\s*(?:/\s*([-\d.]+))?)");
    std::smatch m;
    bool found = false;
    if (std::regex_search(line, m, re_t)) {
        m_temperatures.hotend = std::atof(m[1].str().c_str());
        if (m[2].matched)
            m_temperatures.hotend_target = std::atof(m[2].str().c_str());
        found = true;
    }
    if (std::regex_search(line, m, re_b)) {
        m_temperatures.bed = std::atof(m[1].str().c_str());
        if (m[2].matched)
            m_temperatures.bed_target = std::atof(m[2].str().c_str());
        found = true;
    }
    (void)found;
}

void GCodeSender::on_line(const std::string &raw, double now)
{
    std::string line = boost::algorithm::trim_copy(raw);
    if (line.empty())
        return;
    m_last_activity = now;
    if (line.find("T:") != std::string::npos || line.find("B:") != std::string::npos) {
        this->parse_temperatures(line);
        if (! boost::starts_with(line, "ok"))
            m_last_report = now;
    }
    if (boost::starts_with(line, "ok")) {
        if (m_in_flight > 0)
            -- m_in_flight;
        if (m_state == State::Connecting) {
            m_state = State::Idle;
            this->log("Connected");
            // Ask the firmware to report the temperatures every 2 s (ignored if unsupported).
            m_manual.push_front("M155 S2");
        }
        this->send_next(now);
        return;
    }
    // "Resend: 12" (Marlin) or "rs N12" / "rs 12" (RepRapFirmware, Repetier).
    static const std::regex re_resend(R"(^(?:Resend:\s*|rs\s+N?)(\d+))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(line, m, re_resend)) {
        m_resend_from = std::atoi(m[1].str().c_str());
        this->log("Printer asked to resend from line " + m[1].str());
        // The "ok" that follows the resend request frees the slot.
        return;
    }
    if (boost::starts_with(line, "busy:") || boost::starts_with(line, "echo:busy")) {
        // Long command in progress, keep waiting.
        return;
    }
    if (boost::starts_with(line, "start")) {
        // The printer has been reset (e.g. the port toggled DTR): start over.
        this->log("Printer restarted");
        if (m_state == State::Printing || m_state == State::Paused) {
            m_state = State::Error;
            this->log("The printer restarted during the print, the print is lost");
            return;
        }
        this->connect(now);
        return;
    }
    if (boost::istarts_with(line, "Error:") || boost::starts_with(line, "!!")) {
        this->log(line);
        if (line.find("halted") != std::string::npos || line.find("Halted") != std::string::npos ||
            line.find("kill") != std::string::npos || boost::starts_with(line, "!!")) {
            m_state = State::Error;
            m_file.clear();
        }
        return;
    }
    this->log(line);
}

void GCodeSender::tick(double now)
{
    if (m_state == State::Disconnected || m_state == State::Error)
        return;
    if (m_in_flight > 0) {
        // Long commands (heating, homing ...) are kept alive by "busy:" or temperature reports.
        const std::string last = m_history.empty() ? std::string() : m_history.back().second;
        const double timeout = is_long_command(last) ? 10. * response_timeout : response_timeout;
        if (now - m_last_activity > timeout) {
            // The "ok" may have been lost: a numbered M105 either gets acknowledged or makes the
            // firmware request the missing line again.
            this->log("No answer from the printer, sending M105");
            m_in_flight = 0;
            this->send_numbered("M105", now);
        }
        return;
    }
    this->send_next(now);
}

// ------------------------------------------------------ USBPrinterConnection

namespace asio = boost::asio;

struct USBPrinterConnection::Priv : public Transport
{
    asio::io_context                  io;
    std::unique_ptr<asio::serial_port> port;
    asio::streambuf                   buffer;
    std::thread                       thread;
    std::mutex                        mutex;
    std::unique_ptr<asio::steady_timer> timer;
    GCodeSender                       sender { *this };

    void write(const std::string &data) override
    {
        // Called with the mutex held.
        if (port && port->is_open()) {
            boost::system::error_code ec;
            asio::write(*port, asio::buffer(data), ec);
        }
    }

    void read_next()
    {
        asio::async_read_until(*port, buffer, '\n', [this](const boost::system::error_code &ec, size_t) {
            if (ec)
                return;
            std::istream is(&buffer);
            std::string line;
            std::getline(is, line);
            {
                std::lock_guard<std::mutex> lock(mutex);
                sender.on_line(line, now_seconds());
            }
            read_next();
        });
    }

    void schedule_tick()
    {
        timer->expires_after(std::chrono::milliseconds(250));
        timer->async_wait([this](const boost::system::error_code &ec) {
            if (ec)
                return;
            {
                std::lock_guard<std::mutex> lock(mutex);
                sender.tick(now_seconds());
            }
            schedule_tick();
        });
    }
};

USBPrinterConnection::USBPrinterConnection() : p(std::make_unique<Priv>()) {}

USBPrinterConnection::~USBPrinterConnection() { this->close(); }

void USBPrinterConnection::open(const std::string &port_name, unsigned baud_rate)
{
    this->close();
    p->io.restart();
    p->port = std::make_unique<asio::serial_port>(p->io);
    boost::system::error_code ec;
    p->port->open(port_name, ec);
    if (ec)
        throw std::runtime_error("Cannot open " + port_name + ": " + ec.message());
    p->port->set_option(asio::serial_port_base::baud_rate(baud_rate), ec);
    if (ec)
        throw std::runtime_error("Cannot set the baud rate " + std::to_string(baud_rate) + ": " + ec.message());
    p->port->set_option(asio::serial_port_base::character_size(8));
    p->port->set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none));
    p->port->set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one));
    p->port->set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none));
    p->timer = std::make_unique<asio::steady_timer>(p->io);
    {
        std::lock_guard<std::mutex> lock(p->mutex);
        p->sender.connect(now_seconds());
    }
    p->read_next();
    p->schedule_tick();
    p->thread = std::thread([this]() { p->io.run(); });
}

void USBPrinterConnection::close()
{
    if (p->port) {
        boost::system::error_code ec;
        p->port->cancel(ec);
        p->port->close(ec);
    }
    if (p->timer)
        p->timer->cancel();
    p->io.stop();
    if (p->thread.joinable())
        p->thread.join();
    {
        std::lock_guard<std::mutex> lock(p->mutex);
        p->sender.disconnected();
    }
    p->timer.reset();
    p->port.reset();
    p->buffer.consume(p->buffer.size());
}

bool USBPrinterConnection::is_open() const
{
    return p->port && p->port->is_open();
}

void USBPrinterConnection::with_sender(const std::function<void(GCodeSender &, double)> &f)
{
    std::lock_guard<std::mutex> lock(p->mutex);
    f(p->sender, now_seconds());
}

} // namespace USB
} // namespace Slic3r
