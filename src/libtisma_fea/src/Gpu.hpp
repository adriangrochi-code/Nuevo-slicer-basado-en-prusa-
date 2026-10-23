///|/ Tisma Slicer: the GPU (Vulkan compute) used by the structural analysis.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Gpu_hpp_
#define tisma_fea_Gpu_hpp_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include <volk.h>

namespace Slic3r {
namespace Fea {
namespace gpu {

// Vulkan is loaded at run time (volk), so Tisma runs without a Vulkan driver: then there is no context and the
// analysis runs on the CPU. Devices that run on the CPU (lavapipe, SwiftShader) are not used, as they are slower than
// the analysis on the CPU, unless the environment variable TISMA_FEA_GPU_ALLOW_CPU is set (tests); TISMA_FEA_GPU=0
// disables the GPU.

struct Buffer
{
    VkBuffer       buffer { VK_NULL_HANDLE };
    VkDeviceMemory memory { VK_NULL_HANDLE };
    VkDeviceSize   size { 0 };
    void          *mapped { nullptr };   // host visible buffers
};

enum class Kernel { Apply, Restrict, Prolong, Count };

class Context
{
public:
    // The context, created at the first call; nullptr when there is no usable device or the GPU is disabled.
    static Context *get();

    const std::string &device_name() const { return m_device_name; }
    VkDevice           device() const { return m_device; }
    VkDeviceSize       max_storage_buffer_range() const { return m_max_storage_buffer_range; }
    // The queue is used by one analysis at a time.
    std::mutex        &mutex() { return m_mutex; }

    // Storage buffer in device memory (host visible: a mapped buffer for the transfers). Size at least 4 bytes.
    bool create_buffer(VkDeviceSize size, bool host_visible, Buffer &out);
    void destroy_buffer(Buffer &buffer);
    // Copies data to a device buffer (through a temporary host visible buffer) and waits.
    bool upload(const Buffer &dst, const void *data, size_t size);

    VkCommandBuffer allocate_command_buffer();
    void            free_command_buffer(VkCommandBuffer cmd);
    // Submits a recorded command buffer and waits for it.
    bool submit_and_wait(VkCommandBuffer cmd);

    struct Pipeline
    {
        VkDescriptorSetLayout set_layout { VK_NULL_HANDLE };
        VkPipelineLayout      layout { VK_NULL_HANDLE };
        VkPipeline            pipeline { VK_NULL_HANDLE };
        uint32_t              bindings { 0 };
    };
    const Pipeline &pipeline(Kernel kernel) const { return m_pipelines[size_t(kernel)]; }

private:
    Context() = default;
    bool init();

    std::string      m_device_name;
    VkInstance       m_instance { VK_NULL_HANDLE };
    VkPhysicalDevice m_physical { VK_NULL_HANDLE };
    VkDevice         m_device { VK_NULL_HANDLE };
    VkQueue          m_queue { VK_NULL_HANDLE };
    uint32_t         m_queue_family { 0 };
    VkCommandPool    m_command_pool { VK_NULL_HANDLE };
    VkFence          m_fence { VK_NULL_HANDLE };
    VkDeviceSize     m_max_storage_buffer_range { 0 };
    VkPhysicalDeviceMemoryProperties m_memory {};
    Pipeline         m_pipelines[size_t(Kernel::Count)];
    std::mutex       m_mutex;
};

// Pipeline barrier between compute and transfer commands (memory barrier).
void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);

} // namespace gpu
} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Gpu_hpp_
