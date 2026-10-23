#include <catch2/catch_test_macros.hpp>

#include <string>

#include "test_data.hpp"
#include "libslic3r/BedType.hpp"

using namespace Slic3r;

// Tisma: the bed types of OrcaSlicer (BedType.hpp).

static DynamicPrintConfig bed_type_config(bool supported, const std::string &bed)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "bed_temperature", "60" },
        { "first_layer_bed_temperature", "65" },
        { "hot_plate_temp", "70" },
        { "hot_plate_temp_initial_layer", "75" },
        { "cool_plate_temp", "35" },
        { "cool_plate_temp_initial_layer", "0" },
        { "support_multi_bed_types", supported ? "1" : "0" },
        { "curr_bed_type", bed },
    });
    return config;
}

TEST_CASE("Bed type names of OrcaSlicer", "[BedType]")
{
    CHECK(normalize_bed_type("Textured PEI Plate") == "Textured PEI Plate");
    CHECK(normalize_bed_type("SuperTack Plate") == "Supertack Plate");
    // The index of the enum of OrcaSlicer, 0 being its default plate.
    CHECK(normalize_bed_type("1") == "Cool Plate");
    CHECK(normalize_bed_type("4") == "Textured PEI Plate");
    CHECK(normalize_bed_type("0").empty());
    CHECK(normalize_bed_type("Glass").empty());
}

TEST_CASE("Bed temperatures of the bed chosen", "[BedType]")
{
    SECTION("the temperatures of the plate") {
        DynamicPrintConfig config = bed_type_config(true, "High Temp Plate");
        CHECK(apply_bed_type(config));
        CHECK(config.opt_int("bed_temperature", 0) == 70);
        CHECK(config.opt_int("first_layer_bed_temperature", 0) == 75);
    }
    SECTION("0 keeps the bed temperature of the filament") {
        DynamicPrintConfig config = bed_type_config(true, "Cool Plate");
        CHECK(apply_bed_type(config));
        CHECK(config.opt_int("bed_temperature", 0) == 35);
        CHECK(config.opt_int("first_layer_bed_temperature", 0) == 65);
    }
    SECTION("a printer without bed types keeps the bed temperatures") {
        DynamicPrintConfig config = bed_type_config(false, "High Temp Plate");
        CHECK(! apply_bed_type(config));
        CHECK(config.opt_int("bed_temperature", 0) == 60);
    }
    SECTION("no plate chosen: the default bed of the printer") {
        DynamicPrintConfig config = bed_type_config(true, "");
        config.set_deserialize_strict({ { "default_bed_type", "High Temp Plate" } });
        CHECK(apply_bed_type(config));
        CHECK(config.opt_string("curr_bed_type") == "High Temp Plate");
        CHECK(config.opt_int("bed_temperature", 0) == 70);
    }
}

TEST_CASE("G-code heats the bed chosen", "[BedType]")
{
    auto gcode = [](const std::string &bed) {
        DynamicPrintConfig config = bed_type_config(true, bed);
        return Test::slice({ Test::TestMesh::cube_20x20x20 }, config);
    };
    const std::string hot = gcode("High Temp Plate");
    CHECK(Test::contains(hot, "M190 S75"));
    CHECK(Test::contains(hot, "M140 S70"));
    const std::string cool = gcode("Cool Plate");
    CHECK(Test::contains(cool, "M190 S65"));
    CHECK(Test::contains(cool, "M140 S35"));
}
