#include "retroportal/ProcessHost.hpp"

#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <vector>

// Global process environment; must not live in an anonymous namespace (linker symbol).
extern char** environ;

namespace retroportal::proc {

namespace {

constexpr char const* kTag = "RetroPortalProc";

void AppendPipeRead(std::mutex* mu, std::string* buf, int fd, bool drain_all) {
    std::vector<char> tmp(16384);
    for (;;) {
        ssize_t const n = ::read(fd, tmp.data(), tmp.size());
        if (n > 0) {
            std::lock_guard<std::mutex> lock(*mu);
            buf->append(tmp.data(), static_cast<std::size_t>(n));
            if (!drain_all &&
                n < static_cast<ssize_t>(tmp.size())) {
                break;
            }
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        __android_log_print(ANDROID_LOG_WARN, kTag, "pipe read errno=%d", errno);
        break;
    }
}

int CountOnlineCpus() {
    long const n = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 0) {
        return 4;
    }
    if (n > CPU_SETSIZE) {
        return CPU_SETSIZE;
    }
    return static_cast<int>(n);
}

unsigned long ReadMaxFreqKhz(int cpu_index) {
    std::string const path =
        "/sys/devices/system/cpu/cpu" + std::to_string(cpu_index) +
        "/cpufreq/cpuinfo_max_freq";
    std::ifstream f(path);
    unsigned long hz = 0;
    if (f >> hz) {
        return hz;
    }
    return 0;
}

bool BuildBigCoreMask(cpu_set_t* out_set, int ncpus) {
    CPU_ZERO(out_set);
    if (ncpus <= 0) {
        return false;
    }
    std::vector<unsigned long> freqs(static_cast<std::size_t>(ncpus), 0);
    unsigned long maxf = 0;
    for (int i = 0; i < ncpus; ++i) {
        freqs[static_cast<std::size_t>(i)] = ReadMaxFreqKhz(i);
        if (freqs[static_cast<std::size_t>(i)] > maxf) {
            maxf = freqs[static_cast<std::size_t>(i)];
        }
    }
    if (maxf == 0) {
        int const start = std::max(0, ncpus / 2);
        for (int i = start; i < ncpus && i < CPU_SETSIZE; ++i) {
            CPU_SET(i, out_set);
        }
        return CPU_COUNT(out_set) > 0;
    }
    for (int i = 0; i < ncpus; ++i) {
        if (freqs[static_cast<std::size_t>(i)] == maxf) {
            CPU_SET(i, out_set);
        }
    }
    return CPU_COUNT(out_set) > 0;
}

}  // namespace

ProcessHost::ProcessHost() = default;

ProcessHost::~ProcessHost() {
    std::string ignored;
    (void)RequestTerminate(std::chrono::milliseconds(800), &ignored);
}

bool ProcessHost::GetExitedCode(int* out_code) const {
    int const st = raw_wait_status_.load();
    if (!WIFEXITED(st)) {
        return false;
    }
    if (out_code != nullptr) {
        *out_code = WEXITSTATUS(st);
    }
    return true;
}

void ProcessHost::ReaderLoop(int fd, bool is_err) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    while (running_.load()) {
        int const pr = ::poll(&pfd, 1, 150);
        if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
            AppendPipeRead(&io_mu_, is_err ? &stderr_buf_ : &stdout_buf_, fd,
                           false);
            if ((pfd.revents & POLLHUP) != 0) {
                break;
            }
        } else if (pr < 0 && errno != EINTR) {
            break;
        }
    }
    AppendPipeRead(&io_mu_, is_err ? &stderr_buf_ : &stdout_buf_, fd, true);
    ::close(fd);
}

void ProcessHost::ReapLoop() {
    int status = 0;
    for (;;) {
        int const r = ::waitpid(child_pid_, &status, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                                "waitpid failed errno=%d", errno);
            break;
        }
        if (r == child_pid_) {
            raw_wait_status_.store(status);
            break;
        }
    }
    running_.store(false);
    reap_cv_.notify_all();
}

bool ProcessHost::ApplyAffinity(int pid, Box64AffinityPolicy const& policy,
                                std::string* out_error) {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (!policy.explicit_cpu_list.empty()) {
        for (int c : policy.explicit_cpu_list) {
            if (c >= 0 && c < CPU_SETSIZE) {
                CPU_SET(c, &set);
            }
        }
    } else {
        int const n = CountOnlineCpus();
        if (policy.pin_to_big_cores) {
            if (!BuildBigCoreMask(&set, n)) {
                CPU_SET(std::max(0, n - 1), &set);
            }
        } else {
            for (int i = 0; i < n && i < CPU_SETSIZE; ++i) {
                CPU_SET(i, &set);
            }
        }
    }
    if (CPU_COUNT(&set) == 0) {
        CPU_SET(0, &set);
    }
    if (::sched_setaffinity(pid, sizeof(set), &set) != 0) {
        if (out_error) {
            *out_error =
                std::string("sched_setaffinity failed: ") + std::strerror(errno);
        }
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "Affinity not applied errno=%d", errno);
        return false;
    }
    return true;
}

SpawnResult ProcessHost::SpawnBox64Wine(SpawnConfig const& cfg) {
    SpawnResult result{};
    std::lock_guard<std::mutex> lock(state_mu_);
    if (running_.load()) {
        result.captured_stderr = "ProcessHost already running a child.";
        return result;
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        result.captured_stderr =
            std::string("pipe() failed: ") + std::strerror(errno);
        return result;
    }

    posix_spawn_file_actions_t fa{};
    if (::posix_spawn_file_actions_init(&fa) != 0) {
        result.captured_stderr = "posix_spawn_file_actions_init failed";
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        return result;
    }

    if (::posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDOUT_FILENO) !=
            0 ||
        ::posix_spawn_file_actions_adddup2(&fa, err_pipe[1],
                                             STDERR_FILENO) != 0 ||
        ::posix_spawn_file_actions_addclose(&fa, out_pipe[0]) != 0 ||
        ::posix_spawn_file_actions_addclose(&fa, err_pipe[0]) != 0) {
        result.captured_stderr = "posix_spawn_file_actions setup failed";
        ::posix_spawn_file_actions_destroy(&fa);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        return result;
    }

    std::unordered_map<std::string, std::string> kv;
    if (environ != nullptr) {
        for (char** e = environ; *e != nullptr; ++e) {
            std::string line = *e;
            auto pos = line.find('=');
            if (pos != std::string::npos) {
                kv[line.substr(0, pos)] = line.substr(pos + 1);
            }
        }
    }
    kv["WINEPREFIX"] = cfg.wine_prefix_dir;
    kv["BOX64_DYNAREC"] = cfg.enable_dynarec ? "1" : "0";
    kv["BOX64_LOG"] = "0";
    kv["BOX64_SHOWSEGV"] = "0";
    kv["BOX64_SHOWBT"] = "1";
    if (cfg.sync_rounding_for_games) {
        kv["BOX64_SYNC_ROUNDING"] = "1";
    }
    for (auto const& extra : cfg.environment) {
        kv[extra.first] = extra.second;
    }

    std::vector<std::string> env_rows;
    env_rows.reserve(kv.size());
    for (auto const& p : kv) {
        env_rows.push_back(p.first + "=" + p.second);
    }
    std::vector<char*> envp;
    envp.reserve(env_rows.size() + 1);
    for (auto& row : env_rows) {
        envp.push_back(row.data());
    }
    envp.push_back(nullptr);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(cfg.box64_executable.c_str()));
    argv.push_back(const_cast<char*>(cfg.wine_executable.c_str()));
    argv.push_back(const_cast<char*>(cfg.target_executable.c_str()));
    argv.push_back(nullptr);

    pid_t pid = -1;
    int const spawn_err =
        ::posix_spawn(&pid, cfg.box64_executable.c_str(), &fa, nullptr,
                      argv.data(), envp.data());

    ::posix_spawn_file_actions_destroy(&fa);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    if (spawn_err != 0) {
        result.captured_stderr =
            std::string("posix_spawn failed: ") + std::strerror(spawn_err);
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        return result;
    }

    std::string aff_err;
    (void)ApplyAffinity(static_cast<int>(pid), cfg.affinity, &aff_err);
    if (!aff_err.empty()) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", aff_err.c_str());
    }

    child_pid_ = static_cast<int>(pid);
    stdout_pipe_ = out_pipe[0];
    stderr_pipe_ = err_pipe[0];
    running_.store(true);

    stdout_thread_.emplace([this]() { ReaderLoop(stdout_pipe_, false); });
    stderr_thread_.emplace([this]() { ReaderLoop(stderr_pipe_, true); });
    reap_thread_.emplace([this]() { ReapLoop(); });

    result.started = true;
    result.pid = child_pid_;
    return result;
}

bool ProcessHost::RequestTerminate(std::chrono::milliseconds grace,
                                   std::string* out_error) {
    int pid = -1;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        pid = child_pid_;
    }
    if (pid <= 0) {
        return true;
    }
    if (::kill(pid, SIGTERM) != 0) {
        if (out_error) {
            *out_error = std::string("kill SIGTERM failed: ") +
                         std::strerror(errno);
        }
    }
    std::unique_lock<std::mutex> lk(state_mu_);
    bool const done = reap_cv_.wait_for(
        lk, grace, [this]() { return !running_.load(); });
    if (!done) {
        if (::kill(pid, SIGKILL) != 0 && out_error) {
            *out_error = std::string("kill SIGKILL failed: ") +
                         std::strerror(errno);
        }
        reap_cv_.wait_for(lk, std::chrono::milliseconds(400),
                          [this]() { return !running_.load(); });
    }

    if (stdout_thread_.has_value() && stdout_thread_->joinable()) {
        stdout_thread_->join();
        stdout_thread_.reset();
    }
    if (stderr_thread_.has_value() && stderr_thread_->joinable()) {
        stderr_thread_->join();
        stderr_thread_.reset();
    }
    if (reap_thread_.has_value() && reap_thread_->joinable()) {
        reap_thread_->join();
        reap_thread_.reset();
    }

    child_pid_ = -1;
    stdout_pipe_ = -1;
    stderr_pipe_ = -1;
    running_.store(false);
    return true;
}

std::string ProcessHost::DrainStdout() {
    std::lock_guard<std::mutex> lock(io_mu_);
    std::string out = std::move(stdout_buf_);
    stdout_buf_.clear();
    return out;
}

std::string ProcessHost::DrainStderr() {
    std::lock_guard<std::mutex> lock(io_mu_);
    std::string out = std::move(stderr_buf_);
    stderr_buf_.clear();
    return out;
}

}  // namespace retroportal::proc
