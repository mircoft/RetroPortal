#include "retroportal/gpu/D3DTranslator.hpp"

#include <android/log.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

#include <algorithm>

namespace retroportal::gpu {

namespace {

constexpr char const* kTag = "RetroPortalD3D";

bool EnvEqualsIgnoreCase(char const* val, char const* expected) {
    if (val == nullptr || expected == nullptr) {
        return false;
    }
    while (*val != '\0' && *expected != '\0') {
        unsigned char const cv =
            static_cast<unsigned char>(*val++);
        unsigned char const ce =
            static_cast<unsigned char>(*expected++);
        if (std::tolower(cv) != std::tolower(ce)) {
            return false;
        }
    }
    return *val == *expected;
}

bool EnvFlagTrue(char const* key) {
    char const* v = std::getenv(key);
    if (v == nullptr || v[0] == '\0') {
        return false;
    }
    return std::strcmp(v, "1") == 0 || EnvEqualsIgnoreCase(v, "true");
}

bool EnvFlagFalse(char const* key) {
    char const* v = std::getenv(key);
    if (v == nullptr || v[0] == '\0') {
        return false;
    }
    return std::strcmp(v, "0") == 0 || EnvEqualsIgnoreCase(v, "false");
}

}  // namespace

D3DTranslator::D3DTranslator() = default;

D3DTranslator::~D3DTranslator() {
    Shutdown();
}

bool D3DTranslator::ProbeRuntime(DxBackendPreference preference,
                                 TranslatorCapabilities* caps,
                                 std::string* out_error) {
    if (caps == nullptr) {
        if (out_error != nullptr) {
            *out_error = "caps null";
        }
        return false;
    }
    caps->dxvk_supported = EnvFlagTrue("RETROPORTAL_DXVK_AVAILABLE");
    caps->zink_supported = EnvFlagTrue("RETROPORTAL_ZINK_AVAILABLE");
    caps->vma_supported = EnvFlagTrue("RETROPORTAL_VMA_AVAILABLE");

    switch (preference) {
        case DxBackendPreference::DXVK_NATIVE:
            if (!caps->dxvk_supported) {
                if (out_error != nullptr) {
                    *out_error =
                        "DXVK backend requested but RETROPORTAL_DXVK_AVAILABLE "
                        "not set.";
                }
                return false;
            }
            break;
        case DxBackendPreference::ZINK_NATIVE:
            if (!caps->zink_supported) {
                if (out_error != nullptr) {
                    *out_error =
                        "Zink backend requested but RETROPORTAL_ZINK_AVAILABLE "
                        "not set.";
                }
                return false;
            }
            break;
        case DxBackendPreference::INTERNAL_MINIMAL:
            break;
    }
    return true;
}

bool D3DTranslator::Initialize(std::optional<DxBackendPreference> forced_backend,
                                 std::string* out_error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }
    DxBackendPreference pref = DxBackendPreference::INTERNAL_MINIMAL;
    if (forced_backend.has_value()) {
        pref = *forced_backend;
    } else if (EnvFlagTrue("RETROPORTAL_FORCE_DXVK")) {
        pref = DxBackendPreference::DXVK_NATIVE;
    } else if (EnvFlagTrue("RETROPORTAL_FORCE_ZINK")) {
        pref = DxBackendPreference::ZINK_NATIVE;
    }

    TranslatorCapabilities probe{};
    if (!ProbeRuntime(pref, &probe, out_error)) {
        if (pref != DxBackendPreference::INTERNAL_MINIMAL) {
            __android_log_print(
                ANDROID_LOG_WARN, kTag,
                "Falling back to INTERNAL_MINIMAL translator path.");
            pref = DxBackendPreference::INTERNAL_MINIMAL;
            if (!ProbeRuntime(pref, &probe, out_error)) {
                return false;
            }
        } else {
            return false;
        }
    }

    caps_ = probe;
    active_ = pref;
    initialized_ = true;
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "D3D translator active backend enum=%u",
                        static_cast<unsigned>(active_));
    return true;
}

void D3DTranslator::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    caps_ = TranslatorCapabilities{};
    active_ = DxBackendPreference::INTERNAL_MINIMAL;
}

bool D3DTranslator::EmitPresentPassCommands(CommandRecorder* recorder,
                                            DxPresentState const& state,
                                            std::string* out_error) {
    if (recorder == nullptr) {
        if (out_error != nullptr) {
            *out_error = "recorder null";
        }
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        if (out_error != nullptr) {
            *out_error = "translator not initialized";
        }
        return false;
    }

    cmd::Instruction clear{};
    clear.opcode = cmd::Opcode::PresentClear;
    cmd::ClearPayload c{};
    c.rgba[0] = 0.f;
    c.rgba[1] = 0.f;
    c.rgba[2] = EnvFlagFalse("RETROPORTAL_PRESENT_DEBUG") ? 0.f : 0.08f;
    c.rgba[3] = 1.f;
    clear.payload = c;

    cmd::Instruction blit{};
    blit.opcode = cmd::Opcode::PresentBlitFramebuffer;
    cmd::BlitPayload b{};
    b.width_px = state.backbuffer_width;
    b.height_px = state.backbuffer_height;
    b.vsync = true;
    blit.payload = b;

    cmd::Instruction uniforms{};
    uniforms.opcode = cmd::Opcode::ApplyStretchUniforms;
    cmd::StretchUniformPayload su{};
    su.viewport_width =
        state.backbuffer_width != 0 ? static_cast<float>(state.backbuffer_width)
                                    : 1920.f;
    su.viewport_height =
        state.backbuffer_height != 0 ? static_cast<float>(state.backbuffer_height)
                                      : 1080.f;
    {
        float const vw = std::max(su.viewport_width, 1.f);
        float const vh = std::max(su.viewport_height, 1.f);
        su.source_aspect = vw / vh;
    }
    su.guard_band = 0.72f;
    su.edge_stretch_strength =
        EnvFlagTrue("RETROPORTAL_EDGE_STRETCH_HARD") ? 0.65f : 0.35f;
    uniforms.payload = su;

    recorder->Submit(clear);
    recorder->Submit(blit);
    recorder->Submit(uniforms);
    return true;
}

}  // namespace retroportal::gpu
