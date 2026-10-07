#include <catch2/catch_test_macros.hpp>
#include <test_utils.hpp>

#include <cstring>
#include <fstream>
#include <iterator>

#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/FileReader.hpp"
#include "libslic3r/Format/GooSLA.hpp"

#include <boost/filesystem.hpp>

using namespace Slic3r;

namespace {

std::uint32_t be_u32(const std::vector<std::uint8_t> &d, std::size_t at)
{
    return (std::uint32_t(d[at]) << 24) | (std::uint32_t(d[at + 1]) << 16) | (std::uint32_t(d[at + 2]) << 8) | d[at + 3];
}
std::uint16_t be_u16(const std::vector<std::uint8_t> &d, std::size_t at)
{
    return std::uint16_t((d[at] << 8) | d[at + 1]);
}
float be_f32(const std::vector<std::uint8_t> &d, std::size_t at)
{
    const std::uint32_t u = be_u32(d, at);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

} // namespace

TEST_CASE("GOO layer encoding round trip", "[sla_archives][goo]") {
    std::vector<std::uint8_t> image;
    // Long black and white runs (several extra length bytes), gray anti-aliasing pixels and single pixels.
    image.insert(image.end(), 1000000, 0x00);
    image.insert(image.end(), 70000, 0xFF);
    for (int i = 0; i < 300; ++ i)
        image.push_back(std::uint8_t(i * 7));
    image.insert(image.end(), 17, 0x80);
    image.push_back(0x00);
    image.push_back(0xFF);
    image.insert(image.end(), 5000, 0x00);

    const std::vector<std::uint8_t> enc = Goo::encode_layer(image.data(), image.size());
    REQUIRE(enc.size() > 2);
    CHECK(enc.front() == Goo::LAYER_MAGIC);
    std::uint8_t sum = 0;
    for (std::size_t k = 1; k + 1 < enc.size(); ++ k)
        sum = std::uint8_t(sum + enc[k]);
    CHECK(enc.back() == std::uint8_t(~sum));
    // A run-length encoding: much smaller than the image.
    CHECK(enc.size() < image.size() / 100);

    std::vector<std::uint8_t> decoded;
    REQUIRE(Goo::decode_layer(enc.data(), enc.size(), image.size(), decoded));
    CHECK(decoded == image);

    SECTION("a wrong checksum is rejected") {
        std::vector<std::uint8_t> bad = enc;
        bad[1] ^= 0x01;
        CHECK(! Goo::decode_layer(bad.data(), bad.size(), image.size(), decoded));
    }
    SECTION("a wrong pixel count is rejected") {
        CHECK(! Goo::decode_layer(enc.data(), enc.size(), image.size() + 1, decoded));
        CHECK(! Goo::decode_layer(enc.data(), enc.size(), image.size() - 1, decoded));
    }
}

TEST_CASE("GOO difference chunks are decoded", "[sla_archives][goo]") {
    // Gray 0x80 x1, then +3 (one pixel), then -2 with a run of 4 (as other slicers write them).
    std::vector<std::uint8_t> data { Goo::LAYER_MAGIC, 0x41, 0x80, 0x83, 0xB2, 0x04 };
    std::uint8_t sum = 0;
    for (std::size_t k = 1; k < data.size(); ++ k)
        sum = std::uint8_t(sum + data[k]);
    data.push_back(std::uint8_t(~sum));

    std::vector<std::uint8_t> decoded;
    REQUIRE(Goo::decode_layer(data.data(), data.size(), 6, decoded));
    CHECK(decoded == std::vector<std::uint8_t>{ 0x80, 0x83, 0x81, 0x81, 0x81, 0x81 });
}

TEST_CASE("GOO export of a sliced print", "[sla_archives][goo]") {
    SLAPrint print;
    SLAFullPrintConfig fullcfg;
    auto m = FileReader::load_model(TEST_DATA_DIR PATH_SEPARATOR + std::string("20mm_cube.obj"));

    fullcfg.printer_technology.setInt(ptSLA);
    fullcfg.set("sla_archive_format", "GOO");
    fullcfg.set("supports_enable", false);
    fullcfg.set("pad_enable", false);
    fullcfg.set_deserialize_strict("display_orientation", "landscape");
    fullcfg.set("display_pixels_x", 600);
    fullcfg.set("display_pixels_y", 400);
    fullcfg.set("display_width", 60.);
    fullcfg.set("display_height", 40.);
    fullcfg.set("layer_height", 0.05);
    fullcfg.set("exposure_time", 2.5);
    fullcfg.set("initial_exposure_time", 25.);
    fullcfg.set("faded_layers", 4);

    DynamicPrintConfig cfg;
    cfg.apply(fullcfg);
    // The material notes are in the full configuration of the GUI (material preset), not in SLAFullPrintConfig.
    cfg.set_key_value("material_ow_faded_layers", new ConfigOptionInt(4));
    cfg.set_key_value("material_notes", new ConfigOptionString("LIFT_DISTANCE=6\nLIFT_SPEED=2\nRETRACT_SPEED=3\nLIGHT_PWM=200"));
    print.set_status_callback([](const PrintBase::SlicingStatus&) {});
    print.apply(m, cfg);
    print.process();

    const std::string fname = "output_goo_cube.goo";
    print.export_print(fname, ThumbnailsList{}, "cube");
    REQUIRE(boost::filesystem::exists(fname));

    std::ifstream in(fname, std::ios::binary);
    const std::vector<std::uint8_t> d { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    REQUIRE(d.size() > Goo::LAYERS_OFFSET + 11);

    // Header.
    CHECK(std::string(d.begin(), d.begin() + 4) == "V3.0");
    const std::uint8_t magic[8] = { 0x07, 0x00, 0x00, 0x00, 0x44, 0x4C, 0x50, 0x00 };
    CHECK(std::memcmp(d.data() + 4, magic, 8) == 0);
    const std::size_t after_previews = 194 + 116 * 116 * 2 + 2 + 290 * 290 * 2 + 2;
    CHECK(be_u16(d, 194 + 116 * 116 * 2) == 0x0D0A);
    CHECK(be_u16(d, after_previews - 2) == 0x0D0A);
    const std::uint32_t layers = be_u32(d, after_previews);
    CHECK(layers == print.print_layers().size());
    CHECK(layers == 395); // 20 mm: the first layer of 0.3 mm and then 0.05 mm
    CHECK(be_u16(d, after_previews + 4) == 600);
    CHECK(be_u16(d, after_previews + 6) == 400);
    CHECK(be_f32(d, after_previews + 10) == 60.f);
    CHECK(be_f32(d, after_previews + 14) == 40.f);
    CHECK(be_f32(d, after_previews + 22) == 0.05f);
    CHECK(be_f32(d, after_previews + 26) == 2.5f);
    CHECK(be_u32(d, Goo::LAYERS_OFFSET - 7) == Goo::LAYERS_OFFSET); // layer content offset
    CHECK(be_f32(d, after_previews + 59) == 25.f);               // bottom exposure
    CHECK(be_u32(d, after_previews + 63) == 4);                  // bottom layers

    // Layers.
    std::size_t at = Goo::LAYERS_OFFSET;
    float last_z = 0.f;
    std::size_t lit_layers = 0;
    for (std::uint32_t i = 0; i < layers; ++ i) {
        INFO("layer " << i);
        REQUIRE(at + 66 + 4 <= d.size());
        const float z = be_f32(d, at + 6);
        CHECK(z > last_z);
        last_z = z;
        CHECK(be_f32(d, at + 10) == (i < 4 ? 25.f : 2.5f));
        CHECK(be_f32(d, at + 30) == 6.f);   // lift distance (the bottom lift follows it when not given)
        CHECK(be_f32(d, at + 34) == 120.f); // 2 mm/s in mm/min
        CHECK(be_u16(d, at + 62) == (i < 4 ? 255 : 200));
        CHECK(be_u16(d, at + 64) == 0x0D0A);
        const std::uint32_t size = be_u32(d, at + 66);
        REQUIRE(at + 70 + size + 2 <= d.size());
        std::vector<std::uint8_t> pixels;
        REQUIRE(Goo::decode_layer(d.data() + at + 70, size, 600 * 400, pixels));
        std::size_t lit = 0;
        for (std::uint8_t p : pixels)
            lit += p > 127;
        // A 20 x 20 mm square at 0.1 mm per pixel: about 200 x 200 pixels.
        if (lit > 0) {
            ++ lit_layers;
            CHECK(lit > 190 * 190);
            CHECK(lit < 210 * 210);
        }
        at += 70 + size;
        CHECK(be_u16(d, at) == 0x0D0A);
        at += 2;
    }
    CHECK(lit_layers == layers);
    CHECK(std::abs(last_z - 20.f) < 0.06f);

    const std::uint8_t ending[11] = { 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x44, 0x4C, 0x50, 0x00 };
    REQUIRE(at + 11 == d.size());
    CHECK(std::memcmp(d.data() + at, ending, 11) == 0);
}
