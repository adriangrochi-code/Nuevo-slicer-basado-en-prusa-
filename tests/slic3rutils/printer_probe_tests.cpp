#include <catch2/catch_test_macros.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "slic3r/Utils/PrinterProbe.hpp"

using namespace Slic3r;

// Tisma: the probe of the network printer of the printer in use (the main button sends to it when it answers).

TEST_CASE("Host and port of the address of a print host", "[PrinterProbe]")
{
    using P = std::pair<std::string, int>;
    CHECK(print_host_endpoint("192.168.1.5", htOctoPrint) == P("192.168.1.5", 80));
    CHECK(print_host_endpoint("http://octopi.local/", htOctoPrint) == P("octopi.local", 80));
    CHECK(print_host_endpoint("https://printer.example.com/api", htOctoPrint) == P("printer.example.com", 443));
    CHECK(print_host_endpoint("http://192.168.1.5:7125", htMoonraker) == P("192.168.1.5", 7125));
    CHECK(print_host_endpoint(" user:pass@10.0.0.2:8080/path ", htRepetier) == P("10.0.0.2", 8080));
    CHECK(print_host_endpoint("[fe80::1]:5000", htDuet) == P("fe80::1", 5000));
    // The ports of the kinds of hosts without a web server.
    CHECK(print_host_endpoint("192.168.1.20", htBambuLan) == P("192.168.1.20", 8883));
    CHECK(print_host_endpoint("192.168.1.21", htFlashforge) == P("192.168.1.21", 8899));
    CHECK(print_host_endpoint("192.168.1.22", htMKS) == P("192.168.1.22", 8080));
    CHECK(print_host_endpoint("", htOctoPrint).first.empty());
}

TEST_CASE("A printer answers when it accepts a connection", "[PrinterProbe]")
{
    namespace asio = boost::asio;
    asio::io_context        io;
    asio::ip::tcp::acceptor acceptor(io, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    const int port = acceptor.local_endpoint().port();
    // The connection is made by the system (listen backlog), nothing has to accept it.
    CHECK(network_printer_answers("127.0.0.1", port, std::chrono::milliseconds(2000)));
    acceptor.close();
    CHECK(! network_printer_answers("127.0.0.1", port, std::chrono::milliseconds(2000)));
    CHECK(! network_printer_answers("", 80, std::chrono::milliseconds(100)));
}
