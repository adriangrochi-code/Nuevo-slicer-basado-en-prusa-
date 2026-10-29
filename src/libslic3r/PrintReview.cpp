///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#include "PrintReview.hpp"

#include "ClipperUtils.hpp"
#include "GCode/GCodeProcessor.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "NonPlanar.hpp"
#include "Print.hpp"
#include "SupportSpotsGenerator.hpp"
#include "format.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

namespace Slic3r {

namespace {

using Item     = PrintReviewItem;
using Severity = PrintReviewItem::Severity;
using Kind     = PrintReviewItem::Kind;
using Cause    = SupportSpotsGenerator::SupportPointCause;

std::string mm(double v, int decimals = 1) { return float_to_string_decimal_point(v, decimals); }

// The layers of a set of objects, gathered while looking at them.
struct Places
{
    std::set<std::string> objects;
    double                z_min { std::numeric_limits<double>::max() };
    double                z_max { std::numeric_limits<double>::lowest() };
    size_t                count { 0 };

    void add(double z, size_t n = 1)
    {
        z_min = std::min(z_min, z);
        z_max = std::max(z_max, z);
        count += n;
    }
    bool empty() const { return count == 0; }
};

Item make_item(Kind kind, Severity severity, const Places &places, double z_offset, std::string title, std::string detail,
               std::string hint)
{
    Item item { kind, severity, std::move(title), std::move(detail), std::move(hint),
                std::vector<std::string>(places.objects.begin(), places.objects.end()) };
    item.z     = places.z_min + z_offset;
    item.z_min = places.z_min + z_offset;
    item.z_max = places.z_max + z_offset;
    item.count = places.count;
    return item;
}

std::string heights(const Places &places, double z_offset)
{
    return std::abs(places.z_max - places.z_min) < EPSILON ?
        format(_u8L("at %1% mm"), mm(places.z_min + z_offset, 2)) :
        format(_u8L("between %1% and %2% mm"), mm(places.z_min + z_offset, 2), mm(places.z_max + z_offset, 2));
}

// The print_z of the layer printed on top of a height (the support points are at the bottom of their layer).
double layer_above(const PrintObject &object, double z)
{
    const auto layers = object.layers();
    auto it = std::upper_bound(layers.begin(), layers.end(), z + EPSILON,
                               [](double v, const Layer *l) { return v < l->print_z; });
    return it == layers.end() ? (layers.empty() ? z : layers.back()->print_z) : (*it)->print_z;
}

// Parts that fall or come loose without supports: what PrusaSlicer's support spots found, on the objects without
// supports (as Print::alert_when_supports_needed()), with the layers where it happens.
void review_stability(const Print &print, double z_offset, std::vector<Item> &out)
{
    std::map<std::pair<Cause, bool>, Places> found;
    std::set<const ModelObject*> seen;
    for (const PrintObject *object : print.objects()) {
        if (object->has_support() || object->shared_regions() == nullptr ||
            ! object->shared_regions()->generated_support_points.has_value() || ! seen.insert(object->model_object()).second)
            continue;
        const auto &generated = *object->shared_regions()->generated_support_points;
        SupportSpotsGenerator::PartialObjects partial = generated.partial_objects;
        for (const auto &[cause, critical] : SupportSpotsGenerator::gather_issues(generated.support_points, partial)) {
            // Its long bridges measure the distance to the edges of the layer below, not whether there is something
            // under the bridge: the bridges over the infill and over the solid layers count too. review_bridges()
            // measures them against what is below.
            if (cause == Cause::LongBridge)
                continue;
            Places &places = found[{ cause, critical }];
            places.objects.insert(object->model_object()->name);
            if (cause == Cause::UnstableFloatingPart) {
                // As gather_issues(): the parts not standing on the bed, bigger than a 200th of the largest one.
                float largest = 0.f;
                for (const SupportSpotsGenerator::PartialObject &p : generated.partial_objects)
                    largest = std::max(largest, p.volume);
                for (const SupportSpotsGenerator::PartialObject &p : generated.partial_objects)
                    if (! p.connected_to_bed && p.volume > largest / 200.f)
                        places.add(layer_above(*object, p.centroid.z()));
            } else {
                for (const SupportSpotsGenerator::SupportPoint &sp : generated.support_points)
                    if (sp.cause == cause)
                        places.add(layer_above(*object, sp.position.z()));
            }
            if (places.empty())
                places.add(object->layers().empty() ? 0. : object->layers().front()->print_z);
        }
    }

    const std::string supports = _u8L("Turn on supports (Print Settings > Support material), or orient the part so that "
                                      "it needs less of them.");
    for (const auto &[issue, places] : found) {
        const auto [cause, critical] = issue;
        const std::string where = heights(places, z_offset);
        switch (cause) {
        case Cause::UnstableFloatingPart:
            out.push_back(make_item(Kind::FloatingPart, Severity::Critical, places, z_offset, _u8L("Part in the air"),
                format(_u8L("A part of the object starts in the air %1%: without supports it will not print."), where),
                supports));
            break;
        case Cause::FloatingExtrusion:
            if (critical)
                out.push_back(make_item(Kind::CollapsingOverhang, Severity::Critical, places, z_offset,
                    _u8L("Collapsing overhang"),
                    format(_u8L("A large overhang %1% (%2% places) will print badly or not at all."), where, places.count),
                    supports));
            else
                out.push_back(make_item(Kind::LooseExtrusions, Severity::Warning, places, z_offset, _u8L("Loose extrusions"),
                    format(_u8L("Overhanging extrusions %1% (%2% places) may curl up or come loose."), where, places.count),
                    _u8L("Turn on supports, lower the speed of the overhangs or orient the part so that it has less "
                         "overhangs.")));
            break;
        case Cause::LongBridge:
            break;
        case Cause::FloatingBridgeAnchor:
            out.push_back(make_item(Kind::FloatingBridgeAnchor, Severity::Critical, places, z_offset,
                _u8L("Bridge anchored in the air"),
                format(_u8L("Bridges %1% start or turn in the air: they will print badly."), where), supports));
            break;
        case Cause::SeparationFromBed:
            out.push_back(make_item(Kind::LowBedAdhesion, Severity::Warning, places, z_offset, _u8L("Low bed adhesion"),
                format(_u8L("The object may come off the bed while printing %1%."), where),
                _u8L("Add a brim (Print Settings > Skirt and brim), clean the bed and check the temperature of the first "
                     "layer.")));
            break;
        case Cause::WeakObjectPart:
            out.push_back(make_item(Kind::FragilePart, Severity::Warning, places, z_offset, _u8L("Fragile part"),
                format(_u8L("A thin part %1% may break while the rest is printed."), where),
                _u8L("Turn on supports, lower the speed or print the part in another orientation.")));
            break;
        }
    }
}

// Parts of the model too thin for any extrusion: they are in the slices but nothing is printed there. Only a detail
// lost in at least 3 layers in a row is reported (a thin wall, a pin): the last layer of a rounded top is a sliver that
// is lost too, and it is not a detail of the model.
void review_lost_details(const Print &print, double z_offset, std::vector<Item> &out)
{
    constexpr size_t min_layers = 3;
    Places places;
    double widest = 0.;
    std::set<const ModelObject*> seen;
    for (const PrintObject *object : print.objects()) {
        if (! seen.insert(object->model_object()).second)
            continue;
        // The islands without extrusions, chained with those of the layer below that they overlap.
        struct Lost { BoundingBox box; double width; double z; size_t group; };
        std::vector<Lost>   lost;
        std::vector<size_t> parent;
        auto root = [&parent](size_t g) { while (parent[g] != g) g = parent[g] = parent[parent[g]]; return g; };
        size_t below_begin = 0, below_end = 0;
        for (const Layer *layer : object->layers()) {
            const size_t layer_begin = lost.size();
            for (size_t i = 0; i < layer->lslices_ex.size() && i < layer->lslices.size(); ++ i) {
                const LayerSlice &slice = layer->lslices_ex[i];
                if (slice.has_extrusions() ||
                    std::any_of(slice.islands.begin(), slice.islands.end(), [](const LayerIsland &island) { return ! island.thin_fills.empty(); }))
                    continue;
                const double area = unscaled<double>(unscaled<double>(layer->lslices[i].area()));
                // Slivers of the slicing itself are not details of the model.
                if (area < 0.05)
                    continue;
                const double length = unscaled<double>(layer->lslices[i].contour.length());
                BoundingBox box = get_extents(layer->lslices[i]);
                box.offset(scaled<double>(0.1));
                const size_t group = parent.size();
                parent.push_back(group);
                for (size_t j = below_begin; j < below_end; ++ j)
                    if (lost[j].box.overlap(box))
                        parent[root(lost[j].group)] = group;
                lost.push_back({ box, length > 0. ? 2. * area / length : 0., layer->print_z, group });
            }
            // The next layer chains to these only: a layer without lost islands breaks the chain.
            below_begin = layer_begin;
            below_end   = lost.size();
        }
        std::map<size_t, std::vector<const Lost*>> groups;
        for (const Lost &l : lost)
            groups[root(l.group)].push_back(&l);
        bool lost_here = false;
        for (const auto &[group, members] : groups) {
            std::set<double> zs;
            for (const Lost *l : members)
                zs.insert(l->z);
            if (zs.size() < min_layers)
                continue;
            places.add(*zs.begin());
            places.z_max = std::max(places.z_max, *zs.rbegin());
            for (const Lost *l : members)
                widest = std::max(widest, l->width);
            lost_here = true;
        }
        if (lost_here)
            places.objects.insert(object->model_object()->name);
    }
    if (places.empty())
        return;
    out.push_back(make_item(Kind::LostDetails, Severity::Warning, places, z_offset, _u8L("Details that will not print"),
        format(_u8L("%1% details of the model %2%, up to %3% mm wide, get no extrusion in 3 or more layers in a row: they "
                    "will be missing from the print."),
               places.count, heights(places, z_offset), mm(widest, 2)),
        _u8L("Use a thinner nozzle or a thinner extrusion width, scale the detail up, or turn on \"Detect thin walls\" "
             "(Print Settings > Layers and perimeters).")));
}

// Bridges longer than the printer bridges: the longest run of each bridge extrusion with nothing under it (the layer
// below, widened by half an extrusion where the bridge is anchored). The bridges over the infill and over the solid
// layers have the layer below under them and are not counted.
void review_bridges(const Print &print, double z_offset, std::vector<Item> &out)
{
    const double longest_allowed = print.config().max_bridge_length.value;
    if (longest_allowed <= 0.)
        return;
    // Where the bridge rests on the layer below: half an extrusion beyond its edge.
    const double anchor = 0.5 * scaled<double>(print.config().nozzle_diameter.get_at(0));
    Places places;
    double longest = 0.;
    std::set<const ModelObject*> seen;
    for (const PrintObject *object : print.objects()) {
        if (! seen.insert(object->model_object()).second)
            continue;
        bool found_here = false;
        for (const Layer *layer : object->layers()) {
            if (layer->lower_layer == nullptr)
                continue;
            std::optional<ExPolygons> below;
            for (const LayerRegion *region : layer->regions())
                for (const ExtrusionEntity *e : region->fills().flatten().entities) {
                    if (! e->role().is_bridge() || e->role().is_perimeter())
                        continue;
                    if (! below)
                        below = offset_ex(layer->lower_layer->lslices, float(anchor));
                    double span = 0.;
                    for (const Polyline &piece : diff_pl(e->as_polylines(), *below))
                        span = std::max(span, unscaled<double>(piece.length()));
                    if (span > longest_allowed) {
                        places.add(layer->print_z);
                        longest    = std::max(longest, span);
                        found_here = true;
                    }
                }
        }
        if (found_here)
            places.objects.insert(object->model_object()->name);
    }
    if (places.empty())
        return;
    out.push_back(make_item(Kind::LongBridge, Severity::Warning, places, z_offset, _u8L("Long bridges"),
        format(_u8L("%1% bridges %2% span up to %3% mm with nothing under them, more than the %4% mm this printer bridges "
                    "(Printer Settings > General > Bridges): they may sag."),
               places.count, heights(places, z_offset), mm(longest, 1), mm(longest_allowed, 1)),
        _u8L("Turn on supports, orient the part so that the bridges are shorter, or calibrate the bridges of this filament "
             "and set the longest bridge the printer makes well.")));
}

// The layers of the G-code: time, height, the extruder printing most of it and the slowest extrusion.
struct GCodeLayer
{
    double                    time { 0. };
    double                    z    { std::numeric_limits<double>::lowest() };
    std::map<size_t, double>  extrusion_time;
    bool                      extrudes { false };
    double                    slowest { std::numeric_limits<double>::max() }; // mm/s
};

std::vector<GCodeLayer> gcode_layers(const GCodeProcessorResult &result)
{
    std::vector<GCodeLayer> layers;
    for (const GCodeProcessorResult::MoveVertex &m : result.moves) {
        if (m.internal_only)
            continue;
        if (m.layer_id >= layers.size())
            layers.resize(m.layer_id + 1);
        GCodeLayer &l = layers[m.layer_id];
        const double t = m.time[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
        l.time += t;
        if (m.type == EMoveType::Extrude) {
            l.extrudes = true;
            l.z = std::max(l.z, double(m.position.z()));
            l.extrusion_time[m.extruder_id] += t;
            l.slowest = std::min(l.slowest, double(m.feedrate));
        }
    }
    return layers;
}

// Layers too small to cool down at the minimum print speed. The cooling slows them down below it to take the time the
// filament asks for (CoolingBuffer): they print very slowly and may get blobs or lose their shape. A layer still
// shorter than that time (the cooling could not slow it down enough) is reported too.
void review_overheating(const Print &print, const std::vector<GCodeLayer> &layers, std::vector<Item> &out)
{
    const PrintConfig &config = print.config();
    Places places;
    double slowest  = std::numeric_limits<double>::max();
    double minimum  = 0.;
    double asked    = 0.;
    for (const GCodeLayer &l : layers) {
        if (! l.extrudes || l.extrusion_time.empty())
            continue;
        const size_t extruder = std::max_element(l.extrusion_time.begin(), l.extrusion_time.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; })->first;
        if (! config.cooling.get_at(extruder))
            continue;
        const double slowdown  = config.slowdown_below_layer_time.get_at(extruder);
        const double min_speed = config.min_print_speed.get_at(extruder);
        if (slowdown <= 0.)
            continue;
        // Slowed down below the minimum print speed to take the cooling time (the time estimate of the G-code, with
        // the accelerations, is a little longer than the one of the cooling, hence the margins), or still too short.
        const bool stretched = min_speed > 0. && l.slowest < 0.95 * min_speed && l.time > 0.9 * slowdown &&
                               l.time < 1.5 * slowdown;
        if (stretched || l.time < 0.9 * slowdown) {
            places.add(l.z);
            if (l.slowest < slowest) {
                slowest = l.slowest;
                minimum = min_speed;
                asked   = slowdown;
            }
        }
    }
    if (places.empty())
        return;
    // Shown at the slowest layer: it is the worst one.
    Item item = make_item(Kind::Overheating, Severity::Warning, places, 0., _u8L("Layers too small to cool down"),
        format(_u8L("%1% layers %2% are too small to cool down in %3% s at the minimum print speed (%4% mm/s): they are "
                    "printed as slowly as %5% mm/s, and may get blobs or lose their shape."),
               places.count, heights(places, 0.), int(std::round(asked)), mm(minimum, 0), mm(slowest, 1)),
        _u8L("Print several copies at once (the layers take longer), or lower the time of the slow down for short layers "
             "(Filament Settings > Cooling)."));
    for (const GCodeLayer &l : layers)
        if (l.extrudes && std::abs(l.slowest - slowest) < EPSILON) {
            item.z = l.z;
            break;
        }
    out.push_back(std::move(item));
}

// Travels leaving the part (the islands of the layer and of the supports, widened by a nozzle): the filament oozing
// there leaves strings, more so without a retraction before the travel.
void review_travels(const Print &print, const GCodeProcessorResult &result, const std::vector<GCodeLayer> &layers,
                    std::vector<Item> &out)
{
    const PrintConfig &config = print.config();
    // The G-code of belt printers and of non-planar layers is not in the coordinates of the slices.
    if (config.belt_printer.value || NonPlanar::enabled(config))
        return;
    const double z_offset = config.z_offset.value;
    const coord_t widen   = scaled<coord_t>(config.nozzle_diameter.get_at(0));

    struct Islands { ExPolygons polygons; BoundingBoxes boxes; };
    std::unordered_map<const void*, Islands> cache;
    auto islands_of = [&cache, widen](const void *key, const ExPolygons &slices) -> const Islands& {
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
        Islands islands;
        islands.polygons = offset_ex(slices, float(widen));
        for (const ExPolygon &e : islands.polygons)
            islands.boxes.emplace_back(get_extents(e));
        return cache.emplace(key, std::move(islands)).first->second;
    };
    // What is below the nozzle at the height of a layer, in the coordinates of each instance.
    struct Under { const Islands *islands; Point shift; };
    auto under = [&](double z) {
        std::vector<Under> result;
        for (const PrintObject *object : print.objects()) {
            const auto layers = object->layers();
            if (layers.empty() || z < layers.front()->bottom_z() - EPSILON || z > layers.back()->print_z + EPSILON)
                continue;
            auto it = std::upper_bound(layers.begin(), layers.end(), z + EPSILON, [](double v, const Layer *l) { return v < l->print_z; });
            if (it == layers.begin())
                continue;
            const Layer *layer = *(-- it);
            const Islands &islands = islands_of(layer, layer->lslices);
            for (const PrintInstance &instance : object->instances())
                result.push_back({ &islands, instance.shift });
            for (const SupportLayer *support : object->support_layers())
                if (std::abs(support->print_z - layer->print_z) < EPSILON && ! support->support_islands.empty()) {
                    const Islands &s = islands_of(support, support->support_islands);
                    for (const PrintInstance &instance : object->instances())
                        result.push_back({ &s, instance.shift });
                }
        }
        return result;
    };
    auto inside = [](const std::vector<Under> &unders, const Vec2d &p) {
        for (const Under &u : unders) {
            const Point q = Point(scaled<coord_t>(p.x()), scaled<coord_t>(p.y())) - u.shift;
            for (size_t i = 0; i < u.islands->polygons.size(); ++ i)
                if (u.islands->boxes[i].contains(q) && u.islands->polygons[i].contains(q))
                    return true;
        }
        return false;
    };

    // The travels to and from the skirt, the brim, the wipe tower and the custom G-code go over the air on purpose.
    auto own_extrusion = [](GCodeExtrusionRole role) {
        return role != GCodeExtrusionRole::None && role != GCodeExtrusionRole::Skirt && role != GCodeExtrusionRole::WipeTower &&
               role != GCodeExtrusionRole::Custom;
    };
    std::vector<GCodeExtrusionRole> next_role(result.moves.size(), GCodeExtrusionRole::None);
    for (size_t i = result.moves.size(); i -- > 1;)
        next_role[i - 1] = result.moves[i].type == EMoveType::Extrude ? result.moves[i].extrusion_role : next_role[i];

    Places without_retraction;
    size_t           cached_layer = size_t(-1);
    std::vector<Under> unders;
    bool retracted = false;
    GCodeExtrusionRole last_role = GCodeExtrusionRole::None;
    for (size_t i = 1; i < result.moves.size(); ++ i) {
        const GCodeProcessorResult::MoveVertex &m = result.moves[i];
        if (m.internal_only)
            continue;
        // Retracting: alone, while wiping, or while travelling (negative E); unretracting: alone or with a Z move.
        if (m.type == EMoveType::Retract || m.type == EMoveType::Wipe || (m.type == EMoveType::Travel && m.delta_extruder < 0.f))
            retracted = true;
        else if (m.type == EMoveType::Unretract || m.type == EMoveType::Extrude || (m.type == EMoveType::Travel && m.delta_extruder > 0.f))
            retracted = false;
        if (m.type == EMoveType::Extrude)
            last_role = m.extrusion_role;
        if (m.type != EMoveType::Travel || m.layer_id >= layers.size() || ! layers[m.layer_id].extrudes ||
            ! own_extrusion(last_role) || ! own_extrusion(next_role[i]))
            continue;
        const Vec2d a = result.moves[i - 1].position.head<2>().cast<double>();
        const Vec2d b = m.position.head<2>().cast<double>();
        const double length = (b - a).norm();
        if (length < 2.)
            continue;
        if (m.layer_id != cached_layer) {
            cached_layer = m.layer_id;
            unders       = under(layers[m.layer_id].z - z_offset);
        }
        if (unders.empty())
            continue;
        // The ends are on the part: look at the travel 1 mm away from them, every millimetre.
        bool air = false;
        for (double s = 1.; s <= length - 1. && ! air; s += 1.)
            air = ! inside(unders, a + (b - a) * (s / length));
        if (! air)
            continue;
        if (! retracted)
            without_retraction.add(layers[m.layer_id].z - z_offset);
    }
    // With a retraction the travels over the air are how every print is made: only those without one are a problem.
    if (without_retraction.empty())
        return;
    out.push_back(make_item(Kind::TravelsOverAir, Severity::Warning, without_retraction, z_offset,
        _u8L("Travels over the air without retraction"),
        format(_u8L("%1% travels %2% leave the part without retracting the filament: they will leave strings."),
               without_retraction.count, heights(without_retraction, z_offset)),
        _u8L("Lower \"Minimum travel after retraction\" (Printer Settings > Extruder) so that these travels retract, "
             "or turn on \"Avoid crossing perimeters\".")));
}

// Moves faster than the machine limits: the printer slows them down (longer print, a different estimate).
void review_machine_limits(const Print &print, const GCodeProcessorResult &result, const std::vector<GCodeLayer> &layers,
                           std::vector<Item> &out)
{
    const PrintConfig &config = print.config();
    if (config.machine_limits_usage.value == MachineLimitsUsage::Ignore || config.belt_printer.value)
        return;
    const double max_x = config.machine_max_feedrate_x.get_at(0);
    const double max_y = config.machine_max_feedrate_y.get_at(0);
    if (max_x <= 0. || max_y <= 0.)
        return;
    Places places;
    double fastest = 0.;
    for (size_t i = 1; i < result.moves.size(); ++ i) {
        const GCodeProcessorResult::MoveVertex &m = result.moves[i];
        if (m.internal_only || m.type != EMoveType::Extrude)
            continue;
        const Vec3d d = (m.position - result.moves[i - 1].position).cast<double>();
        const double length = d.norm();
        if (length < EPSILON)
            continue;
        const double f = m.feedrate;
        if (std::abs(d.x()) / length * f > max_x * 1.001 || std::abs(d.y()) / length * f > max_y * 1.001) {
            places.add(m.layer_id < layers.size() ? layers[m.layer_id].z : double(m.position.z()));
            fastest = std::max(fastest, f);
        }
    }
    if (places.empty())
        return;
    out.push_back(make_item(Kind::SpeedLimited, Severity::Info, places, 0., _u8L("Speeds the printer will limit"),
        format(_u8L("%1% extrusions ask for up to %2% mm/s, more than the printer allows (%3% mm/s along X, %4% mm/s "
                    "along Y): it will print them slower."),
               places.count, int(std::round(fastest)), int(std::round(max_x)), int(std::round(max_y))),
        _u8L("Lower the speeds of the print settings, or check the machine limits of the printer (Printer Settings > "
             "Machine limits).")));
}

} // namespace

std::vector<PrintReviewItem> review_print(const Print &print, const GCodeProcessorResult &result)
{
    std::vector<Item> items;
    const double z_offset = print.config().z_offset.value;
    review_stability(print, z_offset, items);
    review_lost_details(print, z_offset, items);
    review_bridges(print, z_offset, items);
    const std::vector<GCodeLayer> layers = gcode_layers(result);
    review_overheating(print, layers, items);
    review_travels(print, result, layers, items);
    review_machine_limits(print, result, layers, items);
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.severity != b.severity ? int(a.severity) > int(b.severity) : a.z < b.z;
    });
    return items;
}

} // namespace Slic3r
