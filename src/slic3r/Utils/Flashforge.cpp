///|/ Copyright (c) Tisma Slicer contributors
///|/ Based on the Flashforge print host of OrcaSlicer (src/slic3r/Utils/Flashforge.cpp, AGPLv3).
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#include "Flashforge.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <thread>

#include <boost/algorithm/string.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "Http.hpp"
#include "TCPConsole.hpp"

namespace fs = boost::filesystem;
using json = nlohmann::json;

namespace Slic3r {

namespace FlashforgeProtocol {

const std::vector<unsigned char>& discovery_message()
{
    // The probe the Flashforge slicers broadcast ("www.usr" and a fixed tail), as in OrcaSlicer.
    static const std::vector<unsigned char> message = {
        0x77, 0x77, 0x77, 0x2e, 0x75, 0x73, 0x72, 0x22, 0x65, 0x36, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    return message;
}

static std::string fixed_text(const unsigned char *data, size_t size)
{
    std::string out(reinterpret_cast<const char*>(data), size);
    if (const size_t end = out.find('\0'); end != std::string::npos)
        out.resize(end);
    boost::trim(out);
    return out;
}

bool parse_discovery_reply(const std::vector<unsigned char>& reply, const std::string& ip_address, FlashforgeDiscoveredPrinter& printer)
{
    if (reply.size() < 0xC4)
        return false;
    printer.name          = fixed_text(reply.data(), 32);
    printer.serial_number = fixed_text(reply.data() + 0x92, 32);
    printer.ip_address    = ip_address;
    return !printer.name.empty() || !printer.serial_number.empty();
}

// Firmware sends numbers as numbers, strings or booleans.
static bool json_int(const json& value, int& out)
{
    if (value.is_number_integer() || value.is_number_unsigned()) {
        out = value.get<int>();
        return true;
    }
    if (value.is_boolean()) {
        out = value.get<bool>() ? 1 : 0;
        return true;
    }
    if (value.is_string()) {
        std::string text = boost::trim_copy(value.get<std::string>());
        try {
            size_t end = 0;
            out = std::stoi(text, &end);
            return !text.empty() && end == text.size();
        } catch (...) {
        }
    }
    return false;
}

bool check_local_api_reply(const std::string& body, std::string& error)
{
    const json reply = json::parse(body, nullptr, false);
    if (reply.is_discarded() || !reply.is_object()) {
        error = "invalid reply";
        return false;
    }
    int code = 0;
    bool has_code = reply.contains("code") && json_int(reply["code"], code);
    if (!has_code && reply.contains("err"))
        has_code = json_int(reply["err"], code);
    if (has_code && code != 0) {
        for (const char* key : { "message", "msg" })
            if (reply.contains(key) && reply[key].is_string()) {
                error = (boost::format("%1% (%2%)") % reply[key].get<std::string>() % code).str();
                return false;
            }
        error = (boost::format("error %1%") % code).str();
        return false;
    }
    return true;
}

std::string printer_file_name(const std::string& path, const std::string& fallback_extension)
{
    std::string name = fs::path(path).filename().string();
    if (name.empty())
        name = "print" + fallback_extension;
    for (char& c : name)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-'))
            c = '_';
    return name;
}

std::string host_name(const std::string& print_host)
{
    std::string host = boost::trim_copy(print_host);
    if (const size_t scheme = host.find("://"); scheme != std::string::npos)
        host.erase(0, scheme + 3);
    if (const size_t end = host.find_first_of(":/"); end != std::string::npos)
        host.erase(end);
    return host;
}

} // namespace FlashforgeProtocol

namespace {

// Broadcast addresses: everyone, the usual home networks and the networks of this computer.
std::vector<std::string> broadcast_addresses()
{
    std::set<std::string> addresses = { "255.255.255.255", "192.168.0.255", "192.168.1.255" };
    try {
        boost::asio::io_context        io_context;
        boost::asio::ip::tcp::resolver resolver(io_context);
        boost::system::error_code      ec;
        const std::string              host = boost::asio::ip::host_name(ec);
        if (!ec)
            for (const auto& entry : resolver.resolve(boost::asio::ip::tcp::v4(), host, "", ec)) {
                const auto address = entry.endpoint().address();
                if (!address.is_v4() || address.to_v4().to_bytes()[0] == 127)
                    continue;
                const auto b = address.to_v4().to_bytes();
                addresses.insert((boost::format("%1%.%2%.%3%.255") % int(b[0]) % int(b[1]) % int(b[2])).str());
            }
    } catch (...) {
    }
    return { addresses.begin(), addresses.end() };
}

std::string option_string(const DynamicPrintConfig* config, const char* key)
{
    const auto* opt = config ? config->option<ConfigOptionString>(key) : nullptr;
    return opt ? boost::trim_copy(opt->value) : std::string();
}

} // namespace

Flashforge::Flashforge(DynamicPrintConfig* config)
    : m_host(FlashforgeProtocol::host_name(option_string(config, "print_host")))
    , m_serial_number(option_string(config, "flashforge_serial_number"))
    , m_check_code(option_string(config, "printhost_apikey"))
{
}

bool Flashforge::discover(std::vector<FlashforgeDiscoveredPrinter>& printers, wxString& msg, int timeout_ms)
{
    namespace ip = boost::asio::ip;
    printers.clear();
    try {
        boost::asio::io_context   io_context;
        ip::udp::socket           socket(io_context);
        boost::system::error_code ec;
        socket.open(ip::udp::v4(), ec);
        if (!ec) socket.set_option(boost::asio::socket_base::broadcast(true), ec);
        if (!ec) socket.set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (!ec) socket.bind({ ip::udp::v4(), FlashforgeProtocol::discovery_reply_port }, ec);
        if (!ec) socket.non_blocking(true, ec);
        if (ec) {
            msg = GUI::from_u8(ec.message());
            return false;
        }
        for (const std::string& address : broadcast_addresses()) {
            socket.send_to(boost::asio::buffer(FlashforgeProtocol::discovery_message()),
                           { ip::make_address_v4(address, ec), FlashforgeProtocol::discovery_port }, 0, ec);
            ec.clear();
        }
        // Answers until the time is up, or a moment after the last one.
        std::map<std::string, FlashforgeDiscoveredPrinter> by_address;
        const auto start = std::chrono::steady_clock::now();
        auto       last  = start;
        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            if (now - start > std::chrono::milliseconds(timeout_ms) ||
                (!by_address.empty() && now - last > std::chrono::milliseconds(1500)))
                break;
            std::vector<unsigned char> reply(512);
            ip::udp::endpoint          from;
            const size_t               size = socket.receive_from(boost::asio::buffer(reply), from, 0, ec);
            if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
                ec.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if (ec) {
                msg = GUI::from_u8(ec.message());
                return false;
            }
            reply.resize(size);
            FlashforgeDiscoveredPrinter printer;
            if (FlashforgeProtocol::parse_discovery_reply(reply, from.address().to_string(), printer)) {
                by_address[printer.ip_address] = std::move(printer);
                last = std::chrono::steady_clock::now();
            }
        }
        for (auto& [address, printer] : by_address)
            printers.emplace_back(std::move(printer));
    } catch (const std::exception& e) {
        msg = GUI::from_u8(e.what());
        return false;
    }
    if (printers.empty()) {
        msg = _L("No Flashforge printer answered on the local network.");
        return false;
    }
    return true;
}

bool Flashforge::test(wxString& msg) const
{
    return use_local_api() ? test_local_api(msg) : test_console(msg);
}

wxString Flashforge::get_test_ok_msg() const
{
    return use_local_api() ? _L("Connection to the Flashforge printer (local API) works correctly.")
                           : _L("Connection to the Flashforge printer (G-code console) works correctly.");
}

wxString Flashforge::get_test_failed_msg(wxString& msg) const
{
    return GUI::format_wxstr("%s: %s\n\n%s", _L("Could not connect to the Flashforge printer"), msg,
                             use_local_api() ?
                                 _L("Check the IP address, the serial number and the access code: turn on LAN mode on the "
                                    "printer (Settings > Network) to see the code.") :
                                 _L("Adventurer 5M, 5M Pro and AD5X: fill in the serial number and the access code of LAN "
                                    "mode to use the local API."));
}

bool Flashforge::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn) const
{
    return use_local_api() ? upload_local_api(upload_data, std::move(progress_fn), std::move(error_fn))
                           : upload_console(upload_data, std::move(progress_fn), std::move(error_fn));
}

std::string Flashforge::local_api_url(const std::string& path) const
{
    return "http://" + m_host + ":8898/" + path;
}

bool Flashforge::test_local_api(wxString& msg) const
{
    bool ok = true;
    Http::post(local_api_url("detail"))
        .header("Content-Type", "application/json")
        .set_post_body(json{ { "serialNumber", m_serial_number }, { "checkCode", m_check_code } }.dump())
        .on_complete([&](std::string body, unsigned) {
            std::string error;
            if (!FlashforgeProtocol::check_local_api_reply(body, error)) {
                BOOST_LOG_TRIVIAL(error) << "Flashforge: detail rejected: " << body;
                msg = GUI::from_u8(error);
                ok = false;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Flashforge: detail failed: %1%, HTTP %2%, body: `%3%`") % error % status % body;
            msg = format_error(body, error, status);
            ok = false;
        })
        .perform_sync();
    return ok;
}

bool Flashforge::upload_local_api(const PrintHostUpload& upload_data, ProgressFn progress_fn, ErrorFn error_fn) const
{
    const std::string name = FlashforgeProtocol::printer_file_name(upload_data.upload_path.string(), ".gcode");
    std::string size = "0";
    try {
        size = std::to_string(fs::file_size(upload_data.source_path));
    } catch (...) {
    }
    // No material station mapping (AD5X): an empty list, base64 encoded as the printer expects it.
    const std::string mappings = "[]";
    std::string mappings_b64(boost::beast::detail::base64::encoded_size(mappings.size()), '\0');
    mappings_b64.resize(boost::beast::detail::base64::encode(mappings_b64.data(), mappings.data(), mappings.size()));

    bool ok = true;
    Http::post(local_api_url("uploadGcode"))
        .header("serialNumber", m_serial_number)
        .header("checkCode", m_check_code)
        .header("fileSize", size)
        .header("printNow", upload_data.post_action == PrintHostPostUploadAction::StartPrint ? "true" : "false")
        .header("levelingBeforePrint", "false")
        .header("flowCalibration", "false")
        .header("firstLayerInspection", "false")
        .header("timeLapseVideo", "false")
        .header("useMatlStation", "false")
        .header("gcodeToolCnt", "")
        .header("materialMappings", mappings_b64)
        .form_add_file("gcodeFile", upload_data.source_path, name)
        .on_complete([&](std::string body, unsigned) {
            std::string error;
            if (!FlashforgeProtocol::check_local_api_reply(body, error)) {
                BOOST_LOG_TRIVIAL(error) << "Flashforge: upload rejected: " << body;
                error_fn(GUI::from_u8(error));
                ok = false;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Flashforge: upload failed: %1%, HTTP %2%, body: `%3%`") % error % status % body;
            error_fn(format_error(body, error, status));
            ok = false;
        })
        .on_progress([&](Http::Progress progress, bool& cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel)
                ok = false;
        })
        .perform_sync();
    return ok;
}

// The G-code console of the older printers: "~"-prefixed commands, each answered by lines ending in "ok".
static void console_setup(Utils::TCPConsole& console)
{
    console.set_command_done_string("ok");
}

bool Flashforge::test_console(wxString& msg) const
{
    Utils::TCPConsole console(m_host, "8899");
    console_setup(console);
    console.enqueue_cmd("~M601 S1\r"); // take control of the printer
    if (!console.run_queue()) {
        msg = GUI::from_u8(console.error_message());
        return false;
    }
    return true;
}

bool Flashforge::upload_console(const PrintHostUpload& upload_data, ProgressFn progress_fn, ErrorFn error_fn) const
{
    std::string data;
    {
        std::ifstream file(upload_data.source_path.string(), std::ios::binary);
        if (!file) {
            error_fn(_L("Could not read the G-code file."));
            return false;
        }
        data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    const std::string name = FlashforgeProtocol::printer_file_name(upload_data.upload_path.string(), ".gcode");

    Utils::TCPConsole console(m_host, "8899");
    console_setup(console);
    console.enqueue_cmd("~M601 S1\r");
    console.enqueue_cmd("~M115\r");
    console.enqueue_cmd("~M650\r");
    console.enqueue_cmd("~M119\r");
    // The file: its size and name, then the bytes in 4 KiB parts.
    console.enqueue_cmd((boost::format("~M28 %1% 0:/user/%2%\r") % data.size() % name).str());
    for (size_t pos = 0; pos < data.size(); pos += 4096)
        console.enqueue_data(data.substr(pos, 4096));
    const std::string no_buffer;
    bool cancel = false;
    progress_fn(Http::Progress(0, 0, data.size(), 0, no_buffer), cancel);
    if (cancel || !console.run_queue()) {
        if (!cancel)
            error_fn(GUI::from_u8(console.error_message()));
        return false;
    }
    progress_fn(Http::Progress(0, 0, data.size(), data.size(), no_buffer), cancel);

    // The printer needs a moment to store the file before saving it and starting the print.
    Utils::TCPConsole finish(m_host, "8899");
    console_setup(finish);
    finish.set_queue_delay(std::chrono::milliseconds(3000));
    finish.enqueue_cmd("~M29\r");
    if (upload_data.post_action == PrintHostPostUploadAction::StartPrint)
        finish.enqueue_cmd((boost::format("~M23 0:/user/%1%\r") % name).str());
    if (!finish.run_queue()) {
        error_fn(GUI::from_u8(finish.error_message()));
        return false;
    }
    return true;
}

} // namespace Slic3r
