#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace retroportal::gpu {

enum class GpuBackendKind : std::uint8_t {
    Unknown = 0,
    Vulkan = 1,
    GLES3 = 2,
};

namespace cmd {

struct ClearPayload {
    float rgba[4] = {0, 0, 0, 1};
};

struct BlitPayload {
    std::uintptr_t guest_src_address = 0;
    std::uint32_t width_px = 0;
    std::uint32_t height_px = 0;
    bool vsync = true;
};

struct StretchUniformPayload {
    float viewport_width = 0.f;
    float viewport_height = 0.f;
    float source_aspect = (4.f / 3.f);
    float guard_band = 0.72f;
    float edge_stretch_strength = 0.35f;
};

using OpPayload =
    std::variant<std::monostate, ClearPayload, BlitPayload,
                 StretchUniformPayload>;

enum class Opcode : std::uint32_t {
    Nop = 0,
    PresentClear = 1,
    PresentBlitFramebuffer = 2,
    ApplyStretchUniforms = 3,
};

struct Instruction {
    Opcode opcode = Opcode::Nop;
    OpPayload payload{};
};

}  // namespace cmd

class CommandRecorder {
public:
    void BeginFrame(std::uint64_t frame_tag);
    void Submit(cmd::Instruction const& ins);

    bool Drain(std::vector<cmd::Instruction>* out_commands,
               std::optional<std::uint64_t>* out_last_frame_tag);

private:
    std::mutex mutex_{};
    std::vector<cmd::Instruction> queued_{};
    std::optional<std::uint64_t> last_tag_{};
};

using SubmitFn =
    std::function<bool(std::vector<cmd::Instruction> const&, std::string*)>;

bool ExecuteInstructions(GpuBackendKind backend,
                         std::vector<cmd::Instruction> const& ins,
                         SubmitFn gpu_submit,
                         std::string* out_error);

}  // namespace retroportal::gpu
