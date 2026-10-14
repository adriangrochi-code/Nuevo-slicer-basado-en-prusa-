///|/ Tisma Slicer: geometric multigrid preconditioner of the voxel structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Multigrid_hpp_
#define tisma_fea_Multigrid_hpp_

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <Eigen/Dense>

#include <libslic3r/Point.hpp>

namespace Slic3r {
namespace Fea {

// V-cycle of a geometric multigrid, used as the preconditioner of the conjugate gradient (docs/FEA.md, "Solver").
// - Level 0 is the analysis itself: its matrix free product and diagonal, so that the conjugate gradient converges
//   to the same solution as with the Jacobi preconditioner.
// - Each coarser level joins 2 x 2 x 2 elements. Its element matrix is the Galerkin product P^T K P of the elements
//   it joins, P being the trilinear interpolation of the coarse nodes: exact for the stiffness of walls, infill and
//   voids inside the coarse element, which a mean stiffness misses. Stored in single precision.
// - Damped Jacobi smoothing; the coarsest level is solved directly (sparse Cholesky).
// - A node of a coarse level is fixed when a fixed node of the finer level is next to it.
// The V-cycle is symmetric (as many smoothing sweeps before as after), so it is a valid preconditioner.
class Multigrid
{
public:
    using Matrix24d = Eigen::Matrix<double, 24, 24>;
    using Multiply  = std::function<void(const std::vector<double> &x, std::vector<double> &y)>;
    // Matrix of an element of level 0, with its stiffness.
    using ElementMatrix = std::function<Matrix24d(size_t element)>;

    struct FineLevel {
        Vec3i                           size;        // voxels of the grid
        double                          h;           // voxel size
        std::vector<Vec3i>              voxels;      // ijk of each element
        std::vector<std::array<int, 8>> elements;    // nodes of each element (NODE_OFFSETS order)
        std::vector<Vec3i>              nodes;       // ijk of each node
        std::vector<char>               fixed;       // per dof
        std::vector<double>             diag;        // diagonal of the matrix, per dof
        Multiply                        multiply;    // y = K x (fixed dofs of y set to zero)
    };

    Multigrid();
    ~Multigrid();

    // Builds the coarse levels. Returns false when cancelled or when the coarsest level cannot be factorized.
    bool build(FineLevel fine, const ElementMatrix &element_matrix, const std::function<bool()> &cancel);
    // z = M^-1 r (one V-cycle from zero).
    void apply(const std::vector<double> &r, std::vector<double> &z) const;

    size_t levels() const;
    // Degrees of freedom of the coarsest level (solved directly).
    size_t coarsest_dofs() const;

private:
    struct Level;
    std::vector<std::unique_ptr<Level>> m_levels;
    struct Direct;
    std::unique_ptr<Direct>             m_direct;

    void smooth(const Level &level, const std::vector<double> &b, std::vector<double> &x, int sweeps) const;
    void cycle(size_t l, const std::vector<double> &b, std::vector<double> &x) const;
};

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Multigrid_hpp_
