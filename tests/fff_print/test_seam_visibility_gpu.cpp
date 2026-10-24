// Tisma Slicer (phase 8): the visibility rays of the seam on the graphics card (OpenCL) give the result of the CPU.
// Runs with TISMA_GPU_SLICING=1 and an OpenCL device; on the CI runners without a graphics card, pocl (OpenCL on the
// CPU) with TISMA_GPU_DEVICE=cpu.
#include <catch2/catch_test_macros.hpp>

#include <libslic3r/GCode/ModelVisibility.hpp>
#include <libslic3r/OpenCL.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/TriangleSetSampling.hpp>

#include <boost/filesystem.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace Slic3r;

namespace {

void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

struct Comparison
{
    size_t samples   = 0;
    size_t differing = 0;
    float  max_diff  = 0.f;
};

Comparison compare(const std::vector<float> &cpu, const std::vector<float> &gpu)
{
    REQUIRE(cpu.size() == gpu.size());
    Comparison c;
    c.samples = cpu.size();
    for (size_t i = 0; i < cpu.size(); ++i) {
        const float d = std::abs(cpu[i] - gpu[i]);
        if (d > 0.f)
            ++c.differing;
        c.max_diff = std::max(c.max_diff, d);
    }
    return c;
}

// The card gives the result of the CPU in double precision; in single precision only rays grazing an edge may end
// otherwise (at most one ray of the 25 of a sample, 0.04, in the samples measured).
void check(const indexed_triangle_set &its, size_t negative_start, size_t samples_count)
{
    using ModelInfo::Impl::RaycastOn;
    const TriangleSetSamples samples = sample_its_uniform_parallel(samples_count, its);
    const std::vector<float> cpu = ModelInfo::Impl::sample_visibility(its, samples, negative_start, 5, RaycastOn::Cpu);
    REQUIRE(*std::min_element(cpu.begin(), cpu.end()) < 1.f);

    const Comparison single_precision = compare(cpu,
        ModelInfo::Impl::sample_visibility(its, samples, negative_start, 5, RaycastOn::GpuSinglePrecision));
    INFO("single precision: " << single_precision.differing << " of " << single_precision.samples
         << " samples differ, at most by " << single_precision.max_diff);
    CHECK(single_precision.differing <= single_precision.samples / 200);
    CHECK(single_precision.max_diff <= 0.081f);

    OpenCL::Device *device = OpenCL::Device::get();
    REQUIRE(device != nullptr);
    if (device->double_precision()) {
        const Comparison double_precision = compare(cpu,
            ModelInfo::Impl::sample_visibility(its, samples, negative_start, 5, RaycastOn::Gpu));
        INFO("double precision: " << double_precision.differing << " of " << double_precision.samples
             << " samples differ, at most by " << double_precision.max_diff);
        CHECK(double_precision.differing == 0);
    }
}

} // namespace

TEST_CASE("Seam visibility on the graphics card gives the result of the CPU", "[Seams][TismaGpu]")
{
    if (std::getenv("TISMA_GPU_SLICING") == nullptr)
        set_env("TISMA_GPU_SLICING", "1");
    OpenCL::Device *device = OpenCL::Device::get();
    if (device == nullptr)
        SKIP("No OpenCL device (TISMA_GPU_DEVICE=cpu uses pocl)");
    INFO("OpenCL device: " << device->name());

    SECTION("An L shaped part: the inner corner is partially hidden")
    {
        indexed_triangle_set mesh = its_make_cube(20., 20., 10.);
        its_merge(mesh, its_make_cube(5., 20., 20.));
        check(mesh, mesh.indices.size(), 20000);
    }

    SECTION("The Benchy")
    {
        const boost::filesystem::path benchy = boost::filesystem::path(TEST_DATA_DIR) / ".." / ".." / "resources" /
            "shapes" / "3DBenchy.stl";
        TriangleMesh mesh;
        REQUIRE(mesh.ReadSTLFile(benchy.string().c_str()));
        check(mesh.its, mesh.its.indices.size(), 30000);
    }

    SECTION("A negative volume inside the part: every hit is counted")
    {
        indexed_triangle_set mesh = its_make_cube(30., 30., 20.);
        const size_t negative_start = mesh.indices.size();
        indexed_triangle_set hole = its_make_cube(10., 10., 30.);
        for (stl_vertex &v : hole.vertices)
            v += Vec3f(10.f, 10.f, -5.f);
        its_merge(mesh, hole);
        check(mesh, negative_start, 20000);
    }
}
