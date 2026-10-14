#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/BambuProject.hpp"

using namespace Slic3r;
using Catch::Approx;

// tests/data/test_3mf/bambu_project.3mf, written by make_bambu_project.py as Bambu Studio / OrcaSlicer lay out their
// projects: a 20 mm box with a 10 mm modifier, a 15 mm box with two instances, two plates of 256 x 256 mm.
static std::string bambu_project() { return std::string(TEST_DATA_DIR) + "/test_3mf/bambu_project.3mf"; }

TEST_CASE("Projects of Bambu Studio / OrcaSlicer are recognized", "[3mf][Bambu]")
{
    CHECK(is_bambu_project_3mf(bambu_project()));
    CHECK(is_project_3mf(bambu_project()).first);
    CHECK_FALSE(is_bambu_project_3mf(std::string(TEST_DATA_DIR) + "/test_3mf/Geräte/Büchse.3mf"));
}

TEST_CASE("Objects, parts and plates of a Bambu Studio / OrcaSlicer project", "[3mf][Bambu]")
{
    // A bed of 250 x 210 mm in Tisma.
    const BoundingBoxf saved_bed = s_multiple_beds.get_build_volume_box();
    s_multiple_beds.update_build_volume(BoundingBoxf(Vec2d(0., 0.), Vec2d(250., 210.)));

    Model model;
    DynamicPrintConfig config;
    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Enable };
    boost::optional<Semver> version;
    REQUIRE(load_3mf(bambu_project().c_str(), config, ctxt, &model, false, version));
    const Vec2d bed_1 = s_multiple_beds.get_bed_translation(1).head<2>();
    s_multiple_beds.update_build_volume(saved_bed);

    // One object per object of the project (not one per part).
    REQUIRE(model.objects.size() == 2);
    const ModelObject &big = *model.objects[0], &small = *model.objects[1];
    CHECK(big.name == "Big box");
    CHECK(small.name == "Small box");

    // The parts: the body and the modifier, with their names, extruder, settings and matrix.
    REQUIRE(big.volumes.size() == 2);
    CHECK(big.volumes[0]->is_model_part());
    CHECK(big.volumes[0]->name == "Box body");
    CHECK(big.volumes[1]->is_modifier());
    CHECK(big.volumes[1]->name == "Dense core");
    CHECK(big.volumes[1]->config.opt_int("extruder") == 2);
    REQUIRE(big.volumes[1]->config.has("fill_density"));
    CHECK(big.volumes[1]->config.option("fill_density")->getFloat() == Approx(60.));
    CHECK(big.volumes[1]->get_offset().isApprox(Vec3d(3., 0., 5.)));
    // The object: extruder 1 and 4 walls (wall_loops -> perimeters).
    CHECK(big.config.opt_int("extruder") == 1);
    CHECK(big.config.opt_int("perimeters") == 4);
    CHECK(small.config.opt_int("extruder") == 2);
    // The painted triangles (colors) of the body.
    CHECK(! big.volumes[0]->mm_segmentation_facets.empty());
    CHECK(big.volumes[1]->mm_segmentation_facets.empty());

    // Instances: one of the big box, two of the small one.
    REQUIRE(big.instances.size() == 1);
    REQUIRE(small.instances.size() == 2);
    // The plates and the beds: plate 1 -> bed 0, plate 2 -> bed 1, same place relative to the center of the bed.
    CHECK(model.plates[0].name == "Main");
    CHECK(model.plates[1].name == "Spare");
    const Vec2d center(125., 105.);
    const Vec2d bambu_center(128., 128.);
    CHECK(big.instances[0]->get_offset().head<2>().isApprox(Vec2d(100., 120.) - bambu_center + center));
    CHECK(small.instances[0]->get_offset().head<2>().isApprox(Vec2d(160., 120.) - bambu_center + center));
    const Vec2d second_plate(256. * 1.2, 0.);
    CHECK(bed_1.x() > 250.);
    CHECK(small.instances[1]->get_offset().head<2>().isApprox(Vec2d(437.2, 140.) - second_plate - bambu_center + center + bed_1));
}

TEST_CASE("Settings of a Bambu Studio / OrcaSlicer project in Tisma", "[3mf][Bambu]")
{
    Model model;
    DynamicPrintConfig config;
    BambuProjectInfo info;
    std::string error;
    REQUIRE(load_bambu_project_3mf(bambu_project(), model, config, info, error));
    CHECK(info.plates == 2);

    // Print settings.
    CHECK(config.opt_float("layer_height") == Approx(0.16));
    CHECK(config.option("first_layer_height")->getFloat() == Approx(0.2));
    CHECK(config.opt_int("perimeters") == 3);
    CHECK(config.opt_int("top_solid_layers") == 5);
    CHECK(config.opt_int("bottom_solid_layers") == 4);
    CHECK(config.opt<ConfigOptionPercent>("fill_density")->value == Approx(25.));
    CHECK(config.opt_serialize("fill_pattern") == "gyroid");
    CHECK(config.opt_serialize("top_fill_pattern") == "monotoniclines");
    CHECK(config.opt_serialize("seam_position") == "rear");
    CHECK(config.opt_bool("support_material"));
    CHECK(config.opt_bool("support_material_auto"));
    CHECK(config.opt_serialize("support_material_style") == "organic");
    CHECK(config.opt_float("brim_width") == Approx(0.));
    CHECK(config.option("external_perimeter_speed")->getFloat() == Approx(120.));
    CHECK(config.option("perimeter_speed")->getFloat() == Approx(200.));
    // Filament settings, one per filament of the project.
    CHECK(config.opt<ConfigOptionInts>("temperature")->values == std::vector<int>{ 220, 245 });
    CHECK(config.opt<ConfigOptionInts>("first_layer_temperature")->values == std::vector<int>{ 225, 250 });
    CHECK(config.opt<ConfigOptionInts>("bed_temperature")->values == std::vector<int>{ 55, 70 });
    CHECK(config.opt<ConfigOptionInts>("first_layer_bed_temperature")->values == std::vector<int>{ 60, 75 });
    CHECK(config.opt<ConfigOptionStrings>("filament_colour")->values == std::vector<std::string>{ "#FF0000", "#00AE42" });
    CHECK(config.opt<ConfigOptionStrings>("filament_type")->values == std::vector<std::string>{ "PLA", "PETG" });
    // No printer settings: the project is printed on the printer selected in Tisma.
    CHECK_FALSE(config.has("bed_shape"));
    CHECK_FALSE(config.has("max_print_height"));
    CHECK_FALSE(config.has("start_gcode"));
    CHECK_FALSE(config.has("nozzle_diameter"));
}

TEST_CASE("Line widths and accelerations of Bambu Studio / OrcaSlicer in percent", "[3mf][Bambu]")
{
    DynamicPrintConfig out;
    std::vector<std::string> skipped;
    translate_bambu_settings({ { "nozzle_diameter", { "0.6" } }, { "outer_wall_line_width", { "105%" } },
                               { "default_acceleration", { "10000" } }, { "outer_wall_acceleration", { "50%" } },
                               { "sparse_infill_pattern", { "tri-hexagon" } }, { "unknown_option_of_orca", { "1" } },
                               { "wall_loops", { "-3" } } },
                             out, false, &skipped);
    CHECK(out.opt<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width")->value == Approx(0.63));
    CHECK_FALSE(out.opt<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width")->percent);
    CHECK(out.opt_float("external_perimeter_acceleration") == Approx(5000.));
    CHECK(out.opt_serialize("fill_pattern") == "stars");
    // Unknown options and values out of range are not loaded.
    CHECK(std::find(skipped.begin(), skipped.end(), "unknown_option_of_orca") != skipped.end());
    CHECK_FALSE(out.has("perimeters"));
}
