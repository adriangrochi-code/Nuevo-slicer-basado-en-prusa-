///|/ Copyright (c) Prusa Research 2020 - 2021 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SLA_ROTFINDER_HPP
#define SLA_ROTFINDER_HPP

#include <libslic3r/Point.hpp>
#include <functional>
#include <array>
#include <utility>

namespace Slic3r {

class ModelObject;
class SLAPrintObject;
class TriangleMesh;
class DynamicPrintConfig;

namespace sla {

using RotOptimizeStatusCB = std::function<bool(int)>;

class RotOptimizeParams {
    float m_accuracy = 1.;
    const DynamicPrintConfig *m_print_config = nullptr;
    RotOptimizeStatusCB m_statuscb = [](int) { return true; };

public:

    RotOptimizeParams &accuracy(float a) { m_accuracy = a; return *this; }
    RotOptimizeParams &print_config(const DynamicPrintConfig *c)
    {
        m_print_config = c;
        return *this;
    }
    RotOptimizeParams &statucb(RotOptimizeStatusCB cb)
    {
        m_statuscb = std::move(cb);
        return *this;
    }

    float accuracy() const { return m_accuracy; }
    const DynamicPrintConfig * print_config() const { return m_print_config; }
    const RotOptimizeStatusCB &statuscb() const { return m_statuscb; }
};

/**
  * The function should find the best rotation for SLA upside down printing.
  *
  * @param modelobj The model object representing the 3d mesh.
  * @param accuracy The optimization accuracy from 0.0f to 1.0f. Currently,
  * the nlopt genetic optimizer is used and the number of iterations is
  * accuracy * 100000. This can change in the future.
  * @param statuscb A status indicator callback called with the int
  * argument spanning from 0 to 100. May not reach 100 if the optimization finds
  * an optimum before max iterations are reached. It should return a boolean
  * signaling if the operation may continue (true) or not (false). A status
  * value lower than 0 shall not update the status but still return a valid
  * continuation indicator.
  *
  * @return Returns the rotations around each axis (x, y, z)
  */
Vec2d find_best_misalignment_rotation(const ModelObject &modelobj,
                                      const RotOptimizeParams & = {});

Vec2d find_least_supports_rotation(const ModelObject &modelobj,
                                   const RotOptimizeParams & = {});

Vec2d find_min_z_height_rotation(const ModelObject &mo,
                                 const RotOptimizeParams &params = {});

// Tisma: rotation of an FFF print with the least support material. The candidates lay a face of the convex hull on the
// bed; each is scored by the volume of support under the overhangs steeper than the support threshold of the
// configuration (45 degrees from the horizontal when automatic), from the overhang down to the part or the bed, minus
// a 1 mm slab of the area touching the bed (adhesion). Orientations standing on less than 10 mm2 are avoided.
Vec2d find_least_fff_support_rotation(const ModelObject &mo,
                                      const RotOptimizeParams &params = {});

// Tisma: rotation of an SLA print with the lowest peel force and no suction cups. Among the orientations needing the
// fewest supports (the score of find_least_supports_rotation), the one with no suction cups (sla/SuctionCups.hpp)
// and then the smallest largest layer is chosen: the force that tears each layer from the film grows with its area.
Vec2d find_least_peel_rotation(const ModelObject &mo,
                               const RotOptimizeParams &params = {});

} // namespace sla
} // namespace Slic3r

#endif // SLAROTFINDER_HPP
