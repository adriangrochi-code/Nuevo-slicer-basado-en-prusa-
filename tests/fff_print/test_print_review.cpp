// Tisma Slicer: the review before printing (PrintReview.hpp) finds each problem on a part made to have it, and nothing
// on a plain cube.
#include <catch2/catch_test_macros.hpp>

#include <libslic3r/GCode/GCodeProcessor.hpp>
#include <libslic3r/Layer.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/PrintReview.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>

#include <algorithm>
#include <functional>

#include "test_data.hpp"

using namespace Slic3r;
using Kind     = PrintReviewItem::Kind;
using Severity = PrintReviewItem::Severity;

namespace {

TriangleMesh box(double x, double y, double z, const Vec3f &at = Vec3f::Zero())
{
    indexed_triangle_set its = its_make_cube(x, y, z);
    for (stl_vertex &v : its.vertices)
        v += at;
    return TriangleMesh(its);
}

TriangleMesh merged(std::initializer_list<TriangleMesh> parts)
{
    indexed_triangle_set its;
    for (const TriangleMesh &part : parts)
        its_merge(its, part.its);
    return TriangleMesh(its);
}

std::vector<PrintReviewItem> review(std::vector<TriangleMesh> meshes,
                                    std::initializer_list<ConfigBase::SetDeserializeItem> settings = {},
                                    std::function<void(const Print&)> inspect = {})
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict(settings);
    Print print;
    Model model;
    Test::init_print(std::move(meshes), print, model, config);
    print.process();
    GCodeProcessorResult result;
    const boost::filesystem::path temp = boost::filesystem::unique_path();
    print.export_gcode(temp.string(), &result, nullptr);
    boost::nowide::remove(temp.string().c_str());
    if (inspect)
        inspect(print);
    return review_print(print, result);
}

const PrintReviewItem* find(const std::vector<PrintReviewItem> &items, Kind kind)
{
    auto it = std::find_if(items.begin(), items.end(), [kind](const PrintReviewItem &i) { return i.kind == kind; });
    return it == items.end() ? nullptr : &*it;
}

std::string kinds(const std::vector<PrintReviewItem> &items)
{
    std::string out;
    for (const PrintReviewItem &i : items)
        out += i.title + " (" + i.detail + ")\n";
    return out;
}

} // namespace

TEST_CASE("Print review of a plain cube finds no problem", "[TismaReview]")
{
    const std::vector<PrintReviewItem> items = review({ box(20., 20., 20.) });
    INFO(kinds(items));
    CHECK(std::none_of(items.begin(), items.end(), [](const PrintReviewItem &i) { return i.severity != Severity::Info; }));
}

TEST_CASE("Print review finds a part starting in the air", "[TismaReview]")
{
    // A block standing on the bed and, beside it, a small one starting 10 mm above the bed: no supports.
    const std::vector<PrintReviewItem> items = review({ merged({ box(20., 20., 5.), box(5., 5., 5., Vec3f(30.f, 0.f, 10.f)) }) });
    INFO(kinds(items));
    const PrintReviewItem *floating = find(items, Kind::FloatingPart);
    REQUIRE(floating != nullptr);
    CHECK(floating->severity == Severity::Critical);
    CHECK(floating->z > 10.);
    CHECK(floating->z < 15.);
    CHECK(items.front().severity == Severity::Critical);
}

TEST_CASE("Print review finds details too thin to print", "[TismaReview]")
{
    // A wall 0.05 mm thick beside a block: in the slices of every layer, but no extrusion fits in it.
    const std::vector<PrintReviewItem> items = review({ merged({ box(20., 20., 5.), box(0.05, 10., 5., Vec3f(25.f, 0.f, 0.f)) }) });
    INFO(kinds(items));
    const PrintReviewItem *lost = find(items, Kind::LostDetails);
    REQUIRE(lost != nullptr);
    // The top layer of the 5 mm wall: 0.35 mm, then 0.3 mm layers.
    CHECK(lost->z_max < 5.2);
    CHECK(lost->count == 1);
}

TEST_CASE("Print review does not report the tip of a cone", "[TismaReview]")
{
    // The last layer of a pointed top is a sliver too thin to print: that is not a detail of the model.
    const std::vector<PrintReviewItem> items = review({ TriangleMesh(its_make_cone(8., 12.)) });
    INFO(kinds(items));
    CHECK(find(items, Kind::LostDetails) == nullptr);
}

TEST_CASE("Print review measures the bridges against what is under them", "[TismaReview]")
{
    // Two pillars 30 mm apart joined by a slab: a 30 mm bridge over the air.
    const auto span = [](double gap) {
        return merged({ box(5., 10., 10.), box(5., 10., 10., Vec3f(float(5. + gap), 0.f, 0.f)),
                        box(float(10. + gap), 10., 2., Vec3f(0.f, 0.f, 10.f)) });
    };
    const std::vector<PrintReviewItem> long_one = review({ span(30.) });
    INFO(kinds(long_one));
    const PrintReviewItem *bridge = find(long_one, Kind::LongBridge);
    REQUIRE(bridge != nullptr);
    CHECK(bridge->z > 10.);
    CHECK(bridge->z < 10.5);
    // The printer bridges 35 mm: no warning; a 10 mm bridge: none either.
    CHECK(find(review({ span(30.) }, { { "max_bridge_length", "35" } }), Kind::LongBridge) == nullptr);
    CHECK(find(review({ span(10.) }), Kind::LongBridge) == nullptr);

    // A block with sparse infill: the first solid layers over the infill are bridges, with the infill under them.
    size_t bridges = 0;
    const std::vector<PrintReviewItem> block = review({ box(40., 40., 10.) }, { { "fill_density", "15%" } }, [&bridges](const Print &print) {
        for (const PrintObject *object : print.objects())
            for (const Layer *layer : object->layers())
                for (const LayerRegion *region : layer->regions())
                    for (const ExtrusionEntity *e : region->fills().flatten().entities)
                        bridges += e->role().is_bridge() ? 1 : 0;
    });
    INFO(kinds(block));
    CHECK(bridges > 0);
    CHECK(find(block, Kind::LongBridge) == nullptr);
}

TEST_CASE("Print review: layers without time to cool down", "[TismaReview]")
{
    // A thin pillar cannot take the 15 s of the cooling at 60 mm/s, but the cooling slows its layers down below the
    // minimum print speed until they take that time: no layer is left without time to cool down.
    const std::vector<PrintReviewItem> items = review({ box(3., 3., 8.) }, { { "min_print_speed", "60" }, { "slowdown_below_layer_time", "15" } });
    INFO(kinds(items));
    CHECK(find(items, Kind::Overheating) == nullptr);
    // Not with the cooling off.
    const std::vector<PrintReviewItem> off = review({ box(3., 3., 8.) }, { { "min_print_speed", "60" }, { "cooling", "0" } });
    CHECK(find(off, Kind::Overheating) == nullptr);
}

TEST_CASE("Print review finds travels over the air", "[TismaReview]")
{
    // Two pillars apart: every layer travels from one to the other.
    const std::vector<PrintReviewItem> retracting = review({ box(5., 5., 5.), box(5., 5., 5.) });
    INFO(kinds(retracting));
    // With a retraction the travels over the air are how every print is made: nothing to report.
    CHECK(find(retracting, Kind::TravelsOverAir) == nullptr);

    // Retracting while wiping counts as a retraction.
    const std::vector<PrintReviewItem> wiping = review({ box(5., 5., 5.), box(5., 5., 5.) }, { { "wipe", "1" } });
    INFO(kinds(wiping));
    CHECK(find(wiping, Kind::TravelsOverAir) == nullptr);

    const std::vector<PrintReviewItem> oozing = review({ box(5., 5., 5.), box(5., 5., 5.) }, { { "retract_before_travel", "500" } });
    INFO(kinds(oozing));
    const PrintReviewItem *strings = find(oozing, Kind::TravelsOverAir);
    REQUIRE(strings != nullptr);
    CHECK(strings->severity == Severity::Warning);
    CHECK(strings->count >= 20);

    // One block: the travels stay over the part (the one from the skirt does not count).
    const std::vector<PrintReviewItem> block = review({ box(20., 20., 5.) });
    INFO(kinds(block));
    CHECK(find(block, Kind::TravelsOverAir) == nullptr);
}

TEST_CASE("Print review finds speeds above the machine limits", "[TismaReview]")
{
    const std::vector<PrintReviewItem> items = review({ box(20., 20., 2.) },
        { { "machine_limits_usage", "time_estimate_only" }, { "machine_max_feedrate_x", "20,20" }, { "machine_max_feedrate_y", "20,20" } });
    INFO(kinds(items));
    const PrintReviewItem *limited = find(items, Kind::SpeedLimited);
    REQUIRE(limited != nullptr);
    CHECK(limited->count > 10);
    const std::vector<PrintReviewItem> ignored = review({ box(20., 20., 2.) },
        { { "machine_limits_usage", "ignore" }, { "machine_max_feedrate_x", "20,20" }, { "machine_max_feedrate_y", "20,20" } });
    CHECK(find(ignored, Kind::SpeedLimited) == nullptr);
}
