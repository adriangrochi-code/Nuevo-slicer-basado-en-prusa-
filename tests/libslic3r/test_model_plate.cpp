#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/ModelPlate.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem/operations.hpp>

using namespace Slic3r;

TEST_CASE("Plate overrides reach the print config", "[ModelPlate]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.option<ConfigOptionInts>("bed_temperature")->values = { 60, 60 };
    config.option<ConfigOptionInts>("first_layer_bed_temperature")->values = { 65, 65 };

    SECTION("a default plate changes nothing") {
        DynamicPrintConfig copy = config;
        ModelPlate{}.apply_to(copy);
        CHECK(copy == config);
    }
    SECTION("every override is applied") {
        ModelPlate plate;
        plate.print_sequence = ModelPlate::Sequence::ByObject;
        plate.spiral_vase = ModelPlate::Toggle::On;
        plate.bed_temperature = 100;
        plate.first_layer_bed_temperature = 105;
        REQUIRE(plate.has_overrides());
        plate.apply_to(config);
        CHECK(config.opt_bool("complete_objects"));
        CHECK(config.opt_bool("spiral_vase"));
        CHECK(config.option<ConfigOptionInts>("bed_temperature")->values == std::vector<int>{ 100, 100 });
        CHECK(config.option<ConfigOptionInts>("first_layer_bed_temperature")->values == std::vector<int>{ 105, 105 });
    }
    SECTION("by layer and vase off") {
        config.set_key_value("complete_objects", new ConfigOptionBool(true));
        config.set_key_value("spiral_vase", new ConfigOptionBool(true));
        ModelPlate plate;
        plate.print_sequence = ModelPlate::Sequence::ByLayer;
        plate.spiral_vase = ModelPlate::Toggle::Off;
        plate.apply_to(config);
        CHECK(!config.opt_bool("complete_objects"));
        CHECK(!config.opt_bool("spiral_vase"));
    }
}

TEST_CASE("Plate settings round trip", "[ModelPlate]")
{
    std::vector<ModelPlate> plates(MAX_NUMBER_OF_BEDS);
    plates[0].name = "Carcasa <A> & \"B\"";
    plates[0].locked = true;
    plates[2].print_sequence = ModelPlate::Sequence::ByObject;
    plates[2].bed_temperature = 90;

    SECTION("xml") {
        const std::string xml = plates_to_xml(plates, 3);
        REQUIRE(!xml.empty());
        std::vector<ModelPlate> read(MAX_NUMBER_OF_BEDS);
        REQUIRE(plates_from_xml(xml, read));
        CHECK(read == plates);
        // Plates beyond the number of beds are not stored.
        CHECK(plates_to_xml(plates, 2).find("bed_idx=\"2\"") == std::string::npos);
        CHECK(plates_to_xml(std::vector<ModelPlate>(MAX_NUMBER_OF_BEDS), 9).empty());
        CHECK(!plates_from_xml("<plate", read));
    }

    SECTION("3mf project") {
        Model model;
        model.add_object("cube", "", TriangleMesh(its_make_cube(10., 10., 10.)));
        model.add_default_instances();
        model.plates = plates;
        const std::string path = (boost::filesystem::temp_directory_path() / "tisma_plates.3mf").string();
        REQUIRE(store_3mf(path.c_str(), &model, nullptr, false));

        Model loaded;
        DynamicPrintConfig config;
        ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
        boost::optional<Semver> version;
        REQUIRE(load_3mf(path.c_str(), config, ctxt, &loaded, false, version));
        boost::filesystem::remove(path);
        // Only the plates of the existing beds (one here) are stored.
        CHECK(loaded.plate(0) == plates[0]);
        CHECK(loaded.plate(2).is_default());
    }
}

TEST_CASE("Plate G-code file names", "[ModelPlate]")
{
    std::vector<ModelPlate> plates(MAX_NUMBER_OF_BEDS);
    plates[1].name = "Tapa: v2/final. ";
    CHECK(plate_file_suffix(plates, 0) == "_bed1");
    CHECK(plate_file_suffix(plates, 1) == "_Tapa_ v2_final");
}
