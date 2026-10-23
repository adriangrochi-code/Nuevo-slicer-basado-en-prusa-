///|/ Tisma Slicer: geometric multigrid preconditioner of the voxel structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Multigrid.hpp"
#include "Gpu.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

#include <boost/log/trivial.hpp>

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
    m_gpu.reset();
    m_levels.clear();
    m_direct.reset();
    // The matrices of the elements of level 0 for the GPU.
    FineLevel table;
    table.gpu            = fine.gpu;
    table.matrices       = std::move(fine.matrices);
    table.element_matrix = std::move(fine.element_matrix);
    table.element_scale  = std::move(fine.element_scale);
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
    if (table.gpu && ! table.matrices.empty() && ! (cancel && cancel()) && ! build_gpu(table))
        m_gpu.reset();
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
        solve_coarsest(b, x);
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

void Multigrid::solve_coarsest(const std::vector<double> &b, std::vector<double> &x) const
{
    const Level &level = *m_levels.back();
    Eigen::Map<const Eigen::VectorXd> bv(b.data(), Eigen::Index(b.size()));
    Eigen::VectorXd bf = bv;
    for (size_t d = 0; d < b.size(); ++ d)
        if (level.fixed[d])
            bf[Eigen::Index(d)] = 0.;
    const Eigen::VectorXd xv = m_direct->solver.solve(bf);
    x.resize(b.size());
    for (size_t d = 0; d < x.size(); ++ d)
        x[d] = level.fixed[d] ? 0. : xv[Eigen::Index(d)];
}

void Multigrid::apply(const std::vector<double> &r, std::vector<double> &z) const
{
    if (m_gpu && apply_gpu(r, z))
        return;
    z.assign(r.size(), 0.);
    cycle(0, r, z);
}

// ---------------------------------------------------------------------------------------------------------------------
// The V-cycle on the GPU: the same steps as cycle() in single precision, with the shaders of src/libtisma_fea/src/gpu
// (fea_apply: a Jacobi sweep or the residual, fea_restrict, fea_prolong). The command buffers are recorded once: the
// first one uploads the residual and goes down to the coarsest level, whose right hand side is read back and solved
// on the CPU; the second one uploads its solution, goes up and the result is read back.

struct Multigrid::Gpu
{
    gpu::Context *ctx { nullptr };
    struct LevelBuffers {
        uint32_t    nodes { 0 };
        float       damping { 0.f };
        bool        per_element { false };
        gpu::Buffer elem_nodes, elem_mat, matrices, node_adj, free, dinv, x, b, t;
        // Interpolation from the next coarser level (prolongation) and restriction to it (gathered on its nodes).
        gpu::Buffer parents, weights, offsets, children, child_weights;
        VkDescriptorSet apply_xt { VK_NULL_HANDLE }, apply_tx { VK_NULL_HANDLE }, restrict_set { VK_NULL_HANDLE }, prolong_set { VK_NULL_HANDLE };
    };
    std::vector<LevelBuffers> levels;
    gpu::Buffer      dummy;
    gpu::Buffer      staging_r, staging_z, staging_cb, staging_cx;
    VkDescriptorPool pool { VK_NULL_HANDLE };
    VkCommandBuffer  down { VK_NULL_HANDLE }, up { VK_NULL_HANDLE };

    ~Gpu()
    {
        if (ctx == nullptr)
            return;
        std::lock_guard<std::mutex> lock(ctx->mutex());
        ctx->free_command_buffer(down);
        ctx->free_command_buffer(up);
        if (pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(ctx->device(), pool, nullptr);
        for (LevelBuffers &l : levels)
            for (gpu::Buffer *b : { &l.elem_nodes, &l.elem_mat, &l.matrices, &l.node_adj, &l.free, &l.dinv, &l.x, &l.b, &l.t,
                                    &l.parents, &l.weights, &l.offsets, &l.children, &l.child_weights })
                ctx->destroy_buffer(*b);
        for (gpu::Buffer *b : { &dummy, &staging_r, &staging_z, &staging_cb, &staging_cx })
            ctx->destroy_buffer(*b);
    }
};

namespace {

template<class T> bool device_buffer(gpu::Context &ctx, const std::vector<T> &data, gpu::Buffer &out)
{
    const size_t bytes = std::max<size_t>(data.size() * sizeof(T), 4);
    if (bytes > ctx.max_storage_buffer_range() || ! ctx.create_buffer(bytes, false, out))
        return false;
    return data.empty() || ctx.upload(out, data.data(), data.size() * sizeof(T));
}

VkDescriptorSet descriptor_set(gpu::Context &ctx, VkDescriptorPool pool, gpu::Kernel kernel, std::initializer_list<const gpu::Buffer *> buffers)
{
    const gpu::Context::Pipeline &p = ctx.pipeline(kernel);
    VkDescriptorSetAllocateInfo ai { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &p.set_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(ctx.device(), &ai, &set) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    std::vector<VkDescriptorBufferInfo> infos;
    infos.reserve(buffers.size());
    for (const gpu::Buffer *b : buffers)
        infos.push_back({ b->buffer, 0, VK_WHOLE_SIZE });
    std::vector<VkWriteDescriptorSet> writes(infos.size());
    for (size_t i = 0; i < infos.size(); ++ i) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = set;
        writes[i].dstBinding      = uint32_t(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo     = &infos[i];
    }
    vkUpdateDescriptorSets(ctx.device(), uint32_t(writes.size()), writes.data(), 0, nullptr);
    return set;
}

struct ApplyParams { uint32_t nodes; uint32_t per_element; float c0; float c1; uint32_t use_dinv; };

void dispatch(gpu::Context &ctx, VkCommandBuffer cmd, gpu::Kernel kernel, VkDescriptorSet set, const void *push, uint32_t push_size, uint32_t items)
{
    const gpu::Context::Pipeline &p = ctx.pipeline(kernel);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push);
    vkCmdDispatch(cmd, (items + 63) / 64, 1, 1);
    gpu::barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
}

void copy(VkCommandBuffer cmd, const gpu::Buffer &src, const gpu::Buffer &dst, VkDeviceSize size)
{
    VkBufferCopy region { 0, 0, size };
    vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &region);
    gpu::barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT);
}

} // namespace

bool Multigrid::build_gpu(const FineLevel &table)
{
    gpu::Context *ctx = gpu::Context::get();
    if (ctx == nullptr || m_levels.size() < 2)
        return false;
    // Declared before the lock: when the GPU cannot be used, it is released (which locks the context) after it.
    auto g = std::make_unique<Gpu>();
    g->ctx = ctx;
    std::lock_guard<std::mutex> lock(ctx->mutex());
    const size_t n_levels = m_levels.size();
    // The coarsest level is solved on the CPU: the GPU levels are all the others, plus the vectors of the coarsest.
    g->levels.resize(n_levels);
    for (size_t l = 0; l < n_levels; ++ l) {
        const Level   &level = *m_levels[l];
        Gpu::LevelBuffers &lb = g->levels[l];
        lb.nodes   = uint32_t(level.nodes.size());
        lb.damping = float(level.damping);
        const size_t dofs = level.dofs();
        for (gpu::Buffer *b : { &lb.x, &lb.b, &lb.t })
            if (! ctx->create_buffer(dofs * sizeof(float), false, *b))
                return false;
        std::vector<float> free_dof(dofs), dinv(dofs);
        for (size_t d = 0; d < dofs; ++ d) {
            free_dof[d] = level.fixed[d] ? 0.f : 1.f;
            dinv[d]     = level.diag[d] > 0. ? float(1. / level.diag[d]) : 0.f;
        }
        if (! device_buffer(*ctx, free_dof, lb.free) || ! device_buffer(*ctx, dinv, lb.dinv))
            return false;
        if (l + 1 == n_levels)
            break;
        // Elements, their matrices and the elements around every node.
        std::vector<int> elem_nodes(level.elements.size() * 8), node_adj(level.nodes.size() * 8, -1);
        std::vector<uint8_t> adj_count(level.nodes.size(), 0);
        for (size_t e = 0; e < level.elements.size(); ++ e)
            for (int a = 0; a < 8; ++ a) {
                const int n = level.elements[e][a];
                elem_nodes[8 * e + a] = n;
                if (adj_count[n] < 8)
                    node_adj[8 * size_t(n) + adj_count[n] ++] = int(8 * e + a);
            }
        if (! device_buffer(*ctx, elem_nodes, lb.elem_nodes) || ! device_buffer(*ctx, node_adj, lb.node_adj))
            return false;
        std::vector<float> matrices;
        if (l == 0) {
            if (table.element_matrix.size() != level.elements.size() || table.element_scale.size() != level.elements.size())
                return false;
            matrices.reserve(table.matrices.size() * 576);
            for (const Matrix24d &m : table.matrices)
                for (int r = 0; r < 24; ++ r)
                    for (int c = 0; c < 24; ++ c)
                        matrices.push_back(float(m(r, c)));
            struct ElementMatrix { int ma; int mb; float sa; float sb; };
            std::vector<ElementMatrix> elem_mat(level.elements.size());
            for (size_t e = 0; e < elem_mat.size(); ++ e)
                elem_mat[e] = { table.element_matrix[e][0], table.element_matrix[e][1], table.element_scale[e][0], table.element_scale[e][1] };
            if (! device_buffer(*ctx, elem_mat, lb.elem_mat))
                return false;
        } else {
            lb.per_element = true;
            matrices.reserve(level.kmat.size() * 576);
            for (const Matrix24f &m : level.kmat)
                for (int r = 0; r < 24; ++ r)
                    for (int c = 0; c < 24; ++ c)
                        matrices.push_back(m(r, c));
        }
        if (! device_buffer(*ctx, matrices, lb.matrices))
            return false;
        // Transfers to the next coarser level.
        const Level &coarse = *m_levels[l + 1];
        std::vector<int>   parents(level.nodes.size() * 8, -1);
        std::vector<float> weights(level.nodes.size() * 8, 0.f);
        std::vector<std::vector<std::pair<int, float>>> children(coarse.nodes.size());
        for (size_t n = 0; n < level.nodes.size(); ++ n)
            for (int q = 0; q < 8 && level.parents[n][q] >= 0; ++ q) {
                parents[8 * n + q] = level.parents[n][q];
                weights[8 * n + q] = level.weights[n][q];
                children[level.parents[n][q]].emplace_back(int(n), level.weights[n][q]);
            }
        std::vector<uint32_t> offsets(coarse.nodes.size() + 1, 0);
        std::vector<int>      child_nodes;
        std::vector<float>    child_weights;
        for (size_t c = 0; c < children.size(); ++ c) {
            for (const auto &[n, w] : children[c]) {
                child_nodes.push_back(n);
                child_weights.push_back(w);
            }
            offsets[c + 1] = uint32_t(child_nodes.size());
        }
        if (! device_buffer(*ctx, parents, lb.parents) || ! device_buffer(*ctx, weights, lb.weights) ||
            ! device_buffer(*ctx, offsets, lb.offsets) || ! device_buffer(*ctx, child_nodes, lb.children) ||
            ! device_buffer(*ctx, child_weights, lb.child_weights))
            return false;
    }
    if (! ctx->create_buffer(4, false, g->dummy))
        return false;
    const size_t fine_bytes   = m_levels.front()->dofs() * sizeof(float);
    const size_t coarse_bytes = m_levels.back()->dofs() * sizeof(float);
    if (! ctx->create_buffer(fine_bytes, true, g->staging_r) || ! ctx->create_buffer(fine_bytes, true, g->staging_z) ||
        ! ctx->create_buffer(coarse_bytes, true, g->staging_cb) || ! ctx->create_buffer(coarse_bytes, true, g->staging_cx))
        return false;

    // Descriptor sets.
    const uint32_t gpu_levels = uint32_t(n_levels - 1);
    VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gpu_levels * (2 * 9 + 6 + 5) };
    VkDescriptorPoolCreateInfo dpi { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets       = gpu_levels * 4;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes    = &pool_size;
    if (vkCreateDescriptorPool(ctx->device(), &dpi, nullptr, &g->pool) != VK_SUCCESS)
        return false;
    for (size_t l = 0; l + 1 < n_levels; ++ l) {
        Gpu::LevelBuffers &lb = g->levels[l];
        Gpu::LevelBuffers &cb = g->levels[l + 1];
        const gpu::Buffer *elem_mat = lb.per_element ? &g->dummy : &lb.elem_mat;
        lb.apply_xt = descriptor_set(*ctx, g->pool, gpu::Kernel::Apply, { &lb.elem_nodes, elem_mat, &lb.matrices, &lb.node_adj, &lb.free, &lb.dinv, &lb.x, &lb.b, &lb.t });
        lb.apply_tx = descriptor_set(*ctx, g->pool, gpu::Kernel::Apply, { &lb.elem_nodes, elem_mat, &lb.matrices, &lb.node_adj, &lb.free, &lb.dinv, &lb.t, &lb.b, &lb.x });
        lb.restrict_set = descriptor_set(*ctx, g->pool, gpu::Kernel::Restrict, { &lb.offsets, &lb.children, &lb.child_weights, &cb.free, &lb.t, &cb.b });
        lb.prolong_set  = descriptor_set(*ctx, g->pool, gpu::Kernel::Prolong, { &lb.parents, &lb.weights, &lb.free, &cb.x, &lb.x });
        if (! lb.apply_xt || ! lb.apply_tx || ! lb.restrict_set || ! lb.prolong_set)
            return false;
    }

    // Command buffers.
    g->down = ctx->allocate_command_buffer();
    g->up   = ctx->allocate_command_buffer();
    if (g->down == VK_NULL_HANDLE || g->up == VK_NULL_HANDLE)
        return false;
    VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    auto smooth_sweeps = [&](VkCommandBuffer cmd, const Gpu::LevelBuffers &lb) {
        // SMOOTH_SWEEPS (2) Jacobi sweeps: x -> t -> x.
        static_assert(SMOOTH_SWEEPS == 2, "the sweeps alternate between x and t");
        const ApplyParams p { lb.nodes, lb.per_element ? 1u : 0u, 1.f, lb.damping, 1u };
        dispatch(*ctx, cmd, gpu::Kernel::Apply, lb.apply_xt, &p, sizeof(p), lb.nodes);
        dispatch(*ctx, cmd, gpu::Kernel::Apply, lb.apply_tx, &p, sizeof(p), lb.nodes);
    };
    vkBeginCommandBuffer(g->down, &bi);
    copy(g->down, g->staging_r, g->levels[0].b, fine_bytes);
    for (size_t l = 0; l + 1 < n_levels; ++ l) {
        const Gpu::LevelBuffers &lb = g->levels[l];
        vkCmdFillBuffer(g->down, lb.x.buffer, 0, VK_WHOLE_SIZE, 0);
        gpu::barrier(g->down, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        smooth_sweeps(g->down, lb);
        const ApplyParams residual { lb.nodes, lb.per_element ? 1u : 0u, 0.f, 1.f, 0u };
        dispatch(*ctx, g->down, gpu::Kernel::Apply, lb.apply_xt, &residual, sizeof(residual), lb.nodes);
        const uint32_t coarse_nodes = g->levels[l + 1].nodes;
        dispatch(*ctx, g->down, gpu::Kernel::Restrict, lb.restrict_set, &coarse_nodes, sizeof(coarse_nodes), coarse_nodes);
    }
    copy(g->down, g->levels.back().b, g->staging_cb, coarse_bytes);
    if (vkEndCommandBuffer(g->down) != VK_SUCCESS)
        return false;
    vkBeginCommandBuffer(g->up, &bi);
    copy(g->up, g->staging_cx, g->levels.back().x, coarse_bytes);
    for (size_t l = n_levels - 1; l-- > 0;) {
        const Gpu::LevelBuffers &lb = g->levels[l];
        dispatch(*ctx, g->up, gpu::Kernel::Prolong, lb.prolong_set, &lb.nodes, sizeof(lb.nodes), lb.nodes);
        smooth_sweeps(g->up, lb);
    }
    copy(g->up, g->levels.front().x, g->staging_z, fine_bytes);
    if (vkEndCommandBuffer(g->up) != VK_SUCCESS)
        return false;
    m_gpu = std::move(g);
    return true;
}

bool Multigrid::apply_gpu(const std::vector<double> &r, std::vector<double> &z) const
{
    Gpu &g = *m_gpu;
    std::lock_guard<std::mutex> lock(g.ctx->mutex());
    float *rf = static_cast<float *>(g.staging_r.mapped);
    for (size_t d = 0; d < r.size(); ++ d)
        rf[d] = float(r[d]);
    if (! g.ctx->submit_and_wait(g.down))
        return false;
    const Level &coarse = *m_levels.back();
    std::vector<double> bc(coarse.dofs()), xc;
    const float *bcf = static_cast<const float *>(g.staging_cb.mapped);
    for (size_t d = 0; d < bc.size(); ++ d)
        bc[d] = double(bcf[d]);
    solve_coarsest(bc, xc);
    float *xcf = static_cast<float *>(g.staging_cx.mapped);
    for (size_t d = 0; d < xc.size(); ++ d)
        xcf[d] = float(xc[d]);
    if (! g.ctx->submit_and_wait(g.up))
        return false;
    const float *zf = static_cast<const float *>(g.staging_z.mapped);
    z.resize(r.size());
    for (size_t d = 0; d < z.size(); ++ d)
        z[d] = double(zf[d]);
    return true;
}

} // namespace Fea
} // namespace Slic3r
