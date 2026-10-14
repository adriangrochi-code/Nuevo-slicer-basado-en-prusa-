///|/ Tisma Slicer: suction cups of resin prints.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SuctionCups.hpp"

#include <algorithm>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"

namespace Slic3r {
namespace sla {

std::vector<SuctionCup> find_suction_cups(const std::vector<ExPolygons> &slices, bool bottom_sealed, double min_area,
                                          double min_vent)
{
    std::vector<SuctionCup> cups;
    BoundingBox bbox;
    for (const ExPolygons &layer : slices)
        for (const ExPolygon &expoly : layer)
            bbox.merge(get_extents(expoly.contour));
    if (! bbox.defined)
        return cups;
    bbox.offset(scaled(1.));
    const Polygon frame = bbox.polygon();
    const double  min_area_scaled = min_area / (SCALING_FACTOR * SCALING_FACTOR);
    const double  min_vent_scaled = min_vent / (SCALING_FACTOR * SCALING_FACTOR);
    // Overlaps smaller than this are numerical noise.
    const double  tiny = 0.01 / (SCALING_FACTOR * SCALING_FACTOR);

    // Air that can be reached from below (vented) at the previous layer, and the hollows that cannot (with their cup).
    ExPolygons vented_prev;
    std::vector<std::pair<ExPolygon, size_t>> sealed_prev;
    bool first = true;
    for (size_t layer = 0; layer < slices.size(); ++ layer) {
        if (slices[layer].empty()) {
            // Nothing printed here: everything is open (the part starts higher, or a gap between parts).
            vented_prev = { ExPolygon(frame) };
            sealed_prev.clear();
            continue;
        }
        ExPolygons vented;
        std::vector<std::pair<ExPolygon, size_t>> sealed;
        for (ExPolygon &air : diff_ex(frame, slices[layer])) {
            // The air around the part reaches the frame.
            if (air.contour.contains(bbox.min)) {
                vented.emplace_back(std::move(air));
                continue;
            }
            // A hollow enclosed by the part in this layer: vented if it continues into vented air below through an opening
            // wide enough (a hollow smaller than that only needs to be open).
            const double air_area = air.area();
            const double needed   = std::max(tiny, std::min(min_vent_scaled, 0.5 * air_area));
            bool open_below = false;
            if (first)
                open_below = ! bottom_sealed && air_area >= needed;
            else {
                double opening = 0.;
                for (const ExPolygon &v : vented_prev)
                    if (get_extents(v).overlap(get_extents(air)))
                        opening += area(intersection_ex(ExPolygons{ air }, ExPolygons{ v }));
                open_below = opening >= needed;
            }
            if (open_below) {
                vented.emplace_back(std::move(air));
                continue;
            }
            // Closed below: the cup it continues, or a new one from its floor.
            size_t cup = size_t(-1);
            if (! first)
                for (const auto &[region, id] : sealed_prev)
                    if (get_extents(region).overlap(get_extents(air)) && area(intersection_ex(ExPolygons{ air }, ExPolygons{ region })) > tiny) {
                        cup = id;
                        break;
                    }
            if (cup == size_t(-1)) {
                cup = cups.size();
                cups.push_back({ layer, layer, 0., Vec2d::Zero(), layer });
            }
            SuctionCup &c = cups[cup];
            c.last_layer = layer;
            if (air_area > c.max_area) {
                c.max_area  = air_area;
                c.max_layer = layer;
                c.center    = unscaled(get_extents(air.contour).center());
            }
            sealed.emplace_back(std::move(air), cup);
        }
        vented_prev = std::move(vented);
        sealed_prev = std::move(sealed);
        first = false;
    }
    // In mm^2, without the small ones.
    std::vector<SuctionCup> out;
    for (SuctionCup &c : cups)
        if (c.max_area >= min_area_scaled) {
            c.max_area *= SCALING_FACTOR * SCALING_FACTOR;
            out.emplace_back(c);
        }
    std::sort(out.begin(), out.end(), [](const SuctionCup &a, const SuctionCup &b) { return a.max_area > b.max_area; });
    return out;
}

} // namespace sla
} // namespace Slic3r
