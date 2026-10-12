///|/ Tisma Slicer: the orientation of the print for the loads (the layers are weaker across than along).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/Orientation.hpp"

#include <Eigen/Geometry>

namespace Slic3r {
namespace Fea {

OrientationResult recommend_orientation(const indexed_triangle_set &mesh, const Setup &setup,
                                        std::function<bool()> cancel, std::function<void(int)> progress)
{
    OrientationResult out;
    const std::pair<Vec3d, const char*> directions[3] = { { Vec3d::UnitZ(), "Z" }, { Vec3d::UnitX(), "X" }, { Vec3d::UnitY(), "Y" } };
    for (int i = 0; i < 3; ++ i) {
        Setup s = setup;
        s.build_direction = directions[i].first;
        Result r = analyze(mesh, s, cancel, [&](int p) {
            if (progress)
                progress((100 * i + p) / 3);
        });
        if (! r.ok) {
            out.error = r.error;
            return out;
        }
        out.candidates.push_back({ directions[i].first, directions[i].second, std::move(r) });
        if (cancel && cancel()) {
            out.error = "Cancelled";
            return out;
        }
    }
    const double current = out.candidates.front().result.safety_factor;
    out.best = 0;
    for (size_t i = 1; i < out.candidates.size(); ++ i) {
        const double sf = out.candidates[i].result.safety_factor;
        if (sf > 1.05 * current && sf > out.candidates[out.best].result.safety_factor)
            out.best = i;
    }
    out.ok = true;
    if (progress)
        progress(100);
    return out;
}

Transform3d rotation_to_print(const Vec3d &build_direction)
{
    Transform3d t = Transform3d::Identity();
    t.linear() = Eigen::Quaterniond::FromTwoVectors(build_direction.normalized(), Vec3d::UnitZ()).toRotationMatrix();
    return t;
}

} // namespace Fea
} // namespace Slic3r
