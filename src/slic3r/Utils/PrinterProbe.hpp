///|/ Tisma Slicer: whether a network printer answers.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PrinterProbe_hpp_
#define slic3r_PrinterProbe_hpp_

#include <chrono>
#include <string>
#include <utility>

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

// The host and the port of the address of a print host ("http://192.168.1.5:7125/path" -> "192.168.1.5", 7125). The
// port is the one of the address, else 443 for https, else the port of the kind of host: 8883 for Bambu Lab (MQTT),
// 8899 for Flashforge, 8080 for MKS, 80 for the web servers. An empty host if the address has none.
std::pair<std::string, int> print_host_endpoint(const std::string &print_host, PrintHostType type);

// Whether something answers on the network at host:port: a TCP connection made within the timeout.
bool network_printer_answers(const std::string &host, int port, std::chrono::milliseconds timeout);

} // namespace Slic3r

#endif // slic3r_PrinterProbe_hpp_
