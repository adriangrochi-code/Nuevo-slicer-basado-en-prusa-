///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#include "OpenCL.hpp"

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#include <CL/cl_function_types.h>

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Slic3r {
namespace OpenCL {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string env(const char *name)
{
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// The functions of OpenCL 1.2 that are used, loaded from the library of the driver (the ICD loader).
struct Api
{
    void                        *library = nullptr;
    clGetPlatformIDs_fn          GetPlatformIDs          = nullptr;
    clGetDeviceIDs_fn            GetDeviceIDs            = nullptr;
    clGetDeviceInfo_fn           GetDeviceInfo           = nullptr;
    clCreateContext_fn           CreateContext           = nullptr;
    clReleaseContext_fn          ReleaseContext          = nullptr;
    clCreateCommandQueue_fn      CreateCommandQueue      = nullptr;
    clReleaseCommandQueue_fn     ReleaseCommandQueue     = nullptr;
    clCreateProgramWithSource_fn CreateProgramWithSource = nullptr;
    clBuildProgram_fn            BuildProgram            = nullptr;
    clGetProgramBuildInfo_fn     GetProgramBuildInfo     = nullptr;
    clReleaseProgram_fn          ReleaseProgram          = nullptr;
    clCreateKernel_fn            CreateKernel            = nullptr;
    clReleaseKernel_fn           ReleaseKernel           = nullptr;
    clSetKernelArg_fn            SetKernelArg            = nullptr;
    clCreateBuffer_fn            CreateBuffer            = nullptr;
    clReleaseMemObject_fn        ReleaseMemObject        = nullptr;
    clEnqueueNDRangeKernel_fn    EnqueueNDRangeKernel    = nullptr;
    clEnqueueReadBuffer_fn       EnqueueReadBuffer       = nullptr;
    clFinish_fn                  Finish                  = nullptr;

    void* symbol(const char *name) const
    {
#ifdef _WIN32
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(library), name));
#else
        return ::dlsym(library, name);
#endif
    }

    template<typename Fn> bool get(Fn &fn, const char *name)
    {
        fn = reinterpret_cast<Fn>(this->symbol(name));
        return fn != nullptr;
    }

    bool load()
    {
#ifdef _WIN32
        library = ::LoadLibraryA("OpenCL.dll");
#elif defined(__APPLE__)
        library = ::dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW | RTLD_LOCAL);
#else
        library = ::dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr)
            library = ::dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
#endif
        if (library == nullptr)
            return false;
        return get(GetPlatformIDs, "clGetPlatformIDs") && get(GetDeviceIDs, "clGetDeviceIDs") &&
            get(GetDeviceInfo, "clGetDeviceInfo") && get(CreateContext, "clCreateContext") &&
            get(ReleaseContext, "clReleaseContext") && get(CreateCommandQueue, "clCreateCommandQueue") &&
            get(ReleaseCommandQueue, "clReleaseCommandQueue") && get(CreateProgramWithSource, "clCreateProgramWithSource") &&
            get(BuildProgram, "clBuildProgram") && get(GetProgramBuildInfo, "clGetProgramBuildInfo") &&
            get(ReleaseProgram, "clReleaseProgram") && get(CreateKernel, "clCreateKernel") &&
            get(ReleaseKernel, "clReleaseKernel") && get(SetKernelArg, "clSetKernelArg") &&
            get(CreateBuffer, "clCreateBuffer") && get(ReleaseMemObject, "clReleaseMemObject") &&
            get(EnqueueNDRangeKernel, "clEnqueueNDRangeKernel") && get(EnqueueReadBuffer, "clEnqueueReadBuffer") &&
            get(Finish, "clFinish");
    }
};

struct Candidate
{
    cl_device_id device;
    std::string  name;
    bool         dedicated;
    bool         cpu;
    bool         double_precision;
    cl_uint      compute_units;
    cl_ulong     memory;
};

template<typename T> T device_info(const Api &api, cl_device_id device, cl_device_info what, T fallback)
{
    T value = fallback;
    if (api.GetDeviceInfo(device, what, sizeof(T), &value, nullptr) != CL_SUCCESS)
        return fallback;
    return value;
}

std::string device_name(const Api &api, cl_device_id device)
{
    size_t size = 0;
    if (api.GetDeviceInfo(device, CL_DEVICE_NAME, 0, nullptr, &size) != CL_SUCCESS || size == 0)
        return "OpenCL device";
    std::string name(size, '\0');
    api.GetDeviceInfo(device, CL_DEVICE_NAME, size, name.data(), nullptr);
    name.resize(std::strlen(name.c_str()));
    // Some drivers pad the name with spaces.
    while (! name.empty() && name.back() == ' ')
        name.pop_back();
    return name;
}

bool has_double_precision(const Api &api, cl_device_id device)
{
    size_t size = 0;
    if (api.GetDeviceInfo(device, CL_DEVICE_EXTENSIONS, 0, nullptr, &size) != CL_SUCCESS || size == 0)
        return false;
    std::string extensions(size, '\0');
    api.GetDeviceInfo(device, CL_DEVICE_EXTENSIONS, size, extensions.data(), nullptr);
    return extensions.find("cl_khr_fp64") != std::string::npos &&
        (device_info<cl_device_fp_config>(api, device, CL_DEVICE_DOUBLE_FP_CONFIG, 0) & CL_FP_ROUND_TO_NEAREST) != 0;
}

// The devices of every platform able to build and run a program: graphics cards, and CPUs only when asked for.
std::vector<Candidate> candidates(const Api &api, bool allow_cpu)
{
    std::vector<Candidate> out;
    cl_uint n_platforms = 0;
    if (api.GetPlatformIDs(0, nullptr, &n_platforms) != CL_SUCCESS || n_platforms == 0)
        return out;
    std::vector<cl_platform_id> platforms(n_platforms);
    if (api.GetPlatformIDs(n_platforms, platforms.data(), nullptr) != CL_SUCCESS)
        return out;
    for (cl_platform_id platform : platforms) {
        const cl_device_type types = CL_DEVICE_TYPE_GPU | (allow_cpu ? CL_DEVICE_TYPE_CPU : 0);
        cl_uint n_devices = 0;
        if (api.GetDeviceIDs(platform, types, 0, nullptr, &n_devices) != CL_SUCCESS || n_devices == 0)
            continue;
        std::vector<cl_device_id> devices(n_devices);
        if (api.GetDeviceIDs(platform, types, n_devices, devices.data(), nullptr) != CL_SUCCESS)
            continue;
        for (cl_device_id d : devices) {
            if (! device_info<cl_bool>(api, d, CL_DEVICE_AVAILABLE, CL_FALSE) ||
                ! device_info<cl_bool>(api, d, CL_DEVICE_COMPILER_AVAILABLE, CL_FALSE))
                continue;
            const cl_device_type type = device_info<cl_device_type>(api, d, CL_DEVICE_TYPE, 0);
            out.push_back({ d, device_name(api, d),
                            ! device_info<cl_bool>(api, d, CL_DEVICE_HOST_UNIFIED_MEMORY, CL_TRUE),
                            (type & CL_DEVICE_TYPE_GPU) == 0,
                            has_double_precision(api, d),
                            device_info<cl_uint>(api, d, CL_DEVICE_MAX_COMPUTE_UNITS, 0),
                            device_info<cl_ulong>(api, d, CL_DEVICE_GLOBAL_MEM_SIZE, 0) });
        }
    }
    return out;
}

} // namespace

bool enabled()
{
    const std::string v = lower(env("TISMA_GPU_SLICING"));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

struct Device::Impl
{
    Api                                                    api;
    std::string                                            name;
    bool                                                   double_precision = false;
    cl_device_id                                           device  = nullptr;
    cl_context                                             context = nullptr;
    cl_command_queue                                       queue   = nullptr;
    std::map<const char*, cl_program>                      programs;
    std::map<std::pair<cl_program, std::string>, cl_kernel> kernels;
    std::mutex                                             mutex;

    ~Impl()
    {
        if (api.library == nullptr)
            return;
        for (auto &k : kernels)
            api.ReleaseKernel(k.second);
        for (auto &p : programs)
            api.ReleaseProgram(p.second);
        if (queue)
            api.ReleaseCommandQueue(queue);
        if (context)
            api.ReleaseContext(context);
        // The library of the driver stays loaded until the process ends: some drivers do not like to be unloaded.
    }

    cl_kernel kernel(const char *source, const char *kernel_name)
    {
        cl_program program = nullptr;
        if (auto it = programs.find(source); it != programs.end())
            program = it->second;
        else {
            cl_int err = CL_SUCCESS;
            program = api.CreateProgramWithSource(context, 1, &source, nullptr, &err);
            if (err != CL_SUCCESS || program == nullptr) {
                BOOST_LOG_TRIVIAL(error) << "OpenCL: the program was not created (" << err << ")";
                return nullptr;
            }
            // No fast math, so that the results follow IEEE rounding as on the CPU (the kernels turn off FP_CONTRACT).
            err = api.BuildProgram(program, 1, &device, "-cl-std=CL1.2", nullptr, nullptr);
            if (err != CL_SUCCESS) {
                size_t size = 0;
                api.GetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &size);
                std::string log(size, '\0');
                if (size > 0)
                    api.GetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, size, log.data(), nullptr);
                BOOST_LOG_TRIVIAL(error) << "OpenCL: the program did not build on " << name << " (" << err << "): " << log;
                api.ReleaseProgram(program);
                return nullptr;
            }
            programs.emplace(source, program);
        }
        auto key = std::make_pair(program, std::string(kernel_name));
        if (auto it = kernels.find(key); it != kernels.end())
            return it->second;
        cl_int err = CL_SUCCESS;
        cl_kernel k = api.CreateKernel(program, kernel_name, &err);
        if (err != CL_SUCCESS || k == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "OpenCL: no kernel " << kernel_name << " (" << err << ")";
            return nullptr;
        }
        kernels.emplace(key, k);
        return k;
    }
};

namespace {
std::once_flag          s_once;
std::unique_ptr<Device> s_device;
std::atomic<bool>       s_failed { false };
} // namespace

Device::Device(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
Device::~Device() = default;

const std::string& Device::name() const { return m_impl->name; }
bool Device::double_precision() const { return m_impl->double_precision; }

Device* Device::get()
{
    if (! enabled() || s_failed)
        return nullptr;
    std::call_once(s_once, []() {
        auto impl = std::make_unique<Impl>();
        if (! impl->api.load()) {
            BOOST_LOG_TRIVIAL(info) << "OpenCL: not available on this machine, slicing on the CPU";
            return;
        }
        const std::string wanted   = lower(env("TISMA_GPU_DEVICE"));
        const bool        want_cpu = wanted == "cpu";
        std::vector<Candidate> found = candidates(impl->api, want_cpu || ! wanted.empty());
        if (want_cpu)
            found.erase(std::remove_if(found.begin(), found.end(), [](const Candidate &c) { return ! c.cpu; }), found.end());
        else if (! wanted.empty())
            found.erase(std::remove_if(found.begin(), found.end(),
                [&wanted](const Candidate &c) { return lower(c.name).find(wanted) == std::string::npos; }), found.end());
        if (found.empty()) {
            BOOST_LOG_TRIVIAL(info) << "OpenCL: no device" << (wanted.empty() ? "" : " named " + wanted) << ", slicing on the CPU";
            return;
        }
        // A dedicated card before an integrated one, then the larger one.
        std::stable_sort(found.begin(), found.end(), [](const Candidate &a, const Candidate &b) {
            if (a.cpu != b.cpu)
                return ! a.cpu;
            if (a.dedicated != b.dedicated)
                return a.dedicated;
            if (a.compute_units != b.compute_units)
                return a.compute_units > b.compute_units;
            return a.memory > b.memory;
        });
        const Candidate &best = found.front();
        cl_int err = CL_SUCCESS;
        impl->device  = best.device;
        impl->name    = best.name;
        impl->double_precision = best.double_precision && lower(env("TISMA_GPU_PRECISION")) != "single";
        impl->context = impl->api.CreateContext(nullptr, 1, &impl->device, nullptr, nullptr, &err);
        if (err != CL_SUCCESS || impl->context == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "OpenCL: no context on " << best.name << " (" << err << ")";
            return;
        }
        impl->queue = impl->api.CreateCommandQueue(impl->context, impl->device, 0, &err);
        if (err != CL_SUCCESS || impl->queue == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "OpenCL: no command queue on " << best.name << " (" << err << ")";
            return;
        }
        BOOST_LOG_TRIVIAL(info) << "OpenCL: slicing uses " << best.name << (best.dedicated ? " (dedicated)" : " (integrated)")
                                << ", " << best.compute_units << " compute units"
                                << (impl->double_precision ? ", double precision" : ", single precision");
        s_device.reset(new Device(std::move(impl)));
    });
    return s_failed || ! s_device ? nullptr : s_device.get();
}

bool Device::run(const char *source, const char *kernel_name, size_t global_size, const std::vector<Arg> &args)
{
    Impl &d = *m_impl;
    std::lock_guard<std::mutex> lock(d.mutex);
    if (s_failed)
        return false;
    auto fail = [&d](const char *what, cl_int err) {
        BOOST_LOG_TRIVIAL(error) << "OpenCL: " << what << " failed on " << d.name << " (" << err << "): slicing on the CPU";
        s_failed = true;
        return false;
    };
    cl_kernel kernel = d.kernel(source, kernel_name);
    if (kernel == nullptr) {
        s_failed = true;
        return false;
    }
    std::vector<cl_mem> buffers;
    auto release = [&d, &buffers]() {
        for (cl_mem m : buffers)
            d.api.ReleaseMemObject(m);
    };
    cl_int err = CL_SUCCESS;
    for (size_t i = 0; i < args.size(); ++i) {
        const Arg &a = args[i];
        if (a.kind == Arg::Kind::Value) {
            err = d.api.SetKernelArg(kernel, cl_uint(i), a.size, a.data);
        } else {
            // An empty buffer is not allowed: one of 4 bytes stands for it (the kernel does not read it).
            static const cl_uint dummy = 0;
            const bool   empty = a.size == 0;
            const size_t size  = empty ? sizeof(dummy) : a.size;
            cl_mem m = a.kind == Arg::Kind::Input ?
                d.api.CreateBuffer(d.context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, size,
                                   const_cast<void*>(empty ? static_cast<const void*>(&dummy) : a.data), &err) :
                d.api.CreateBuffer(d.context, CL_MEM_WRITE_ONLY, size, nullptr, &err);
            if (err != CL_SUCCESS || m == nullptr) {
                release();
                return fail("allocating memory", err);
            }
            buffers.push_back(m);
            err = d.api.SetKernelArg(kernel, cl_uint(i), sizeof(cl_mem), &buffers.back());
        }
        if (err != CL_SUCCESS) {
            release();
            return fail("setting an argument", err);
        }
    }
    if (global_size > 0) {
        const size_t size = global_size;
        err = d.api.EnqueueNDRangeKernel(d.queue, kernel, 1, nullptr, &size, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            release();
            return fail("running the kernel", err);
        }
    }
    size_t buffer = 0;
    for (const Arg &a : args) {
        if (a.kind == Arg::Kind::Value)
            continue;
        if (a.kind == Arg::Kind::Output && a.size > 0) {
            err = d.api.EnqueueReadBuffer(d.queue, buffers[buffer], CL_TRUE, 0, a.size, a.result, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) {
                release();
                return fail("reading the result", err);
            }
        }
        ++buffer;
    }
    err = d.api.Finish(d.queue);
    release();
    return err == CL_SUCCESS || fail("finishing", err);
}

} // namespace OpenCL
} // namespace Slic3r
