#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "slic3r/Utils/PrintHost.hpp"

using namespace Slic3r;

// Each host client against the fake hosts of tools/printhosts/fake_hosts.py, started on 18080 to 18085.
TEST_CASE("Print hosts connect to fake printers", "[.][PrintHostFake]")
{
    const std::pair<const char*, int> hosts[] = {
        {"octoprint", 18080}, {"prusalink", 18081}, {"astrobox", 18082}, {"moonraker", 18083}, {"repetier", 18084}, {"duet", 18085}};
    for (const auto& [type, port] : hosts) {
        // The options of a physical printer, with their defaults.
        DynamicPrintConfig cfg;
        for (const std::string& key : PhysicalPrinter::printer_options())
            cfg.set_key_value(key, print_config_def.get(key)->default_value->clone());
        cfg.set_deserialize_strict("host_type", type);
        cfg.set_deserialize_strict("print_host", "127.0.0.1:" + std::to_string(port));
        cfg.set_deserialize_strict("printhost_apikey", "secret");
        std::unique_ptr<PrintHost> host(PrintHost::get_print_host(&cfg));
        REQUIRE(host);
        wxString msg;
        const bool ok = host->test(msg);
        INFO(type << ": " << msg.ToUTF8().data());
        CHECK(ok);
    }
}
