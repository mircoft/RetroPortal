#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "retroportal/gpu/CommandBuffer.hpp"

namespace retroportal::gpu {

struct VulkanExtensionProbe {
    bool timeline_semaphore = false;
    bool dma_buf_external = false;
    bool synchronization2 = false;
};

struct GLESProbe {
    bool gli_es3 = false;
};

struct CapabilityReport {
    GpuBackendKind preferred = GpuBackendKind::Unknown;
    VulkanExtensionProbe vk{};
    GLESProbe gl{};
};

enum class AspectUniformPreset : std::uint8_t {
    PreserveCenterGameplay = 0,
    MaximumFill = 1,
};

CapabilityReport ProbeCapabilities(std::string* out_error);

class GpuPresentationPipeline {
public:
    GpuPresentationPipeline();
    ~GpuPresentationPipeline();

    GpuPresentationPipeline(GpuPresentationPipeline const&) = delete;
    GpuPresentationPipeline& operator=(GpuPresentationPipeline const&) =
        delete;

    bool Initialize(std::string* out_error);
    void Shutdown();

    bool SubmitFrameCommands(std::vector<cmd::Instruction> const& batch,
                             std::string* out_error);

    void SetStretchPreset(AspectUniformPreset preset);

    GpuBackendKind ActiveBackend() const;

private:
    bool ExecuteVulkan(std::vector<cmd::Instruction> const& batch,
                       std::string* out_error);
    bool ExecuteGLES(std::vector<cmd::Instruction> const& batch,
                     std::string* out_error);

    bool initialized_ = false;
    CapabilityReport caps_{};
    GpuBackendKind active_backend_ = GpuBackendKind::Unknown;
    AspectUniformPreset preset_ = AspectUniformPreset::PreserveCenterGameplay;
    mutable std::mutex mutex_{};
};

}  // namespace retroportal::gpu
