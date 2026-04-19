#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace retroportal::proc {

struct Box64AffinityPolicy {
    bool pin_to_big_cores = true;
    bool avoid_little_cores = true;
    std::vector<int> explicit_cpu_list;
};

struct SpawnConfig {
    std::string wine_prefix_dir;
    std::string box64_executable;
    std::string wine_executable;
    std::string target_executable;
    std::vector<std::pair<std::string, std::string>> environment;
    Box64AffinityPolicy affinity;
    bool enable_dynarec = true;
    bool sync_rounding_for_games = false;
};

struct SpawnResult {
    bool started = false;
    int pid = -1;
    std::string captured_stdout;
    std::string captured_stderr;
};

class ProcessHost {
public:
    ProcessHost();
    ~ProcessHost();

    ProcessHost(ProcessHost const&) = delete;
    ProcessHost& operator=(ProcessHost const&) = delete;

    SpawnResult SpawnBox64Wine(SpawnConfig const& cfg);
    bool IsRunning() const { return running_.load(); }
    bool RequestTerminate(std::chrono::milliseconds grace,
                          std::string* out_error);

    int GetRawWaitStatus() const { return raw_wait_status_.load(); }
    bool GetExitedCode(int* out_code) const;

    std::string DrainStdout();
    std::string DrainStderr();

private:
    void ReaderLoop(int fd, bool is_err);
    void ReapLoop();
    bool ApplyAffinity(int pid, Box64AffinityPolicy const& policy,
                       std::string* out_error);

    mutable std::mutex io_mu_;
    std::string stdout_buf_;
    std::string stderr_buf_;

    std::mutex state_mu_;
    std::condition_variable reap_cv_;
    std::atomic<bool> running_{false};
    std::atomic<int> raw_wait_status_{0};
    int child_pid_ = -1;
    int stdout_pipe_ = -1;
    int stderr_pipe_ = -1;
    std::optional<std::thread> stdout_thread_;
    std::optional<std::thread> stderr_thread_;
    std::optional<std::thread> reap_thread_;
};

}  // namespace retroportal::proc
