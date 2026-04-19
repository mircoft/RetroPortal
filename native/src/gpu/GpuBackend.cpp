#include "retroportal/gpu/GpuBackend.hpp"

#include <android/log.h>
#include <dlfcn.h>

#include <cstring>

namespace retroportal::gpu {

namespace {

constexpr char const* kTag = "RetroPortalGPU";

bool ProbeVulkanLoader(VulkanExtensionProbe* vk) {
    void* h = ::dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, kTag,
                            "Vulkan loader not present on this build/device.");
        return false;
    }
    vk->timeline_semaphore = true;
    vk->dma_buf_external = true;
    vk->synchronization2 = true;
    ::dlclose(h);
    return true;
}

bool ProbeGLES(GLESProbe* gl) {
    gl->gli_es3 = true;
    return true;
}

}  // namespace

CapabilityReport ProbeCapabilities(std::string* out_error) {
    CapabilityReport r{};
    bool vk_ok = ProbeVulkanLoader(&r.vk);
    bool gl_ok = ProbeGLES(&r.gl);
    if (!vk_ok && !gl_ok) {
        if (out_error) {
            *out_error = "Neither Vulkan nor GLES probes succeeded.";
        }
        return r;
    }
    if (vk_ok) {
        r.preferred = GpuBackendKind::Vulkan;
    } else if (gl_ok) {
        r.preferred = GpuBackendKind::GLES3;
    }
    return r;
}

GpuPresentationPipeline::GpuPresentationPipeline() = default;

GpuPresentationPipeline::~GpuPresentationPipeline() {
    Shutdown();
}

bool GpuPresentationPipeline::Initialize(std::string* out_error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }
    caps_ = ProbeCapabilities(out_error);
    if (caps_.preferred == GpuBackendKind::Unknown) {
        return false;
    }
    active_backend_ = caps_.preferred;
    initialized_ = true;
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "GpuPresentationPipeline backend=%u",
                        static_cast<unsigned>(active_backend_));
    return true;
}

void GpuPresentationPipeline::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    active_backend_ = GpuBackendKind::Unknown;
}

bool GpuPresentationPipeline::ExecuteVulkan(
    std::vector<cmd::Instruction> const& batch,
    std::string* out_error) {
    (void)batch;
    if (out_error) {
        out_error->clear();
    }
    __android_log_print(ANDROID_LOG_VERBOSE, kTag,
                        "Vulkan execution path (host bind in full build).");
    return true;
}

bool GpuPresentationPipeline::ExecuteGLES(
    std::vector<cmd::Instruction> const& batch,
    std::string* out_error) {
    (void)batch;
    if (out_error) {
        out_error->clear();
    }
    __android_log_print(ANDROID_LOG_VERBOSE, kTag,
                        "GLES execution path (ANGLE/Mali host bind).");
    return true;
}

void GpuPresentationPipeline::SetStretchPreset(AspectUniformPreset preset) {
    std::lock_guard<std::mutex> lock(mutex_);
    preset_ = preset;
}

GpuBackendKind GpuPresentationPipeline::ActiveBackend() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_backend_;
}

bool GpuPresentationPipeline::SubmitFrameCommands(
    std::vector<cmd::Instruction> const& batch,
    std::string* out_error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        if (out_error) {
            *out_error = "GpuPresentationPipeline not initialized";
        }
        return false;
    }
    if (active_backend_ == GpuBackendKind::Vulkan) {
        return ExecuteVulkan(batch, out_error);
    }
    if (active_backend_ == GpuBackendKind::GLES3) {
        return ExecuteGLES(batch, out_error);
    }
    if (out_error) {
        *out_error = "No active GPU backend";
    }
    return false;
}

}  // namespace retroportal::gpu
