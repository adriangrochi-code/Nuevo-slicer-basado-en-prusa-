///|/ Tisma Slicer: closed mesh of a set of cells of a regular grid (zones, reinforcements).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_CellsMesh_hpp_
#define tisma_fea_CellsMesh_hpp_

#include <map>
#include <vector>

#include <libslic3r/Point.hpp>
#include <admesh/stl.h>

namespace Slic3r {
namespace Fea {

// Closed mesh of the boundary faces of a set of cells of a regular grid (shared vertices).
inline indexed_triangle_set cells_mesh(const std::vector<char> &selected, const Vec3i &n, const Vec3d &origin, double c)
{
    indexed_triangle_set its;
    std::map<long long, int> vertex_id;
    auto vertex = [&](int i, int j, int k) {
        const long long key = i + (long long)(n.x() + 1) * (j + (long long)(n.y() + 1) * k);
        auto it = vertex_id.find(key);
        if (it != vertex_id.end())
            return it->second;
        const int id = int(its.vertices.size());
        its.vertices.emplace_back((origin + c * Vec3d(i, j, k)).cast<float>());
        vertex_id.emplace(key, id);
        return id;
    };
    auto is_sel = [&](int i, int j, int k) {
        return i >= 0 && j >= 0 && k >= 0 && i < n.x() && j < n.y() && k < n.z() && selected[i + size_t(n.x()) * (j + size_t(n.y()) * k)];
    };
    for (int k = 0; k < n.z(); ++ k)
        for (int j = 0; j < n.y(); ++ j)
            for (int i = 0; i < n.x(); ++ i) {
                if (! is_sel(i, j, k))
                    continue;
                // The four corners of each face, counter clockwise seen from outside.
                if (! is_sel(i - 1, j, k)) { int a = vertex(i, j, k), b = vertex(i, j, k + 1), cc = vertex(i, j + 1, k + 1), d = vertex(i, j + 1, k);
                    its.indices.emplace_back(a, b, cc); its.indices.emplace_back(a, cc, d); }
                if (! is_sel(i + 1, j, k)) { int a = vertex(i + 1, j, k), b = vertex(i + 1, j + 1, k), cc = vertex(i + 1, j + 1, k + 1), d = vertex(i + 1, j, k + 1);
                    its.indices.emplace_back(a, b, cc); its.indices.emplace_back(a, cc, d); }
                if (! is_sel(i, j - 1, k)) { int a = vertex(i, j, k), b = vertex(i + 1, j, k), cc = vertex(i + 1, j, k + 1), d = vertex(i, j, k + 1);
                    its.indices.emplace_back(a, b, cc); its.indices.emplace_back(a, cc, d); }
                if (! is_sel(i, j + 1, k)) { int a = vertex(i, j + 1, k), b = vertex(i, j + 1, k + 1), cc = vertex(i + 1, j + 1, k + 1), d = vertex(i + 1, j + 1, k);
                    its.indices.emplace_back(a, b, cc); its.indices.emplace_back(a, cc, d); }
                if (! is_sel(i, j, k - 1)) { int a = vertex(i, j, k), b = vertex(i, j + 1, k), cc = vertex(i + 1, j + 1, k), d = vertex(i + 1, j, k);
                    its.indices.emplace_back(a, b, cc); its.indices.emplace_back(a, cc, d); }
                if (! is_sel(i, j, k + 1)) { int a = vertex(i, j, k + 1), b = vertex(i + 1, j, k + 1), cc = vertex(i + 1, j + 1, k + 1), d = vertex(i, j + 1, k + 1);
                    its.indices.emplace_back(a, b, cc); its.indices.emplace_back(a, cc, d); }
            }
    return its;
}

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_CellsMesh_hpp_
