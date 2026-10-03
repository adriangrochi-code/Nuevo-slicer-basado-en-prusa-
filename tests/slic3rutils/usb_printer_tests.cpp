#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <set>
#include <string>
#include <vector>

#include "slic3r/Utils/USBPrinter.hpp"

using namespace Slic3r::USB;

namespace {

// Simulates the Marlin side of the host protocol.
class FakeMarlin : public Transport
{
public:
    std::deque<std::string>  responses;   // lines the "printer" will send back
    std::vector<std::string> executed;    // commands accepted, in order
    std::set<int>            corrupt_once; // line numbers whose first transmission is corrupted
    int                      last_line { 0 };
    size_t                   emergency { 0 };

    void write(const std::string &data) override
    {
        std::string line = data;
        if (! line.empty() && line.back() == '\n')
            line.pop_back();
        if (line == "M108") {
            ++ emergency;
            return;
        }
        REQUIRE(line[0] == 'N');
        const size_t star = line.rfind('*');
        REQUIRE(star != std::string::npos);
        const std::string body = line.substr(0, star);
        unsigned char cs = 0;
        for (char c : body)
            cs ^= (unsigned char)c;
        REQUIRE(std::stoi(line.substr(star + 1)) == int(cs));
        const size_t space = body.find(' ');
        const int n = std::stoi(body.substr(1, space - 1));
        const std::string cmd = body.substr(space + 1);
        if (cmd == "M110 N0") {
            last_line = 0;
            responses.push_back("ok");
            return;
        }
        if (corrupt_once.erase(n)) {
            responses.push_back("Error:checksum mismatch, Last Line: " + std::to_string(last_line));
            responses.push_back("Resend: " + std::to_string(last_line + 1));
            responses.push_back("ok");
            return;
        }
        if (n != last_line + 1) {
            responses.push_back("Error:Line Number is not Last Line Number+1, Last Line: " + std::to_string(last_line));
            responses.push_back("Resend: " + std::to_string(last_line + 1));
            responses.push_back("ok");
            return;
        }
        last_line = n;
        executed.push_back(cmd);
        if (cmd == "M105")
            responses.push_back("ok T:210.0 /215.0 B:60.1 /60.0 @:0 B@:0");
        else
            responses.push_back("ok");
    }
};

void pump(GCodeSender &sender, FakeMarlin &printer, double &now, size_t max_steps = 100000)
{
    for (size_t i = 0; i < max_steps && ! printer.responses.empty(); ++ i) {
        const std::string r = printer.responses.front();
        printer.responses.pop_front();
        now += 0.01;
        sender.on_line(r, now);
    }
}

std::vector<std::string> without(const std::vector<std::string> &cmds, const std::set<std::string> &skip)
{
    std::vector<std::string> out;
    for (const std::string &c : cmds)
        if (! skip.count(c))
            out.push_back(c);
    return out;
}

} // namespace

TEST_CASE("USB: G-code preparation and line format", "[USBPrinter]")
{
    const auto cmds = GCodeSender::prepare("; header\nG28 ; home\n\n  G1 X10 Y10  \nM104 S200\n");
    REQUIRE(cmds == std::vector<std::string>{ "G28", "G1 X10 Y10", "M104 S200" });
    // Reference value of the Marlin / OctoPrint checksum.
    REQUIRE(GCodeSender::numbered_line(0, "M110 N0") == "N0 M110 N0*125");
}

TEST_CASE("USB: a print is sent line by line, in order", "[USBPrinter]")
{
    FakeMarlin printer;
    GCodeSender sender(printer);
    double now = 0.;
    sender.connect(now);
    pump(sender, printer, now);
    REQUIRE(sender.state() == State::Idle);

    std::vector<std::string> file;
    for (int i = 0; i < 500; ++ i)
        file.push_back("G1 X" + std::to_string(i) + " E0.1");
    sender.start(file, now);
    pump(sender, printer, now);
    REQUIRE(sender.state() == State::Idle);
    REQUIRE(sender.sent() == file.size());
    REQUIRE(without(printer.executed, { "M155 S2", "M105" }) == file);
}

TEST_CASE("USB: corrupted lines are resent", "[USBPrinter]")
{
    FakeMarlin printer;
    printer.corrupt_once = { 5, 6, 100, 101, 102, 250 };
    GCodeSender sender(printer);
    double now = 0.;
    sender.connect(now);
    pump(sender, printer, now);
    std::vector<std::string> file;
    for (int i = 0; i < 300; ++ i)
        file.push_back("G1 X" + std::to_string(i));
    sender.start(file, now);
    pump(sender, printer, now);
    REQUIRE(sender.state() == State::Idle);
    // Every line executed exactly once, in order.
    REQUIRE(without(printer.executed, { "M155 S2", "M105" }) == file);
}

TEST_CASE("USB: pause, resume and cancel", "[USBPrinter]")
{
    FakeMarlin printer;
    GCodeSender sender(printer);
    double now = 0.;
    sender.connect(now);
    pump(sender, printer, now);
    std::vector<std::string> file(100, "G1 X1");
    sender.start(file, now);
    // Let 10 answers through, then pause.
    pump(sender, printer, now, 10);
    sender.pause();
    pump(sender, printer, now);
    const size_t at_pause = sender.sent();
    REQUIRE(sender.state() == State::Paused);
    REQUIRE(at_pause < file.size());
    sender.tick(now + 100.);
    pump(sender, printer, now);
    REQUIRE(sender.sent() == at_pause);

    sender.resume(now);
    pump(sender, printer, now, 5);
    sender.cancel(now);
    pump(sender, printer, now);
    REQUIRE(sender.state() == State::Idle);
    REQUIRE(printer.emergency == 1);
    const std::vector<std::string> tail(printer.executed.end() - 4, printer.executed.end());
    REQUIRE(tail == sender.cancel_commands);
}

TEST_CASE("USB: temperatures, lost ok and fatal errors", "[USBPrinter]")
{
    FakeMarlin printer;
    GCodeSender sender(printer);
    double now = 0.;
    sender.connect(now);
    pump(sender, printer, now);

    sender.on_line("T:205.3 /210.0 B:59.8 /60.0 @:127 B@:0", now);
    REQUIRE(*sender.temperatures().hotend == 205.3);
    REQUIRE(*sender.temperatures().bed_target == 60.0);

    // Lost "ok": after the timeout a numbered M105 recovers the communication.
    sender.start({ "G1 X1", "G1 X2", "G1 X3" }, now);
    printer.responses.clear();
    sender.tick(now + sender.response_timeout + 1.);
    pump(sender, printer, now);
    REQUIRE(sender.state() == State::Idle);
    REQUIRE(sender.sent() == 3);

    sender.start({ "G1 X1", "G1 X2" }, now);
    sender.on_line("Error:Printer halted. kill() called!", now);
    REQUIRE(sender.state() == State::Error);
}
