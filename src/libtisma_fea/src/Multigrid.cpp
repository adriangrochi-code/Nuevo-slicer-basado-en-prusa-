///|/ Tisma Slicer: geometric multigrid preconditioner of the voxel structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Multigrid.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace Slic3r {
namespace Fea {

namespace {

constexpr int NODE_OFFSETS[8][3] = { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1} };
// The coarsest level is solved directly when it has at most this many degrees of freedom.
constexpr size_t DIRECT_DOFS = 15000;
// Sweeps of the Jacobi smoother; its damping is 4 / (3 lambda_max) of each level, lambda_max being the largest
// eigenvalue of D^-1 A (estimated with the power method): a fixed damping diverges where the walls and the infill make
// lambda_max large, and the preconditioner is no longer positive definite.
constexpr int    SMOOTH_SWEEPS  = 2;
constexpr int    POWER_STEPS    = 12;

template<class F> void parallel(size_t n, F &&f)
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, n, 4096), [&f](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); ++ i)
            f(i);
    });
}

} // namespace

using Matrix24f = Eigen::Matrix<float, 24, 24>;

struct Multigrid::Level
{
    Vec3i                           size;
    double                          h { 0. };
    std::vector<Vec3i>              voxels;
    std::vector<std::array<int, 8>> elements;
    std::vector<Vec3i>              nodes;
    std::vector<char>               fixed;
    std::vector<double>             diag;
    double                          damping { 0.6 };
    Multiply                        multiply;     // level 0
    // Coarse levels: the matrix of each element, groups of elements without shared nodes.
    std::vector<Matrix24f>          kmat;
    std::array<std::vector<int>, 8> colors;
    // Transfer to the next coarser level: the coarse nodes of each node of this level and their weights.
    std::vector<std::array<int, 8>>   parents;
    std::vector<std::array<float, 8>> weights;

    size_t dofs() const { return 3 * nodes.size(); }

    void apply(const std::vector<double> &x, std::vector<double> &y) const
    {
        if (multiply) {
            multiply(x, y);
            return;
        }
        std::fill(y.begin(), y.end(), 0.);
        for (const std::vector<int> &color : colors)
            tbb::parallel_for(tbb::blocked_range<size_t>(0, color.size(), 256), [&](const tbb::blocked_range<size_t> &range) {
                Eigen::Matrix<float, 24, 1> xe, ye;
                for (size_t ci = range.begin(); ci < range.end(); ++ ci) {
                    const int e = color[ci];
                    const std::array<int, 8> &n = elements[e];
                    for (int a = 0; a < 8; ++ a)
                        for (int k = 0; k < 3; ++ k)
                            xe[3 * a + k] = float(x[3 * n[a] + k]);
                    ye.noalias() = kmat[e] * xe;
                    for (int a = 0; a < 8; ++ a)
                        for (int k = 0; k < 3; ++ k)
                            y[3 * n[a] + k] += double(ye[3 * a + k]);
                }
            });
        for (size_t d = 0; d < y.size(); ++ d)
            if (fixed[d])
                y[d] = 0.;
    }
};

struct Multigrid::Direct
{
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
};

Multigrid::Multigrid() = default;
Multigrid::~Multigrid() = default;

size_t Multigrid::levels() const { return m_levels.size(); }
size_t Multigrid::coarsest_dofs() const { return m_levels.empty() ? 0 : m_levels.back()->dofs(); }

// Interpolation of the nodes of the element of octant (ox, oy, oz) of a coarse element from its 8 nodes: P(3a+k, 3b+k)
// is the trilinear shape function of coarse node b at fine node a.
static Multigrid::Matrix24d octant_interpolation(int ox, int oy, int oz)
{
    Multigrid::Matrix24d p = Multigrid::Matrix24d::Zero();
    for (int a = 0; a < 8; ++ a) {
        const double xi[3] = { 0.5 * (ox + NODE_OFFSETS[a][0]), 0.5 * (oy + NODE_OFFSETS[a][1]), 0.5 * (oz + NODE_OFFSETS[a][2]) };
        for (int b = 0; b < 8; ++ b) {
            double n = 1.;
            for (int ax = 0; ax < 3; ++ ax)
                n *= NODE_OFFSETS[b][ax] ? xi[ax] : 1. - xi[ax];
            for (int k = 0; k < 3; ++ k)
                p(3 * a + k, 3 * b + k) = n;
        }
    }
    return p;
}

bool Multigrid::build(FineLevel fine, const ElementMatrix &element_matrix, const std::function<bool()> &cancel)
{
    m_levels.clear();
    m_direct.reset();
    {
        auto level = std::make_unique<Level>();
        level->size     = fine.size;
        level->h        = fine.h;
        level->voxels   = std::move(fine.voxels);
        level->elements = std::move(fine.elements);
        level->nodes    = std::move(fine.nodes);
        level->fixed    = std::move(fine.fixed);
        level->diag     = std::move(fine.diag);
        level->multiply = std::move(fine.multiply);
        m_levels.emplace_back(std::move(level));
    }
    std::array<Matrix24d, 8> interpolation;
    for (int o = 0; o < 8; ++ o)
        interpolation[o] = octant_interpolation(o & 1, (o >> 1) & 1, (o >> 2) & 1);

    while (m_levels.back()->dofs() > DIRECT_DOFS && m_levels.size() < 10) {
        if (cancel && cancel())
            return false;
        Level &f = *m_levels.back();
        auto   c = std::make_unique<Level>();
        c->size = Vec3i((f.size.x() + 1) / 2, (f.size.y() + 1) / 2, (f.size.z() + 1) / 2);
        c->h    = 2. * f.h;

        // Coarse elements and the fine elements they join.
        std::unordered_map<long long, int>    element_of;
        std::vector<std::vector<int>>         children;
        element_of.reserve(f.voxels.size() / 4);
        auto voxel_key = [&c](const Vec3i &v) { return v.x() + (long long)c->size.x() * (v.y() + (long long)c->size.y() * v.z()); };
        for (size_t e = 0; e < f.voxels.size(); ++ e) {
            const Vec3i cv(f.voxels[e].x() / 2, f.voxels[e].y() / 2, f.voxels[e].z() / 2);
            auto [it, added] = element_of.try_emplace(voxel_key(cv), int(c->voxels.size()));
            if (added) {
                c->voxels.push_back(cv);
                children.emplace_back();
            }
            children[it->second].push_back(int(e));
        }
        if (c->voxels.size() * 2 > f.voxels.size() && m_levels.size() > 1)
            // Not coarsening any more (very thin part).
            break;

        // Galerkin matrices of the coarse elements.
        c->kmat.resize(c->voxels.size());
        const bool fine_is_level0 = m_levels.size() == 1;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, c->voxels.size(), 64), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t ce = range.begin(); ce < range.end(); ++ ce) {
                Matrix24d k = Matrix24d::Zero();
                for (int e : children[ce]) {
                    const int o = (f.voxels[e].x() & 1) | ((f.voxels[e].y() & 1) << 1) | ((f.voxels[e].z() & 1) << 2);
                    const Matrix24d ke = fine_is_level0 ? element_matrix(size_t(e)) : Matrix24d(f.kmat[e].cast<double>());
                    k.noalias() += interpolation[o].transpose() * ke * interpolation[o];
                }
                c->kmat[ce] = k.cast<float>();
            }
        });
        for (size_t e = 0; e < c->voxels.size(); ++ e)
            c->colors[(c->voxels[e].x() & 1) | ((c->voxels[e].y() & 1) << 1) | ((c->voxels[e].z() & 1) << 2)].push_back(int(e));

        // Coarse nodes.
        std::unordered_map<long long, int> node_of;
        node_of.reserve(c->voxels.size() * 2);
        auto node_key = [&c](int i, int j, int k) { return i + (long long)(c->size.x() + 1) * (j + (long long)(c->size.y() + 1) * k); };
        c->elements.resize(c->voxels.size());
        for (size_t e = 0; e < c->voxels.size(); ++ e)
            for (int a = 0; a < 8; ++ a) {
                const int i = c->voxels[e].x() + NODE_OFFSETS[a][0], j = c->voxels[e].y() + NODE_OFFSETS[a][1], k = c->voxels[e].z() + NODE_OFFSETS[a][2];
                auto [it, added] = node_of.try_emplace(node_key(i, j, k), int(c->nodes.size()));
                if (added)
                    c->nodes.emplace_back(i, j, k);
                c->elements[e][a] = it->second;
            }

        // Trilinear interpolation of each fine node from the coarse nodes (the same as in the Galerkin products);
        // fixed coarse nodes.
        c->fixed.assign(c->dofs(), 0);
        f.parents.resize(f.nodes.size());
        f.weights.resize(f.nodes.size());
        for (size_t n = 0; n < f.nodes.size(); ++ n) {
            const Vec3i &p = f.nodes[n];
            int   ci[3][2];
            float cw[3][2];
            int   cnt[3];
            for (int ax = 0; ax < 3; ++ ax) {
                ci[ax][0] = p[ax] / 2;
                if (p[ax] % 2 == 0) {
                    cw[ax][0] = 1.f;
                    cnt[ax]   = 1;
                } else {
                    ci[ax][1] = p[ax] / 2 + 1;
                    cw[ax][0] = cw[ax][1] = 0.5f;
                    cnt[ax]   = 2;
                }
            }
            std::array<int, 8>   par;
            std::array<float, 8> w;
            par.fill(-1);
            w.fill(0.f);
            int m = 0;
            for (int a = 0; a < cnt[0]; ++ a)
                for (int b = 0; b < cnt[1]; ++ b)
                    for (int d = 0; d < cnt[2]; ++ d) {
                        auto it = node_of.find(node_key(ci[0][a], ci[1][b], ci[2][d]));
                        if (it == node_of.end())
                            continue;
                        par[m] = it->second;
                        w[m]   = cw[0][a] * cw[1][b] * cw[2][d];
                        ++ m;
                    }
            f.parents[n] = par;
            f.weights[n] = w;
            if (f.fixed[3 * n])
                for (int q = 0; q < m; ++ q)
                    c->fixed[3 * par[q]] = c->fixed[3 * par[q] + 1] = c->fixed[3 * par[q] + 2] = 1;
        }

        // Diagonal.
        c->diag.assign(c->dofs(), 0.);
        for (size_t e = 0; e < c->voxels.size(); ++ e)
            for (int a = 0; a < 8; ++ a)
                for (int k = 0; k < 3; ++ k)
                    c->diag[3 * c->elements[e][a] + k] += double(c->kmat[e](3 * a + k, 3 * a + k));
        for (size_t d = 0; d < c->dofs(); ++ d)
            if (! (c->diag[d] > 0.))
                c->diag[d] = 1., c->fixed[d] = 1;
        m_levels.emplace_back(std::move(c));
    }

    if (m_levels.size() == 1)
        // A small part (or one which does not coarsen): the Jacobi preconditioner is enough.
        return false;

    // Damping of the smoother of each level (but the coarsest).
    for (size_t l = 0; l + 1 < m_levels.size(); ++ l) {
        Level &level = *m_levels[l];
        std::vector<double> v(level.dofs()), av(level.dofs());
        for (size_t d = 0; d < v.size(); ++ d)
            v[d] = level.fixed[d] ? 0. : 1. + 0.37 * double((d * 7919) % 13);
        double lambda = 1.;
        for (int it = 0; it < POWER_STEPS; ++ it) {
            double norm = 0.;
            for (double x : v)
                norm += x * x;
            norm = std::sqrt(norm);
            if (norm == 0.)
                break;
            for (double &x : v)
                x /= norm;
            level.apply(v, av);
            double vav = 0.;
            for (size_t d = 0; d < v.size(); ++ d) {
                av[d] = level.fixed[d] ? 0. : av[d] / level.diag[d];
                vav  += v[d] * av[d];
            }
            lambda = vav;
            v.swap(av);
        }
        // The power method approaches lambda_max from below: a margin.
        level.damping = 4. / (3. * 1.1 * std::max(lambda, 1.));
        if (cancel && cancel())
            return false;
    }

    // The coarsest level, assembled and factorized.
    const Level &last = *m_levels.back();
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(last.voxels.size() * 300);
    for (size_t e = 0; e < last.voxels.size(); ++ e) {
        const Matrix24f &ke = last.kmat[e];
        for (int a = 0; a < 24; ++ a) {
            const int ra = 3 * last.elements[e][a / 3] + a % 3;
            if (last.fixed[ra])
                continue;
            for (int b = 0; b < 24; ++ b) {
                const int rb = 3 * last.elements[e][b / 3] + b % 3;
                if (! last.fixed[rb])
                    triplets.emplace_back(ra, rb, double(ke(a, b)));
            }
        }
    }
    for (size_t d = 0; d < last.dofs(); ++ d)
        if (last.fixed[d])
            triplets.emplace_back(int(d), int(d), 1.);
    Eigen::SparseMatrix<double> a(int(last.dofs()), int(last.dofs()));
    a.setFromTriplets(triplets.begin(), triplets.end());
    m_direct = std::make_unique<Direct>();
    m_direct->solver.compute(a);
    if (m_direct->solver.info() != Eigen::Success) {
        m_direct.reset();
        return false;
    }
    return true;
}

void Multigrid::smooth(const Level &level, const std::vector<double> &b, std::vector<double> &x, int sweeps) const
{
    std::vector<double> ax(x.size());
    for (int s = 0; s < sweeps; ++ s) {
        level.apply(x, ax);
        parallel(x.size(), [&](size_t d) {
            if (! level.fixed[d])
                x[d] += level.damping * (b[d] - ax[d]) / level.diag[d];
        });
    }
}

void Multigrid::cycle(size_t l, const std::vector<double> &b, std::vector<double> &x) const
{
    const Level &level = *m_levels[l];
    if (l + 1 == m_levels.size()) {
        Eigen::Map<const Eigen::VectorXd> bv(b.data(), Eigen::Index(b.size()));
        Eigen::VectorXd bf = bv;
        for (size_t d = 0; d < b.size(); ++ d)
            if (level.fixed[d])
                bf[Eigen::Index(d)] = 0.;
        const Eigen::VectorXd xv = m_direct->solver.solve(bf);
        for (size_t d = 0; d < x.size(); ++ d)
            x[d] = level.fixed[d] ? 0. : xv[Eigen::Index(d)];
        return;
    }
    std::fill(x.begin(), x.end(), 0.);
    smooth(level, b, x, SMOOTH_SWEEPS);
    // Residual, restricted to the coarse level.
    std::vector<double> r(x.size());
    level.apply(x, r);
    parallel(r.size(), [&](size_t d) { r[d] = level.fixed[d] ? 0. : b[d] - r[d]; });
    const Level &coarse = *m_levels[l + 1];
    std::vector<double> rc(coarse.dofs(), 0.), xc(coarse.dofs(), 0.);
    for (size_t n = 0; n < level.nodes.size(); ++ n)
        for (int q = 0; q < 8 && level.parents[n][q] >= 0; ++ q)
            for (int k = 0; k < 3; ++ k)
                rc[3 * level.parents[n][q] + k] += level.weights[n][q] * r[3 * n + k];
    for (size_t d = 0; d < rc.size(); ++ d)
        if (coarse.fixed[d])
            rc[d] = 0.;
    cycle(l + 1, rc, xc);
    // Correction interpolated to this level.
    parallel(level.nodes.size(), [&](size_t n) {
        for (int k = 0; k < 3; ++ k) {
            if (level.fixed[3 * n + k])
                continue;
            double v = 0.;
            for (int q = 0; q < 8 && level.parents[n][q] >= 0; ++ q)
                v += level.weights[n][q] * xc[3 * level.parents[n][q] + k];
            x[3 * n + k] += v;
        }
    });
    smooth(level, b, x, SMOOTH_SWEEPS);
}

void Multigrid::apply(const std::vector<double> &r, std::vector<double> &z) const
{
    z.assign(r.size(), 0.);
    cycle(0, r, z);
}

} // namespace Fea
} // namespace Slic3r
