///|/ Tisma Slicer: whether a network printer answers.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PrinterProbe.hpp"

#include <cstdlib>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace Slic3r {

static int default_port(PrintHostType type)
{
    switch (type) {
    case htBambuLan:   return 8883;
    case htFlashforge: return 8899;
    case htMKS:        return 8080;
    default:           return 80;
    }
}

std::pair<std::string, int> print_host_endpoint(const std::string &print_host, PrintHostType type)
{
    std::string s = boost::algorithm::trim_copy(print_host);
    bool https = false;
    if (const size_t scheme = s.find("://"); scheme != std::string::npos) {
        https = boost::algorithm::iequals(s.substr(0, scheme), "https");
        s = s.substr(scheme + 3);
    }
    // The path, then the user and password.
    if (const size_t slash = s.find('/'); slash != std::string::npos)
        s = s.substr(0, slash);
    if (const size_t at = s.rfind('@'); at != std::string::npos)
        s = s.substr(at + 1);
    std::string host = s;
    int         port = 0;
    if (! s.empty() && s.front() == '[') {
        // IPv6: [::1]:port
        const size_t close = s.find(']');
        host = s.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == ':')
            port = std::atoi(s.c_str() + close + 2);
    } else if (const size_t colon = s.find(':'); colon != std::string::npos && colon == s.rfind(':')) {
        host = s.substr(0, colon);
        port = std::atoi(s.c_str() + colon + 1);
    }
    if (port <= 0 || port > 65535)
        port = https ? 443 : default_port(type);
    return { host, port };
}

bool network_printer_answers(const std::string &host, int port, std::chrono::milliseconds timeout)
{
    if (host.empty())
        return false;
    namespace asio = boost::asio;
    try {
        asio::io_context        io;
        asio::ip::tcp::resolver resolver(io);
        asio::ip::tcp::socket   socket(io);
        bool                    connected = false;
        resolver.async_resolve(host, std::to_string(port),
            [&](const boost::system::error_code &ec, asio::ip::tcp::resolver::results_type endpoints) {
                if (! ec)
                    asio::async_connect(socket, endpoints,
                        [&](const boost::system::error_code &ec, const asio::ip::tcp::endpoint &) { connected = ! ec; });
            });
        io.run_for(timeout);
        return connected;
    } catch (const std::exception &) {
        return false;
    }
}

} // namespace Slic3r
