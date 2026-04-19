#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "retroportal/gpu/CommandBuffer.hpp"

namespace retroportal::gpu {

enum class DxBackendPreference : std::uint8_t {
    DXVK_NATIVE = 0,
    ZINK_NATIVE = 1,
    INTERNAL_MINIMAL = 2,
};

struct TranslatorCapabilities {
    bool dxvk_supported = false;
    bool zink_supported = false;
    bool vma_supported = false;
};

struct DxPresentState {
    std::uint32_t backbuffer_width = 640;
    std::uint32_t backbuffer_height = 480;
    float depth_clear_value = 1.f;
};

class D3DTranslator {
public:
    D3DTranslator();
    ~D3DTranslator();

    bool Initialize(std::optional<DxBackendPreference> forced_backend,
                    std::string* out_error);
    void Shutdown();

    bool ProbeRuntime(DxBackendPreference preference,
                      TranslatorCapabilities* caps,
                      std::string* out_error);

    bool EmitPresentPassCommands(CommandRecorder* recorder,
                                 DxPresentState const& state,
                                 std::string* out_error);

private:
    TranslatorCapabilities caps_{};
    DxBackendPreference active_ = DxBackendPreference::INTERNAL_MINIMAL;
    bool initialized_ = false;
    std::mutex mutex_{};
};

}  // namespace retroportal::gpu
