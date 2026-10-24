///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_OpenCL_hpp_
#define slic3r_OpenCL_hpp_

// Tisma (phase 8): computing on the graphics card while slicing. OpenCL 1.2 is the one API both of the Intel HD Graphics
// 4000 (it has no Vulkan on Windows) and of the NVIDIA cards of the computing servers. OpenCL is loaded at run time
// (OpenCL.dll / libOpenCL.so, installed by the driver of the graphics card): nothing is needed to build or to run, and
// whenever it is missing or anything fails the callers compute on the CPU, as before.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
namespace OpenCL {

// Whether slicing may use the graphics card: TISMA_GPU_SLICING=1 (set by the computing nodes when their "gpu" setting
// is on). TISMA_GPU_DEVICE picks the device by a part of its name (e.g. "P2000", "HD Graphics"), otherwise the
// dedicated card with the most compute units, then an integrated one; "cpu" allows an OpenCL device that is a CPU
// (pocl, for the tests).
bool enabled();

// One argument of a kernel: a buffer read by the kernel, a buffer written by it (read back after the run) or a value.
struct Arg
{
    enum class Kind { Input, Output, Value };
    Kind        kind;
    const void *data;
    void       *result;
    size_t      size;

    template<typename T> static Arg input(const std::vector<T> &v) { return { Kind::Input, v.data(), nullptr, v.size() * sizeof(T) }; }
    template<typename T> static Arg output(std::vector<T> &v) { return { Kind::Output, nullptr, v.data(), v.size() * sizeof(T) }; }
    template<typename T> static Arg value(const T &v) { return { Kind::Value, &v, nullptr, sizeof(T) }; }
};

class Device
{
public:
    // The device of this process, or nullptr when the graphics card is not to be used (not enabled, no OpenCL, no
    // device, or an error before).
    static Device* get();

    const std::string& name() const;

    // Whether the kernels compute in double precision (cl_khr_fp64), as the CPU code does in many places, and so give
    // its results: NVIDIA cards and pocl have it, integrated graphics often do not. TISMA_GPU_PRECISION=single turns it
    // off (faster on cards with slow doubles, slightly different results).
    bool double_precision() const;

    // Runs the kernel `kernel` of the program `source` (built once per program) over `global_size` work items. Returns
    // false on any error, after which the device is not used again by this process (get() returns nullptr) and the
    // caller computes on the CPU. Thread safe: one run at a time.
    bool run(const char *source, const char *kernel, size_t global_size, const std::vector<Arg> &args);

    ~Device();

    struct Impl;

private:
    explicit Device(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace OpenCL
} // namespace Slic3r

#endif // slic3r_OpenCL_hpp_
