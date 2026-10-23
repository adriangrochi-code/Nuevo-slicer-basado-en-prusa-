#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <memory>

#include <boost/filesystem.hpp>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/Flashforge.hpp"

using namespace Slic3r;
namespace fs = boost::filesystem;

// The physical printer dialog shows the host types of the "host_type" option and takes the value of each as its position
// in that list: an enum value out of order left the choice empty (Flashforge after the unlisted PrusaConnectNew).
TEST_CASE("Host types are listed in the order of their values", "[Flashforge]")
{
    const ConfigOptionDef *def = print_config_def.get("host_type");
    REQUIRE(def != nullptr);
    const auto &keys = ConfigOptionEnum<PrintHostType>::get_enum_values();
    for (size_t i = 0; i < def->enum_def->values().size(); ++i) {
        INFO(def->enum_def->value(i));
        CHECK(keys.at(def->enum_def->value(i)) == int(i));
    }
    CHECK(keys.at("flashforge") == int(htFlashforge));
}

TEST_CASE("Flashforge discovery reply", "[Flashforge]")
{
    std::vector<unsigned char> reply(0xC4, 0);
    const std::string name = "Adventurer 5M Pro", serial = "SNMOMC9900001";
    std::copy(name.begin(), name.end(), reply.begin());
    std::copy(serial.begin(), serial.end(), reply.begin() + 0x92);
    FlashforgeDiscoveredPrinter printer;
    REQUIRE(FlashforgeProtocol::parse_discovery_reply(reply, "192.168.1.50", printer));
    CHECK(printer.name == name);
    CHECK(printer.serial_number == serial);
    CHECK(printer.ip_address == "192.168.1.50");
    reply.resize(0xC3);
    CHECK(!FlashforgeProtocol::parse_discovery_reply(reply, "192.168.1.50", printer));
    CHECK(FlashforgeProtocol::discovery_message().size() == 20);
}

TEST_CASE("Flashforge local API replies", "[Flashforge]")
{
    std::string error;
    CHECK(FlashforgeProtocol::check_local_api_reply(R"({"code":0,"message":"Success"})", error));
    CHECK(FlashforgeProtocol::check_local_api_reply(R"({"code":"0"})", error));
    CHECK(!FlashforgeProtocol::check_local_api_reply(R"({"code":1,"message":"Check code error"})", error));
    CHECK(error == "Check code error (1)");
    CHECK(!FlashforgeProtocol::check_local_api_reply(R"({"err":2,"msg":"busy"})", error));
    CHECK(error == "busy (2)");
    CHECK(!FlashforgeProtocol::check_local_api_reply("<html>", error));
}

TEST_CASE("Flashforge file and host names", "[Flashforge]")
{
    CHECK(FlashforgeProtocol::printer_file_name("/tmp/my part (2).gcode", ".gcode") == "my_part__2_.gcode");
    CHECK(FlashforgeProtocol::printer_file_name("", ".gcode") == "print.gcode");
    CHECK(FlashforgeProtocol::host_name("192.168.1.50") == "192.168.1.50");
    CHECK(FlashforgeProtocol::host_name("http://192.168.1.50:8898/") == "192.168.1.50");
    CHECK(FlashforgeProtocol::host_name(" ad5m.local ") == "ad5m.local");
}

// With tools/flashforge/fake_printer.py --dir <dir> running and TISMA_FLASHFORGE_FAKE=<dir>.
TEST_CASE("Flashforge against the fake printer", "[.][FlashforgeFake]")
{
    const char *dir = std::getenv("TISMA_FLASHFORGE_FAKE");
    REQUIRE(dir != nullptr);
    DynamicPrintConfig config;
    for (const std::string &key : PhysicalPrinter::printer_options())
        config.set_key_value(key, print_config_def.get(key)->default_value->clone());
    config.set_deserialize_strict("host_type", "flashforge");
    config.set_deserialize_strict("print_host", "127.0.0.1");

    const fs::path gcode = fs::temp_directory_path() / "tisma flashforge.gcode";
    {
        std::ofstream out(gcode.string());
        for (int i = 0; i < 5000; ++i)
            out << "G1 X" << i % 200 << " Y10 E0.1\n";
    }
    PrintHostUpload upload;
    upload.source_path = gcode;
    upload.upload_path = "tisma flashforge.gcode";
    upload.post_action = PrintHostPostUploadAction::StartPrint;
    const fs::path stored  = fs::path(dir) / "files" / "tisma_flashforge.gcode";
    const fs::path started = fs::path(dir) / "started";
    auto progress = [](Http::Progress, bool &) {};
    wxString error;
    auto on_error = [&error](wxString msg) { error = msg; };

    SECTION("discovery") {
        std::vector<FlashforgeDiscoveredPrinter> printers;
        wxString msg;
        REQUIRE(Flashforge::discover(printers, msg, 3000));
        CHECK(printers.front().serial_number == "SNMOMC9900001");
    }

    SECTION("local API") {
        config.set_deserialize_strict("flashforge_serial_number", "SNMOMC9900001");
        config.set_deserialize_strict("printhost_apikey", "a1b2c3d4");
        std::unique_ptr<PrintHost> host(PrintHost::get_print_host(&config));
        wxString msg;
        REQUIRE(host->test(msg));
        fs::remove(stored);
        fs::remove(started);
        REQUIRE(host->upload(upload, progress, on_error, {}));
        CHECK(fs::file_size(stored) == fs::file_size(gcode));
        CHECK(fs::exists(started));

        config.set_deserialize_strict("printhost_apikey", "wrong");
        std::unique_ptr<PrintHost> bad(PrintHost::get_print_host(&config));
        CHECK(!bad->test(msg));
        CHECK(msg.ToStdString().find("check code") != std::string::npos);
    }

    SECTION("G-code console") {
        std::unique_ptr<PrintHost> host(PrintHost::get_print_host(&config));
        wxString msg;
        REQUIRE(host->test(msg));
        fs::remove(stored);
        fs::remove(started);
        INFO(error.ToStdString());
        REQUIRE(host->upload(upload, progress, on_error, {}));
        CHECK(fs::file_size(stored) == fs::file_size(gcode));
        CHECK(fs::exists(started));
    }
    fs::remove(gcode);
}
