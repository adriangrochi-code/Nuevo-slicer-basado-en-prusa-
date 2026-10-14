///|/ Tisma Slicer: suction cups of resin prints.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SLA_SuctionCups_hpp_
#define slic3r_SLA_SuctionCups_hpp_

// A suction cup (cupping): on a bottom-up resin printer every new layer is cured against the film of the vat. A hollow
// of the part that is closed at the bottom (by the part itself, by the pad or by the build plate) and open towards the
// film is sealed by the film while the layer is peeled off: the vacuum pulls at the part and at the film, which can
// tear the part from the supports or the build plate, deform it or leave it uncured. Resin and air must be able to
// get in from below, through a drain hole in the floor of the hollow, or the part has to be rotated so that the
// hollow opens towards the build plate. See for example the documentation of Mango3D ("Suction Cup Detector") and
// Nexa3D ("Cupping"); the detection on the slices below is Tisma's own.

#include <vector>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {
namespace sla {

struct SuctionCup
{
    // Layers (indices into the slices) where the hollow is open towards the film.
    size_t first_layer { 0 };
    size_t last_layer  { 0 };
    // Largest cross section of the hollow (mm^2) and its center (the layer where it is the largest).
    double max_area    { 0. };
    Vec2d  center      { Vec2d::Zero() };
    size_t max_layer   { 0 };
};

// slices: the cross sections of the part, from the bottom up. bottom_sealed: the first layer lies on the build plate or
// on the pad (a hollow there is closed at the bottom); otherwise the part is lifted on supports and air is below it.
// Hollows smaller than min_area (mm^2) are ignored. A hollow is vented only through an opening of at least min_vent
// (mm^2, about 2 mm of diameter): resin and air hardly flow through a thinner channel.
std::vector<SuctionCup> find_suction_cups(const std::vector<ExPolygons> &slices, bool bottom_sealed, double min_area = 4.,
                                          double min_vent = 3.);

} // namespace sla
} // namespace Slic3r

#endif // slic3r_SLA_SuctionCups_hpp_
