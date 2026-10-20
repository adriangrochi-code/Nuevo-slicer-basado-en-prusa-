///|/ Tisma Slicer: the GPU (Vulkan compute) used by the structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Gpu.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <boost/log/trivial.hpp>

#include "tisma_fea/Analysis.hpp"
#include "gpu/FeaShaders.hpp"

namespace Slic3r {
namespace Fea {

namespace {
std::atomic<bool> s_gpu_enabled { true };

bool env_set(const char *name)
{
    const char *v = std::getenv(name);
    return v != nullptr && *v != 0 && std::strcmp(v, "0") != 0;
}
} // namespace

void set_gpu_enabled(bool enabled) { s_gpu_enabled = enabled; }
bool gpu_enabled() { return s_gpu_enabled; }

std::string gpu_device_name()
{
    gpu::Context *ctx = gpu::Context::get();
    return ctx ? ctx->device_name() : std::string();
}

namespace gpu {

Context *Context::get()
{
    // Created once; never destroyed, as the Vulkan driver may be unloaded before the static destructors run.
    static Context *s_context = []() -> Context * {
        const char *disabled = std::getenv("TISMA_FEA_GPU");
        if (disabled != nullptr && std::strcmp(disabled, "0") == 0)
            return nullptr;
        auto *ctx = new Context();
        if (! ctx->init()) {
            // The objects created before the failure are leaked: it happens once.
            return nullptr;
        }
        BOOST_LOG_TRIVIAL(info) << "Structural analysis on the GPU: " << ctx->device_name();
        return ctx;
    }();
    return s_gpu_enabled ? s_context : nullptr;
}

static bool create_pipeline(VkDevice device, const uint32_t *code, size_t code_size, uint32_t bindings, uint32_t push_size, Context::Pipeline &out)
{
    std::vector<VkDescriptorSetLayoutBinding> b(bindings);
    for (uint32_t i = 0; i < bindings; ++ i) {
        b[i].binding         = i;
        b[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i].descriptorCount = 1;
        b[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dl { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dl.bindingCount = bindings;
    dl.pBindings    = b.data();
    if (vkCreateDescriptorSetLayout(device, &dl, nullptr, &out.set_layout) != VK_SUCCESS)
        return false;
    VkPushConstantRange push { VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size };
    VkPipelineLayoutCreateInfo pl { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pl.setLayoutCount         = 1;
    pl.pSetLayouts            = &out.set_layout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges    = &push;
    if (vkCreatePipelineLayout(device, &pl, nullptr, &out.layout) != VK_SUCCESS)
        return false;
    VkShaderModuleCreateInfo sm { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    sm.codeSize = code_size;
    sm.pCode    = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &sm, nullptr, &module) != VK_SUCCESS)
        return false;
    VkComputePipelineCreateInfo cp { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cp.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cp.stage.module = module;
    cp.stage.pName  = "main";
    cp.layout       = out.layout;
    const VkResult res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &out.pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    out.bindings = bindings;
    return res == VK_SUCCESS;
}

bool Context::init()
{
    if (volkInitialize() != VK_SUCCESS)
        return false;
    if (volkGetInstanceVersion() < VK_API_VERSION_1_1)
        return false;

    VkApplicationInfo app { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "Tisma Slicer";
    app.pEngineName      = "Tisma Slicer FEA";
    app.apiVersion       = VK_API_VERSION_1_1;
    // macOS (MoltenVK through the Vulkan loader): the portability devices are listed only when asked.
    std::vector<const char *> extensions;
    VkInstanceCreateFlags     flags = 0;
    {
        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> ext(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, ext.data());
        for (const VkExtensionProperties &e : ext)
            if (std::strcmp(e.extensionName, "VK_KHR_portability_enumeration") == 0) {
                extensions.push_back("VK_KHR_portability_enumeration");
                flags |= 0x00000001; // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
            }
    }
    VkInstanceCreateInfo ici { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.flags                   = flags;
    ici.pApplicationInfo        = &app;
    ici.enabledExtensionCount   = uint32_t(extensions.size());
    ici.ppEnabledExtensionNames = extensions.data();
    if (vkCreateInstance(&ici, nullptr, &m_instance) != VK_SUCCESS)
        return false;
    volkLoadInstance(m_instance);

    // The device: a discrete GPU first, then an integrated one.
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(m_instance, &count, nullptr) != VK_SUCCESS || count == 0)
        return false;
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
    const bool allow_cpu = env_set("TISMA_FEA_GPU_ALLOW_CPU");
    int best_rank = 0;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.apiVersion < VK_API_VERSION_1_1)
            continue;
        int rank = 0;
        switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   rank = 4; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: rank = 3; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    rank = 2; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            rank = allow_cpu ? 1 : 0; break;
        default:                                     rank = 0; break;
        }
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qf(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qf.data());
        int family = -1;
        for (uint32_t i = 0; i < nq; ++ i)
            if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                family = int(i);
                break;
            }
        if (rank > best_rank && family >= 0) {
            best_rank      = rank;
            m_physical     = d;
            m_queue_family = uint32_t(family);
            m_device_name  = props.deviceName;
            m_max_storage_buffer_range = props.limits.maxStorageBufferRange;
        }
    }
    if (m_physical == VK_NULL_HANDLE)
        return false;
    vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memory);

    std::vector<const char *> device_extensions;
    {
        uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> ext(n);
        vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &n, ext.data());
        for (const VkExtensionProperties &e : ext)
            if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0)
                device_extensions.push_back("VK_KHR_portability_subset");
    }
    const float priority = 1.f;
    VkDeviceQueueCreateInfo qci { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = m_queue_family;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = uint32_t(device_extensions.size());
    dci.ppEnabledExtensionNames = device_extensions.data();
    if (vkCreateDevice(m_physical, &dci, nullptr, &m_device) != VK_SUCCESS)
        return false;
    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);

    VkCommandPoolCreateInfo cpi { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = m_queue_family;
    if (vkCreateCommandPool(m_device, &cpi, nullptr, &m_command_pool) != VK_SUCCESS)
        return false;
    VkFenceCreateInfo fci { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(m_device, &fci, nullptr, &m_fence) != VK_SUCCESS)
        return false;

    // Push constants: fea_apply 20 bytes, fea_restrict and fea_prolong 4 bytes.
    return create_pipeline(m_device, GpuShaders::fea_apply, sizeof(GpuShaders::fea_apply), 9, 20, m_pipelines[size_t(Kernel::Apply)]) &&
           create_pipeline(m_device, GpuShaders::fea_restrict, sizeof(GpuShaders::fea_restrict), 6, 4, m_pipelines[size_t(Kernel::Restrict)]) &&
           create_pipeline(m_device, GpuShaders::fea_prolong, sizeof(GpuShaders::fea_prolong), 5, 4, m_pipelines[size_t(Kernel::Prolong)]);
}

bool Context::create_buffer(VkDeviceSize size, bool host_visible, Buffer &out)
{
    size = std::max<VkDeviceSize>(size, 4);
    VkBufferCreateInfo bci { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size        = size;
    bci.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &out.buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, out.buffer, &req);
    const VkMemoryPropertyFlags wanted = host_visible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) :
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < m_memory.memoryTypeCount; ++ i)
        if ((req.memoryTypeBits & (1u << i)) && (m_memory.memoryTypes[i].propertyFlags & wanted) == wanted) {
            type = i;
            break;
        }
    if (type == UINT32_MAX) {
        destroy_buffer(out);
        return false;
    }
    VkMemoryAllocateInfo mai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = type;
    if (vkAllocateMemory(m_device, &mai, nullptr, &out.memory) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, out.buffer, out.memory, 0) != VK_SUCCESS) {
        destroy_buffer(out);
        return false;
    }
    if (host_visible && vkMapMemory(m_device, out.memory, 0, size, 0, &out.mapped) != VK_SUCCESS) {
        destroy_buffer(out);
        return false;
    }
    out.size = size;
    return true;
}

void Context::destroy_buffer(Buffer &buffer)
{
    if (buffer.mapped)
        vkUnmapMemory(m_device, buffer.memory);
    if (buffer.buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_device, buffer.buffer, nullptr);
    if (buffer.memory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = Buffer();
}

VkCommandBuffer Context::allocate_command_buffer()
{
    VkCommandBufferAllocateInfo ai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool        = m_command_pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &ai, &cmd) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return cmd;
}

void Context::free_command_buffer(VkCommandBuffer cmd)
{
    if (cmd != VK_NULL_HANDLE)
        vkFreeCommandBuffers(m_device, m_command_pool, 1, &cmd);
}

bool Context::submit_and_wait(VkCommandBuffer cmd)
{
    VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    if (vkQueueSubmit(m_queue, 1, &si, m_fence) != VK_SUCCESS)
        return false;
    const bool ok = vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    vkResetFences(m_device, 1, &m_fence);
    return ok;
}

bool Context::upload(const Buffer &dst, const void *data, size_t size)
{
    if (size == 0)
        return true;
    Buffer staging;
    if (! create_buffer(size, true, staging))
        return false;
    std::memcpy(staging.mapped, data, size);
    VkCommandBuffer cmd = allocate_command_buffer();
    bool ok = cmd != VK_NULL_HANDLE;
    if (ok) {
        VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        VkBufferCopy region { 0, 0, size };
        vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &region);
        ok = vkEndCommandBuffer(cmd) == VK_SUCCESS && submit_and_wait(cmd);
        free_command_buffer(cmd);
    }
    destroy_buffer(staging);
    return ok;
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access)
{
    VkMemoryBarrier mb { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = src_access;
    mb.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

} // namespace gpu
} // namespace Fea
} // namespace Slic3r
