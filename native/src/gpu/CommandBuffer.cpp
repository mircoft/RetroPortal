#include "retroportal/gpu/CommandBuffer.hpp"

namespace retroportal::gpu {

void CommandRecorder::BeginFrame(std::uint64_t frame_tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    queued_.clear();
    last_tag_ = frame_tag;
}

void CommandRecorder::Submit(cmd::Instruction const& ins) {
    std::lock_guard<std::mutex> lock(mutex_);
    queued_.push_back(ins);
}

bool CommandRecorder::Drain(std::vector<cmd::Instruction>* out_commands,
                            std::optional<std::uint64_t>* out_last_frame_tag) {
    if (out_commands == nullptr || out_last_frame_tag == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    *out_commands = std::move(queued_);
    queued_.clear();
    *out_last_frame_tag = last_tag_;
    return true;
}

bool ExecuteInstructions(GpuBackendKind /*backend*/,
                         std::vector<cmd::Instruction> const& ins,
                         SubmitFn gpu_submit,
                         std::string* out_error) {
    if (!gpu_submit) {
        if (out_error) {
            *out_error = "gpu_submit callback missing";
        }
        return false;
    }
    return gpu_submit(ins, out_error);
}

}  // namespace retroportal::gpu
