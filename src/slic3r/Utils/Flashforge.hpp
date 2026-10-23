///|/ Copyright (c) Tisma Slicer contributors
///|/ Based on the Flashforge print host of OrcaSlicer (src/slic3r/Utils/Flashforge.cpp, AGPLv3).
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Flashforge_hpp_
#define slic3r_Flashforge_hpp_

#include <string>
#include <vector>

#include "PrintHost.hpp"

namespace Slic3r {

class DynamicPrintConfig;

// A Flashforge printer answering the discovery broadcast of the local network.
struct FlashforgeDiscoveredPrinter
{
    std::string name;
    std::string serial_number;
    std::string ip_address;
};

// The parts of the Flashforge protocols that do not need the network, kept apart for the tests.
namespace FlashforgeProtocol {
    // UDP broadcast asking the printers of the network to answer, to this port; they answer to discovery_reply_port.
    constexpr unsigned short discovery_port       = 48899;
    constexpr unsigned short discovery_reply_port = 18007;
    const std::vector<unsigned char>& discovery_message();
    // Name (at 0) and serial number (at 0x92) of a discovery answer; false if it is not one.
    bool parse_discovery_reply(const std::vector<unsigned char>& reply, const std::string& ip_address, FlashforgeDiscoveredPrinter& printer);
    // A reply of the local API: true when it reports success, else false and the message of the printer.
    bool check_local_api_reply(const std::string& body, std::string& error);
    // File name on the printer: the base name, with only letters, digits, '.', '_' and '-'.
    std::string printer_file_name(const std::string& path, const std::string& fallback_extension);
    // Host name or address of "print_host" (which may be a URL), without the port.
    std::string host_name(const std::string& print_host);
} // namespace FlashforgeProtocol

// Print host for Flashforge printers. With the serial number and the access code of the LAN mode (Adventurer 5M,
// 5M Pro, AD5X) it uses the local HTTP API of the printer (port 8898); without them, the G-code console of the older
// printers (TCP port 8899).
class Flashforge : public PrintHost
{
public:
    explicit Flashforge(DynamicPrintConfig *config);
    ~Flashforge() override = default;

    const char *get_name() const override { return "Flashforge"; }
    bool        test(wxString &msg) const override;
    wxString    get_test_ok_msg() const override;
    wxString    get_test_failed_msg(wxString &msg) const override;
    bool        upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool        has_auto_discovery() const override { return true; }
    bool        can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }
    std::string get_host() const override { return m_host; }

    // Asks the printers of the local network to answer (UDP broadcast). False with a message when none answered.
    static bool discover(std::vector<FlashforgeDiscoveredPrinter> &printers, wxString &msg, int timeout_ms = 4000);

private:
    bool use_local_api() const { return !m_serial_number.empty() && !m_check_code.empty(); }
    std::string local_api_url(const std::string &path) const;
    bool test_local_api(wxString &msg) const;
    bool upload_local_api(const PrintHostUpload &upload_data, ProgressFn progress_fn, ErrorFn error_fn) const;
    bool test_console(wxString &msg) const;
    bool upload_console(const PrintHostUpload &upload_data, ProgressFn progress_fn, ErrorFn error_fn) const;

    std::string m_host;
    std::string m_serial_number;
    std::string m_check_code;
};

} // namespace Slic3r

#endif
