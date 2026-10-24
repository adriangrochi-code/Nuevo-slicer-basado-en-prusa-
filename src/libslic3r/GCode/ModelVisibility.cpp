#include <boost/log/trivial.hpp>
#include <igl/Hit.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>

#include "libslic3r/ShortEdgeCollapse.hpp"
#include "libslic3r/GCode/ModelVisibility.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "admesh/stl.h"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/OpenCL.hpp"

namespace Slic3r::ModelInfo {
namespace Impl {

CoordinateFunctor::CoordinateFunctor(const std::vector<Vec3f> *coords) : coordinates(coords) {}
CoordinateFunctor::CoordinateFunctor() : coordinates(nullptr) {}

const float &CoordinateFunctor::operator()(size_t idx, size_t dim) const {
    return coordinates->operator[](idx)[dim];
}


template<typename T> int sgn(T val) {
    return int(T(0) < val) - int(val < T(0));
}

/// Coordinate frame
class Frame {
public:
    Frame() {
        mX = Vec3f(1, 0, 0);
        mY = Vec3f(0, 1, 0);
        mZ = Vec3f(0, 0, 1);
    }

    Frame(const Vec3f &x, const Vec3f &y, const Vec3f &z) :
            mX(x), mY(y), mZ(z) {
    }

    void set_from_z(const Vec3f &z) {
        mZ = z.normalized();
        Vec3f tmpZ = mZ;
        Vec3f tmpX = (std::abs(tmpZ.x()) > 0.99f) ? Vec3f(0, 1, 0) : Vec3f(1, 0, 0);
        mY = (tmpZ.cross(tmpX)).normalized();
        mX = mY.cross(tmpZ);
    }

    Vec3f to_world(const Vec3f &a) const {
        return a.x() * mX + a.y() * mY + a.z() * mZ;
    }

    Vec3f to_local(const Vec3f &a) const {
        return Vec3f(mX.dot(a), mY.dot(a), mZ.dot(a));
    }

    const Vec3f& binormal() const {
        return mX;
    }

    const Vec3f& tangent() const {
        return mY;
    }

    const Vec3f& normal() const {
        return mZ;
    }

private:
    Vec3f mX, mY, mZ;
};

Vec3f sample_sphere_uniform(const Vec2f &samples) {
    float term1 = 2.0f * float(PI) * samples.x();
    float term2 = 2.0f * sqrt(samples.y() - samples.y() * samples.y());
    return {cos(term1) * term2, sin(term1) * term2,
        1.0f - 2.0f * samples.y()};
}

Vec3f sample_hemisphere_uniform(const Vec2f &samples) {
    float term1 = 2.0f * float(PI) * samples.x();
    float term2 = 2.0f * sqrt(samples.y() - samples.y() * samples.y());
    return {cos(term1) * term2, sin(term1) * term2,
        abs(1.0f - 2.0f * samples.y())};
}

Vec3f sample_power_cosine_hemisphere(const Vec2f &samples, float power) {
    float term1 = 2.f * float(PI) * samples.x();
    float term2 = pow(samples.y(), 1.f / (power + 1.f));
    float term3 = sqrt(1.f - term2 * term2);

    return Vec3f(cos(term1) * term3, sin(term1) * term3, term2);
}

// Tisma (phase 8): the rays of raycast_visibility() on the graphics card (OpenCL.hpp), one work item per ray: the same
// walk of the implicit balanced AABB tree (the children of node i are 2i+1 and 2i+2, left first) and the same ray-box and
// ray-triangle tests as AABBTreeIndirect.hpp. On a card with double precision (TISMA_DOUBLE) the arithmetic is the one of
// the CPU, operation by operation, so that every ray ends as there; in single precision a ray grazing an edge may end
// otherwise. The rays themselves (origins and directions) are made on the CPU exactly as there, and the result of each
// sample is summed the same way.
const char *visibility_kernel_body = R"CL(
#pragma OPENCL FP_CONTRACT OFF
#ifdef TISMA_DOUBLE
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
typedef double  real;
typedef double3 real3;
#define convert_real3 convert_double3
#else
typedef float  real;
typedef float3 real3;
#define convert_real3
#endif
#define NODE_UNUSED -1
#define NODE_INNER  -2

// The arithmetic of Eigen, in its order: Vec3d::dot() adds the first two products first (one SSE2 pair), Vec3f::dot()
// the last two.
real dot_d(real3 a, real3 b) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }
float dot_f(float3 a, float3 b) { return a.x * b.x + (a.y * b.y + a.z * b.z); }
real3 cross_d(real3 a, real3 b) { return (real3)(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
real3 load(__global const float *p, int i) { return convert_real3(vload3(i, p)); }

// ray_box_intersect_invdir() with t0 = 0.
bool ray_box(real3 o, real3 inv, __global const float *box, real t1)
{
    real minx = box[0], miny = box[1], minz = box[2], maxx = box[3], maxy = box[4], maxz = box[5], t;
    if (inv.x < 0) { t = minx; minx = maxx; maxx = t; }
    if (inv.y < 0) { t = miny; miny = maxy; maxy = t; }
    real tmin  = (minx - o.x) * inv.x;
    real tymax = (maxy - o.y) * inv.y;
    if (tmin > tymax)
        return false;
    real tmax  = (maxx - o.x) * inv.x;
    real tymin = (miny - o.y) * inv.y;
    if (tymin > tmax)
        return false;
    if (tymin > tmin)
        tmin = tymin;
    if (tymax < tmax)
        tmax = tymax;
    if (inv.z < 0) { t = minz; minz = maxz; maxz = t; }
    real tzmin = (minz - o.z) * inv.z;
    if (tzmin > tmax)
        return false;
    real tzmax = (maxz - o.z) * inv.z;
    if (tmin > tzmax)
        return false;
    if (tzmin > tmin)
        tmin = tzmin;
    if (tzmax < tmax)
        tmax = tzmax;
    return tmin < t1 && tmax > 0;
}

// intersect_triangle() (Moeller and Trumbore).
bool ray_triangle(real3 orig, real3 dir, real3 v0, real3 v1, real3 v2, real eps, real *t)
{
    real3 edge1 = v1 - v0;
    real3 edge2 = v2 - v0;
    real3 pvec  = cross_d(dir, edge2);
    real  det   = dot_d(edge1, pvec);
    real3 qvec;
    real  u, v;
    if (det > eps) {
        real3 tvec = orig - v0;
        u = dot_d(tvec, pvec);
        if (u < 0 || u > det)
            return false;
        qvec = cross_d(tvec, edge1);
        v = dot_d(dir, qvec);
        if (v < 0 || u + v > det)
            return false;
    } else if (det < -eps) {
        real3 tvec = orig - v0;
        u = dot_d(tvec, pvec);
        if (u > 0 || u < det)
            return false;
        qvec = cross_d(tvec, edge1);
        v = dot_d(dir, qvec);
        if (v > 0 || u + v < det)
            return false;
    } else
        return false;
    real inv_det = (real)1 / det;
    *t = dot_d(edge2, qvec) * inv_det;
    return true;
}

int sgn(float x) { return (0.f < x) - (x < 0.f); }

__kernel void visibility_rays(
    __global const float *vertices, __global const int *faces, __global const float *normals, __global const float *boxes,
    __global const int *nodes, __global const float *origins, __global const float *dirs, __global uchar *occluded,
    int n_rays, int rays_per_sample, int negative_start, int has_negative, real eps)
{
    const int r = get_global_id(0);
    if (r >= n_rays)
        return;
    const float3 dir = vload3(r, dirs);
    const real3  o   = load(origins, r / rays_per_sample);
    const real3  d   = convert_real3(dir);
    const real3  inv = (real3)((real)1 / d.x, (real)1 / d.y, (real)1 / d.z);
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;
    if (! has_negative) {
        // The first hit; hidden when the face hit looks back at the ray. igl::Hit keeps t as a float, and the CPU
        // compares the hits by that float.
        real best_t = INFINITY;
        int  best   = -1;
        while (sp > 0) {
            const int n   = stack[--sp];
            const int idx = nodes[n];
            if (idx == NODE_UNUSED || ! ray_box(o, inv, boxes + 6 * n, best_t))
                continue;
            if (idx >= 0) {
                const int3 f = vload3(idx, faces);
                real t;
                if (ray_triangle(o, d, load(vertices, f.x), load(vertices, f.y), load(vertices, f.z), eps, &t) && t > 0) {
                    const real t_hit = (real)(float)t;
                    if (t_hit < best_t) {
                        best_t = t_hit;
                        best   = idx;
                    }
                }
            } else {
                stack[sp++] = 2 * n + 2;
                stack[sp++] = 2 * n + 1;
            }
        }
        occluded[r] = best >= 0 && dot_f(vload3(best, normals), dir) <= 0.f;
    } else {
        // Every hit: the faces of the model count the ray going in and out, those of the negative volumes the other way.
        int  counter = 0;
        bool some    = false;
        while (sp > 0) {
            const int n   = stack[--sp];
            const int idx = nodes[n];
            if (idx == NODE_UNUSED || ! ray_box(o, inv, boxes + 6 * n, INFINITY))
                continue;
            if (idx >= 0) {
                const int3 f = vload3(idx, faces);
                real t;
                if (ray_triangle(o, d, load(vertices, f.x), load(vertices, f.y), load(vertices, f.z), eps, &t) && t > 0) {
                    some = true;
                    const int s = sgn(dot_f(vload3(idx, normals), dir));
                    counter += idx >= negative_start ? -s : s;
                }
            } else {
                stack[sp++] = 2 * n + 2;
                stack[sp++] = 2 * n + 1;
            }
        }
        occluded[r] = some && counter == 0;
    }
}
)CL";

const char* visibility_kernel(bool double_precision)
{
    static const std::string single_precision = visibility_kernel_body;
    static const std::string with_doubles     = std::string("#define TISMA_DOUBLE\n") + visibility_kernel_body;
    return double_precision ? with_doubles.c_str() : single_precision.c_str();
}

std::optional<std::vector<float>> raycast_visibility_gpu(
    OpenCL::Device &gpu,
    bool double_precision,
    const AABBTreeIndirect::Tree<3, float> &raycasting_tree,
    const indexed_triangle_set &triangles,
    const TriangleSetSamples &samples,
    size_t negative_volumes_start_index,
    const std::vector<Vec3f> &directions
) {
    using Tree = AABBTreeIndirect::Tree<3, float>;
    const auto start = std::chrono::steady_clock::now();
    const size_t n_samples = samples.positions.size();
    const size_t n_dirs    = directions.size();
    const size_t n_rays    = n_samples * n_dirs;
    if (n_rays == 0 || raycasting_tree.empty() || n_rays > size_t(std::numeric_limits<int>::max()) ||
        triangles.vertices.size() > size_t(std::numeric_limits<int>::max() / 3))
        return std::nullopt;

    const auto &tree_nodes = raycasting_tree.nodes();
    std::vector<float>   boxes(tree_nodes.size() * 6);
    std::vector<int32_t> nodes(tree_nodes.size());
    for (size_t i = 0; i < tree_nodes.size(); ++i) {
        const auto &n = tree_nodes[i];
        nodes[i] = n.idx == Tree::npos ? -1 : n.idx == Tree::inner ? -2 : int32_t(n.idx);
        for (int k = 0; k < 3; ++k) {
            boxes[i * 6 + k]     = n.bbox.min()[k];
            boxes[i * 6 + 3 + k] = n.bbox.max()[k];
        }
    }
    std::vector<float> vertices(triangles.vertices.size() * 3);
    for (size_t i = 0; i < triangles.vertices.size(); ++i)
        for (int k = 0; k < 3; ++k)
            vertices[i * 3 + k] = triangles.vertices[i][k];
    // The faces and their normals as its_face_normal() gives them to the CPU code.
    std::vector<int32_t> faces(triangles.indices.size() * 3);
    std::vector<float>   normals(triangles.indices.size() * 3);
    for (size_t i = 0; i < triangles.indices.size(); ++i) {
        const Vec3f normal = its_face_normal(triangles, int(i));
        for (int k = 0; k < 3; ++k) {
            faces[i * 3 + k]   = triangles.indices[i][k];
            normals[i * 3 + k] = normal[k];
        }
    }

    // The rays, made as in raycast_visibility().
    const bool has_negative = negative_volumes_start_index < triangles.indices.size();
    std::vector<float> origins(n_samples * 3);
    std::vector<float> dirs(n_rays * 3);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, n_samples), [&](const tbb::blocked_range<size_t> &r) {
        for (size_t s = r.begin(); s < r.end(); ++s) {
            const Vec3f &center = samples.positions[s];
            const Vec3f &normal = samples.normals[s];
            Frame f;
            f.set_from_z(normal);
            const bool from_negative = has_negative && samples.triangle_indices[s] >= negative_volumes_start_index;
            const Vec3f origin = from_negative ? Vec3f(center - normal * 0.01f) : Vec3f(center + normal * 0.01f);
            for (int k = 0; k < 3; ++k)
                origins[s * 3 + k] = origin[k];
            for (size_t d = 0; d < n_dirs; ++d) {
                Vec3f dir = f.to_world(directions[d]);
                if (from_negative)
                    dir = -dir;
                for (int k = 0; k < 3; ++k)
                    dirs[(s * n_dirs + d) * 3 + k] = dir[k];
            }
        }
    });

    std::vector<uint8_t> occluded(n_rays);
    const int32_t n_rays_i       = int32_t(n_rays);
    const int32_t per_sample     = int32_t(n_dirs);
    const int32_t negative       = int32_t(std::min(negative_volumes_start_index, size_t(std::numeric_limits<int>::max())));
    const int32_t has_negative_i = has_negative ? 1 : 0;
    // The epsilon of intersect_ray_first_hit() / intersect_ray_all_hits() as called by raycast_visibility().
    const double eps        = 0.000001;
    const float  eps_single = float(eps);
    std::vector<OpenCL::Arg> args {
        OpenCL::Arg::input(vertices), OpenCL::Arg::input(faces), OpenCL::Arg::input(normals), OpenCL::Arg::input(boxes),
        OpenCL::Arg::input(nodes), OpenCL::Arg::input(origins), OpenCL::Arg::input(dirs), OpenCL::Arg::output(occluded),
        OpenCL::Arg::value(n_rays_i), OpenCL::Arg::value(per_sample), OpenCL::Arg::value(negative),
        OpenCL::Arg::value(has_negative_i), double_precision ? OpenCL::Arg::value(eps) : OpenCL::Arg::value(eps_single) };
    if (! gpu.run(visibility_kernel(double_precision), "visibility_rays", n_rays, args))
        return std::nullopt;

    // Summed as on the CPU (one decrease per hidden ray, in order), so that equal rays give equal values.
    const float decrease_step = 1.0f / float(n_dirs);
    std::vector<float> result(n_samples);
    for (size_t s = 0; s < n_samples; ++s) {
        float v = 1.0f;
        for (size_t d = 0; d < n_dirs; ++d)
            if (occluded[s * n_dirs + d])
                v -= decrease_step;
        result[s] = v;
    }
    BOOST_LOG_TRIVIAL(info) << "SeamPlacer: " << n_rays << " visibility rays over " << triangles.indices.size()
        << " triangles on " << gpu.name() << (double_precision ? " (double precision)" : " (single precision)") << " in "
        << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << " ms";
    return result;
}

std::vector<float> raycast_visibility(
    const AABBTreeIndirect::Tree<3, float> &raycasting_tree,
    const indexed_triangle_set &triangles,
    const TriangleSetSamples &samples,
    size_t negative_volumes_start_index,
    const Visibility::Params &params,
    RaycastOn on = RaycastOn::Gpu
) {
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: raycast visibility of " << samples.positions.size() << " samples over " << triangles.indices.size()
            << " triangles: end";

    //prepare uniform samples of a hemisphere
    float step_size = 1.0f / params.sqr_rays_per_sample_point;
    std::vector<Vec3f> precomputed_sample_directions(
            params.sqr_rays_per_sample_point * params.sqr_rays_per_sample_point);
    for (size_t x_idx = 0; x_idx < params.sqr_rays_per_sample_point; ++x_idx) {
        float sample_x = x_idx * step_size + step_size / 2.0;
        for (size_t y_idx = 0; y_idx < params.sqr_rays_per_sample_point; ++y_idx) {
            size_t dir_index = x_idx * params.sqr_rays_per_sample_point + y_idx;
            float sample_y = y_idx * step_size + step_size / 2.0;
            precomputed_sample_directions[dir_index] = sample_hemisphere_uniform( { sample_x, sample_y });
        }
    }

    if (OpenCL::Device *gpu = on != RaycastOn::Cpu ? OpenCL::Device::get() : nullptr)
        if (std::optional<std::vector<float>> on_gpu = raycast_visibility_gpu(*gpu,
                on == RaycastOn::Gpu && gpu->double_precision(), raycasting_tree, triangles, samples,
                negative_volumes_start_index, precomputed_sample_directions))
            return std::move(*on_gpu);

    bool model_contains_negative_parts = negative_volumes_start_index < triangles.indices.size();

    std::vector<float> result(samples.positions.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, result.size()),
            [&triangles, &precomputed_sample_directions, model_contains_negative_parts, negative_volumes_start_index,
                    &raycasting_tree, &result, &samples, &params](tbb::blocked_range<size_t> r) {
                // Maintaining hits memory outside of the loop, so it does not have to be reallocated for each query.
                std::vector<igl::Hit> hits;
                for (size_t s_idx = r.begin(); s_idx < r.end(); ++s_idx) {
                    result[s_idx] = 1.0f;
                    const float decrease_step = 1.0f
                            / (params.sqr_rays_per_sample_point * params.sqr_rays_per_sample_point);

                    const Vec3f &center = samples.positions[s_idx];
                    const Vec3f &normal = samples.normals[s_idx];
                    // apply the local direction via Frame struct - the local_dir is with respect to +Z being forward
                    Frame f;
                    f.set_from_z(normal);

                    for (const auto &dir : precomputed_sample_directions) {
                        Vec3f final_ray_dir = (f.to_world(dir));
                        if (!model_contains_negative_parts) {
                            igl::Hit hitpoint;
                            // FIXME: This AABBTTreeIndirect query will not compile for float ray origin and
                            // direction.
                            Vec3d final_ray_dir_d = final_ray_dir.cast<double>();
                            Vec3d ray_origin_d = (center + normal * 0.01f).cast<double>(); // start above surface.
                            bool hit = AABBTreeIndirect::intersect_ray_first_hit(triangles.vertices,
                                    triangles.indices, raycasting_tree, ray_origin_d, final_ray_dir_d, hitpoint);
                            if (hit && its_face_normal(triangles, hitpoint.id).dot(final_ray_dir) <= 0) {
                                result[s_idx] -= decrease_step;
                            }
                        } else { //TODO improve logic for order based boolean operations - consider order of volumes
                            bool casting_from_negative_volume = samples.triangle_indices[s_idx]
                                    >= negative_volumes_start_index;

                            Vec3d ray_origin_d = (center + normal * 0.01f).cast<double>(); // start above surface.
                            if (casting_from_negative_volume) { // if casting from negative volume face, invert direction, change start pos
                                final_ray_dir = -1.0 * final_ray_dir;
                                ray_origin_d = (center - normal * 0.01f).cast<double>();
                            }
                            Vec3d final_ray_dir_d = final_ray_dir.cast<double>();
                            bool some_hit = AABBTreeIndirect::intersect_ray_all_hits(triangles.vertices,
                                    triangles.indices, raycasting_tree,
                                    ray_origin_d, final_ray_dir_d, hits);
                            if (some_hit) {
                                int counter = 0;
                                // NOTE: iterating in reverse, from the last hit for one simple reason: We know the state of the ray at that point;
                                //  It cannot be inside model, and it cannot be inside negative volume
                                for (int hit_index = int(hits.size()) - 1; hit_index >= 0; --hit_index) {
                                    Vec3f face_normal = its_face_normal(triangles, hits[hit_index].id);
                                    if (hits[hit_index].id >= int(negative_volumes_start_index)) { //negative volume hit
                                        counter -= sgn(face_normal.dot(final_ray_dir)); // if volume face aligns with ray dir, we are leaving negative space
                                        // which in reverse hit analysis means, that we are entering negative space :) and vice versa
                                    } else {
                                        counter += sgn(face_normal.dot(final_ray_dir));
                                    }
                                }
                                if (counter == 0) {
                                    result[s_idx] -= decrease_step;
                                }
                            }
                        }
                    }
                }
            });

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: raycast visibility of " << samples.positions.size() << " samples over " << triangles.indices.size()
            << " triangles: end";

    return result;
}

std::vector<float> sample_visibility(const indexed_triangle_set &triangles, const TriangleSetSamples &samples,
                                     size_t negative_volumes_start_index, size_t sqr_rays_per_sample_point, RaycastOn on)
{
    const auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(triangles.vertices, triangles.indices);
    Visibility::Params params;
    params.sqr_rays_per_sample_point = sqr_rays_per_sample_point;
    return raycast_visibility(tree, triangles, samples, negative_volumes_start_index, params, on);
}
}

namespace {

// Tisma (phase 8): cache of the visibility between G-code exports. The visibility depends only on the meshes and
// the matrices of the model parts and negative volumes, on the transformation of the object and on the parameters,
// so changing a setting which only affects the G-code (temperatures, speeds, custom G-code...) does not cast the
// rays again (~0.7 s for the Benchy). The key is a hash of all these inputs, the result is the same.
struct VisibilityCacheEntry
{
    uint64_t           key;
    TriangleSetSamples samples;
    std::vector<float> visibility;
    float              radius;
};

struct VisibilityCache
{
    std::mutex                       mutex;
    std::deque<VisibilityCacheEntry> entries;
    static constexpr size_t          MAX_ENTRIES = 16;
};

VisibilityCache& visibility_cache()
{
    static VisibilityCache cache;
    return cache;
}

class Hasher
{
public:
    void add(const void *data, size_t size)
    {
        // FNV-1a over 64 bit words, then the remaining bytes.
        const unsigned char *p = static_cast<const unsigned char*>(data);
        for (; size >= 8; p += 8, size -= 8) {
            uint64_t w;
            std::memcpy(&w, p, 8);
            m_hash = (m_hash ^ w) * 0x100000001b3ull;
        }
        for (; size > 0; ++ p, -- size)
            m_hash = (m_hash ^ *p) * 0x100000001b3ull;
    }
    template<typename T> void add(const T &value) { add(&value, sizeof(T)); }
    uint64_t value() const { return m_hash; }

private:
    uint64_t m_hash { 0xcbf29ce484222325ull };
};

uint64_t visibility_key(const Transform3d &obj_transform, const ModelVolumePtrs &volumes, const Visibility::Params &params)
{
    Hasher h;
    h.add(obj_transform.matrix().data(), sizeof(double) * 16);
    h.add(params.raycasting_visibility_samples_count);
    h.add(params.fast_decimation_triangle_count_target);
    h.add(params.sqr_rays_per_sample_point);
    for (const ModelVolume *model_volume : volumes) {
        if (model_volume->type() != ModelVolumeType::MODEL_PART && model_volume->type() != ModelVolumeType::NEGATIVE_VOLUME)
            continue;
        h.add(int(model_volume->type()));
        h.add(model_volume->get_matrix().matrix().data(), sizeof(double) * 16);
        const indexed_triangle_set &its = model_volume->mesh().its;
        h.add(its.vertices.size());
        h.add(its.indices.size());
        h.add(its.vertices.data(), its.vertices.size() * sizeof(stl_vertex));
        h.add(its.indices.data(), its.indices.size() * sizeof(stl_triangle_vertex_indices));
    }
    return h.value();
}

} // namespace

Visibility::Visibility(
    const Transform3d &obj_transform,
    const ModelVolumePtrs &volumes,
    const Params &params,
    const std::function<void(void)> &throw_if_canceled
) {
    const uint64_t key = visibility_key(obj_transform, volumes, params);
    {
        VisibilityCache &cache = visibility_cache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        for (const VisibilityCacheEntry &entry : cache.entries)
            if (entry.key == key) {
                this->mesh_samples                    = entry.samples;
                this->mesh_samples_visibility         = entry.visibility;
                this->mesh_samples_radius             = entry.radius;
                this->mesh_samples_coordinate_functor = Impl::CoordinateFunctor(&this->mesh_samples.positions);
                this->mesh_samples_tree = KDTreeIndirect<3, float, Impl::CoordinateFunctor>(this->mesh_samples_coordinate_functor,
                    this->mesh_samples.positions.size());
                BOOST_LOG_TRIVIAL(debug) << "SeamPlacer: visibility from the cache";
                return;
            }
    }
    this->compute(obj_transform, volumes, params, throw_if_canceled);
    {
        VisibilityCache &cache = visibility_cache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.entries.push_front({ key, this->mesh_samples, this->mesh_samples_visibility, this->mesh_samples_radius });
        if (cache.entries.size() > VisibilityCache::MAX_ENTRIES)
            cache.entries.pop_back();
    }
}

void Visibility::compute(
    const Transform3d &obj_transform,
    const ModelVolumePtrs &volumes,
    const Params &params,
    const std::function<void(void)> &throw_if_canceled
) {
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: gather occlusion meshes: start";
    indexed_triangle_set triangle_set;
    indexed_triangle_set negative_volumes_set;
    //add all parts
    for (const ModelVolume *model_volume : volumes) {
        if (model_volume->type() == ModelVolumeType::MODEL_PART
                || model_volume->type() == ModelVolumeType::NEGATIVE_VOLUME) {
            auto model_transformation = model_volume->get_matrix();
            indexed_triangle_set model_its = model_volume->mesh().its;
            its_transform(model_its, model_transformation);
            if (model_volume->type() == ModelVolumeType::MODEL_PART) {
                its_merge(triangle_set, model_its);
            } else {
                its_merge(negative_volumes_set, model_its);
            }
        }
    }
    throw_if_canceled();

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: gather occlusion meshes: end";

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: decimate: start";
    its_short_edge_collpase(triangle_set, params.fast_decimation_triangle_count_target);
    its_short_edge_collpase(negative_volumes_set, params.fast_decimation_triangle_count_target);

    size_t negative_volumes_start_index = triangle_set.indices.size();
    its_merge(triangle_set, negative_volumes_set);
    its_transform(triangle_set, obj_transform);
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: decimate: end";

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: Compute visibility sample points: start";

    this->mesh_samples = sample_its_uniform_parallel(params.raycasting_visibility_samples_count,
            triangle_set);
    this->mesh_samples_coordinate_functor = Impl::CoordinateFunctor(&this->mesh_samples.positions);
    this->mesh_samples_tree = KDTreeIndirect<3, float, Impl::CoordinateFunctor>(this->mesh_samples_coordinate_functor,
            this->mesh_samples.positions.size());

    // The following code determines search area for random visibility samples on the mesh when calculating visibility of each perimeter point
    // number of random samples in the given radius (area) is approximately poisson distribution
    // to compute ideal search radius (area), we use exponential distribution (complementary distr to poisson)
    // parameters of exponential distribution to compute area that will have with probability="probability" more than given number of samples="samples"
    float probability = 0.9f;
    float samples = 4;
    float density = params.raycasting_visibility_samples_count / this->mesh_samples.total_area;
    // exponential probability distrubtion function is : f(x) = P(X > x) = e^(l*x) where l is the rate parameter (computed as 1/u where u is mean value)
    // probability that sampled area A with S samples contains more than samples count:
    //  P(S > samples in A) = e^-(samples/(density*A));   express A:
    float search_area = samples / (-logf(probability) * density);
    float search_radius = sqrt(search_area / PI);
    this->mesh_samples_radius = search_radius;

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: Compute visiblity sample points: end";
    throw_if_canceled();

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: Mesh sample raidus: " << this->mesh_samples_radius;

    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: build AABB tree: start";
    auto raycasting_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(triangle_set.vertices,
            triangle_set.indices);

    throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug)
    << "SeamPlacer: build AABB tree: end";
    this->mesh_samples_visibility = Impl::raycast_visibility(raycasting_tree, triangle_set, this->mesh_samples,
            negative_volumes_start_index, params);
    throw_if_canceled();
}

float Visibility::calculate_point_visibility(const Vec3f &position) const {
    std::vector<size_t> points = find_nearby_points(mesh_samples_tree, position, mesh_samples_radius);
    if (points.empty()) {
        return 1.0f;
    }

    auto compute_dist_to_plane = [](const Vec3f &position, const Vec3f &plane_origin,
                                    const Vec3f &plane_normal) {
        Vec3f orig_to_point = position - plane_origin;
        return std::abs(orig_to_point.dot(plane_normal));
    };

    float total_weight = 0;
    float total_visibility = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        size_t sample_idx = points[i];

        Vec3f sample_point = this->mesh_samples.positions[sample_idx];
        Vec3f sample_normal = this->mesh_samples.normals[sample_idx];

        float weight = mesh_samples_radius -
            compute_dist_to_plane(position, sample_point, sample_normal);
        weight += (mesh_samples_radius - (position - sample_point).norm());
        total_visibility += weight * mesh_samples_visibility[sample_idx];
        total_weight += weight;
    }

    return total_visibility / total_weight;
}

}
