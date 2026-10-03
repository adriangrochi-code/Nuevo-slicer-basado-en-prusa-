#ifndef slic3r_Utils_USBPrinter_hpp_
#define slic3r_Utils_USBPrinter_hpp_

// Printing over USB (serial port) with the Marlin host protocol:
//  * every line is sent as "N<line> <command>*<checksum>",
//  * the next line is only sent after the printer answered "ok",
//  * "Resend: N" / "rs N" makes the host send the lines again from N,
//  * "busy:" keeps the connection alive during long commands,
//  * temperatures are read from "T:... B:..." reports (auto-report M155 or M105).
// Works with Marlin, Prusa firmware (MK2/MK3/MINI/MK4 over USB) and RepRapFirmware.

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Slic3r {
namespace USB {

enum class State
{
    Disconnected,
    Connecting, // waiting for the answer to M110
    Idle,       // connected, not printing
    Printing,
    Paused,
    Error       // the printer reported a fatal error (halted / killed)
};

const char* state_name(State state);

struct Temperatures
{
    std::optional<double> hotend, hotend_target;
    std::optional<double> bed, bed_target;
};

// Where the protocol engine writes to (the serial port, or a fake printer in tests).
class Transport
{
public:
    virtual ~Transport() = default;
    virtual void write(const std::string &data) = 0;
};

// Marlin host protocol, independent of the I/O. Not thread safe: the owner serializes the calls.
class GCodeSender
{
public:
    explicit GCodeSender(Transport &transport);

    // Strips comments and empty lines, returns the commands to send.
    static std::vector<std::string> prepare(const std::string &gcode);
    // "N<n> <cmd>*<checksum>"
    static std::string numbered_line(int line_no, const std::string &cmd);

    // (Re)starts the communication: resets the line numbers with M110.
    void connect(double now);
    // The port was closed.
    void disconnected();
    void start(std::vector<std::string> commands, double now);
    void pause();
    void resume(double now);
    // Stops the print: interrupts heating (M108) and sends the cancel commands.
    void cancel(double now);
    // A command typed by the user, sent as soon as possible.
    void send_manual(const std::string &cmd, double now);

    // A line received from the printer.
    void on_line(const std::string &line, double now);
    // Periodic call: timeouts and temperature polling.
    void tick(double now);

    State        state() const { return m_state; }
    size_t       sent() const { return m_file_pos; }
    size_t       total() const { return m_file.size(); }
    Temperatures temperatures() const { return m_temperatures; }
    // Messages for the console, taken out of the sender.
    std::vector<std::string> take_log();

    // Commands sent after a cancelled print.
    std::vector<std::string> cancel_commands { "M104 S0", "M140 S0", "M107", "M84" };
    // Without any answer for this long, M105 is sent to provoke one (lost "ok").
    double       response_timeout { 15. };
    // Temperature polling period when the printer does not auto-report.
    double       poll_period { 5. };

private:
    void send_next(double now);
    void send_numbered(const std::string &cmd, double now);
    void parse_temperatures(const std::string &line);
    void log(std::string msg);

    Transport                &m_transport;
    State                     m_state { State::Disconnected };
    std::vector<std::string>  m_file;
    size_t                    m_file_pos { 0 };
    std::deque<std::string>   m_manual;
    std::deque<std::pair<int, std::string>> m_history;   // recently sent lines, for resends
    int                       m_next_line_no { 1 };
    std::optional<int>        m_resend_from;
    int                       m_in_flight { 0 };          // lines sent and not yet acknowledged
    double                    m_last_activity { 0. };
    double                    m_last_poll { 0. };
    double                    m_last_report { -1e10 };     // last temperature report
    bool                      m_finishing { false };       // cancel commands in progress
    Temperatures              m_temperatures;
    std::vector<std::string>  m_log;
};

// GCodeSender over a real serial port, with its own reading thread. Thread safe.
class USBPrinterConnection
{
public:
    USBPrinterConnection();
    ~USBPrinterConnection();

    // Throws std::runtime_error if the port cannot be opened.
    void open(const std::string &port, unsigned baud_rate);
    void close();
    bool is_open() const;

    // Runs f with the sender locked (and the current time in seconds).
    void with_sender(const std::function<void(GCodeSender &, double now)> &f);

private:
    struct Priv;
    std::unique_ptr<Priv> p;
};

// Seconds since an arbitrary epoch (steady clock).
double now_seconds();

} // namespace USB
} // namespace Slic3r

#endif // slic3r_Utils_USBPrinter_hpp_
