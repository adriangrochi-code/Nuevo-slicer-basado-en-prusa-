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
// On a GPU (Vulkan, Gpu.hpp) the V-cycle runs in single precision but for the direct solve of the coarsest level, which
// stays on the CPU (docs/FEA.md, "GPU").
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
        // For the GPU: the matrix of element e is element_scale[e][0] * matrices[element_matrix[e][0]] +
        // element_scale[e][1] * matrices[element_matrix[e][1]] (an index -1: no second matrix). Empty: no GPU.
        bool                                gpu { false };
        std::vector<Matrix24d>              matrices;
        std::vector<std::array<int, 2>>     element_matrix;
        std::vector<std::array<float, 2>>   element_scale;
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
    // The V-cycle runs on the GPU.
    bool on_gpu() const { return m_gpu != nullptr; }

private:
    struct Level;
    std::vector<std::unique_ptr<Level>> m_levels;
    struct Direct;
    std::unique_ptr<Direct>             m_direct;

    struct Gpu;
    std::unique_ptr<Gpu>                m_gpu;

    void smooth(const Level &level, const std::vector<double> &b, std::vector<double> &x, int sweeps) const;
    void cycle(size_t l, const std::vector<double> &b, std::vector<double> &x) const;
    // Solution of the coarsest level (fixed dofs of b ignored, of x zero).
    void solve_coarsest(const std::vector<double> &b, std::vector<double> &x) const;
    bool build_gpu(const FineLevel &table);
    bool apply_gpu(const std::vector<double> &r, std::vector<double> &z) const;
};

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Multigrid_hpp_
