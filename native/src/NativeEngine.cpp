#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "retroportal/ProcessHost.hpp"
#include "retroportal/VirtualMemoryManager.hpp"
#include "retroportal/gpu/CommandBuffer.hpp"
#include "retroportal/gpu/D3DTranslator.hpp"
#include "retroportal/gpu/GpuBackend.hpp"

namespace {

constexpr char const* kJniTag = "RetroPortalJNI";

std::unique_ptr<retroportal::vm::VirtualMemoryManager> g_vm;
std::unique_ptr<retroportal::proc::ProcessHost> g_proc;
std::unique_ptr<retroportal::gpu::GpuPresentationPipeline> g_gpu;
std::unique_ptr<retroportal::gpu::D3DTranslator> g_d3d;
std::unique_ptr<retroportal::gpu::CommandRecorder> g_recorder;

std::atomic<int> g_last_spawn_pid{-1};

std::string JStringToUtf8(JNIEnv* env, jstring js) {
    if (js == nullptr) {
        return {};
    }
    char const* utf =
        env->GetStringUTFChars(js, nullptr);
    if (utf == nullptr) {
        return {};
    }
    std::string out = utf;
    env->ReleaseStringUTFChars(js, utf);
    return out;
}

retroportal::vm::PagePerm MaskToPerm(jint mask) {
    using retroportal::vm::PagePerm;
    PagePerm p = PagePerm::None;
    if ((mask & 1) != 0) {
        p = p | PagePerm::Read;
    }
    if ((mask & 2) != 0) {
        p = p | PagePerm::Write;
    }
    if ((mask & 4) != 0) {
        p = p | PagePerm::Exec;
    }
    return p;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) !=
        JNI_OK) {
        return JNI_ERR;
    }
    (void)env;
    __android_log_print(ANDROID_LOG_INFO, kJniTag, "retroportal_native loaded.");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_retroportal_engine_NativeEngine_nativeInit(JNIEnv* env, jobject,
                                                    jstring files_dir) {
    std::string const root = JStringToUtf8(env, files_dir);
    (void)root;
    std::string err;
    if (!g_vm) {
        g_vm = std::make_unique<retroportal::vm::VirtualMemoryManager>();
    }
    if (!g_vm->Init(&err)) {
        __android_log_print(ANDROID_LOG_ERROR, kJniTag, "%s", err.c_str());
        g_vm.reset();
        return JNI_FALSE;
    }

    g_vm->SetInvalidationHook([](std::uintptr_t base, std::size_t len) {
        __android_log_print(ANDROID_LOG_VERBOSE, kJniTag,
                              "JIT invalidation hook base=%zx len=%zu",
                              static_cast<std::size_t>(base), len);
    });

    if (!g_proc) {
        g_proc = std::make_unique<retroportal::proc::ProcessHost>();
    }
    if (!g_gpu) {
        g_gpu = std::make_unique<retroportal::gpu::GpuPresentationPipeline>();
    }
    if (!g_d3d) {
        g_d3d = std::make_unique<retroportal::gpu::D3DTranslator>();
    }
    if (!g_recorder) {
        g_recorder = std::make_unique<retroportal::gpu::CommandRecorder>();
    }

    std::string gpu_err;
    if (!g_gpu->Initialize(&gpu_err)) {
        __android_log_print(ANDROID_LOG_WARN, kJniTag, "GPU init: %s",
                            gpu_err.c_str());
    }

    std::string dx_err;
    if (!g_d3d->Initialize(std::nullopt, &dx_err)) {
        __android_log_print(ANDROID_LOG_WARN, kJniTag, "D3D translator: %s",
                            dx_err.c_str());
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_retroportal_engine_NativeEngine_nativeShutdown(JNIEnv*, jobject) {
    std::string ignored;
    if (g_proc) {
        g_proc->RequestTerminate(std::chrono::milliseconds(1200), &ignored);
        g_proc.reset();
    }
    if (g_d3d) {
        g_d3d->Shutdown();
        g_d3d.reset();
    }
    if (g_gpu) {
        g_gpu->Shutdown();
        g_gpu.reset();
    }
    g_recorder.reset();
    g_vm.reset();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_retroportal_engine_NativeEngine_nativeGpuInitialize(JNIEnv*,
                                                             jobject) {
    std::string err;
    if (!g_gpu) {
        return JNI_FALSE;
    }
    return g_gpu->Initialize(&err) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_retroportal_engine_NativeEngine_nativeVmAllocateRegion(
    JNIEnv*, jobject, jlong guest_base, jlong size_bytes, jint perm_mask) {
    if (!g_vm) {
        return JNI_FALSE;
    }
    std::string err;
    retroportal::vm::PagePerm const perm = MaskToPerm(perm_mask);
    bool ok = g_vm->AllocateGuestRegion(static_cast<std::uintptr_t>(guest_base),
                                       static_cast<std::size_t>(size_bytes),
                                       perm, &err);
    if (!ok) {
        __android_log_print(ANDROID_LOG_ERROR, kJniTag, "%s", err.c_str());
    }
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_retroportal_engine_NativeEngine_nativeSpawnWine(
    JNIEnv* env, jobject, jstring wine_prefix, jstring box64_path,
    jstring wine_path, jstring exe_path, jobjectArray env_lines) {
    if (!g_proc) {
        return -1;
    }

    retroportal::proc::SpawnConfig cfg{};
    cfg.wine_prefix_dir = JStringToUtf8(env, wine_prefix);
    cfg.box64_executable = JStringToUtf8(env, box64_path);
    cfg.wine_executable = JStringToUtf8(env, wine_path);
    cfg.target_executable = JStringToUtf8(env, exe_path);

    jsize const n = env->GetArrayLength(env_lines);
    for (jsize i = 0; i < n; ++i) {
        jstring js = static_cast<jstring>(
            env->GetObjectArrayElement(env_lines, i));
        std::string line = JStringToUtf8(env, js);
        env->DeleteLocalRef(js);
        auto eq = line.find('=');
        if (eq != std::string::npos && eq > 0) {
            cfg.environment.emplace_back(line.substr(0, eq),
                                         line.substr(eq + 1));
        }
    }

    retroportal::proc::SpawnResult r = g_proc->SpawnBox64Wine(cfg);
    if (!r.started) {
        __android_log_print(ANDROID_LOG_ERROR, kJniTag, "spawn failed: %s",
                            r.captured_stderr.c_str());
        return -2;
    }
    g_last_spawn_pid.store(r.pid);
    return static_cast<jint>(r.pid);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_retroportal_engine_NativeEngine_nativeDrainStdout(JNIEnv* env,
                                                           jobject) {
    if (!g_proc) {
        return env->NewStringUTF("");
    }
    std::string s = g_proc->DrainStdout();
    return env->NewStringUTF(s.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_retroportal_engine_NativeEngine_nativeDrainStderr(JNIEnv* env,
                                                           jobject) {
    if (!g_proc) {
        return env->NewStringUTF("");
    }
    std::string s = g_proc->DrainStderr();
    return env->NewStringUTF(s.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_retroportal_engine_NativeEngine_nativeTerminateProcess(JNIEnv*,
                                                                jobject,
                                                                jlong grace_ms) {
    if (!g_proc) {
        return JNI_FALSE;
    }
    std::string err;
    auto ms = std::chrono::milliseconds(
        grace_ms > 0 ? grace_ms : static_cast<jlong>(800));
    return g_proc->RequestTerminate(ms, &err) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_retroportal_engine_NativeEngine_nativeVmStatsJson(JNIEnv* env,
                                                         jobject) {
    if (!g_vm) {
        return env->NewStringUTF("{}");
    }
    retroportal::vm::PageTableStats st = g_vm->GetStats();
    std::ostringstream oss;
    oss << "{"
        << "\"host_page_size\":" << st.host_page_size << ","
        << "\"guest_page_size\":" << st.guest_page_size << ","
        << "\"tracked_guest_pages\":" << st.tracked_guest_pages << ","
        << "\"userfaultfd_active\":" << (st.userfaultfd_active ? "true" : "false")
        << ","
        << "\"mprotect_shim_active\":"
        << (st.mprotect_shim_active ? "true" : "false") << "}";
    std::string const s = oss.str();
    return env->NewStringUTF(s.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_retroportal_engine_NativeEngine_nativeInjectKey(JNIEnv*, jobject,
                                                         jint key_code,
                                                         jboolean down) {
    __android_log_print(ANDROID_LOG_DEBUG, kJniTag,
                        "Inject synthetic key=%d down=%d",
                        static_cast<int>(key_code),
                        down == JNI_TRUE ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL
Java_com_retroportal_engine_NativeEngine_nativeInjectRelativePointer(
    JNIEnv*, jobject, jfloat rx, jfloat ry) {
    __android_log_print(ANDROID_LOG_VERBOSE, kJniTag,
                        "Relative pointer rx=%.4f ry=%.4f", rx, ry);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_retroportal_engine_NativeEngine_nativeGpuSubmitTestFrame(JNIEnv*,
                                                                  jobject) {
    if (!g_gpu || !g_d3d || !g_recorder) {
        return JNI_FALSE;
    }
    std::string err;
    g_recorder->BeginFrame(1);
    retroportal::gpu::DxPresentState ps{};
    ps.backbuffer_width = 640;
    ps.backbuffer_height = 480;
    if (!g_d3d->EmitPresentPassCommands(g_recorder.get(), ps, &err)) {
        __android_log_print(ANDROID_LOG_ERROR, kJniTag, "%s", err.c_str());
        return JNI_FALSE;
    }
    std::vector<retroportal::gpu::cmd::Instruction> batch;
    std::optional<std::uint64_t> tag;
    if (!g_recorder->Drain(&batch, &tag)) {
        return JNI_FALSE;
    }
    return g_gpu->SubmitFrameCommands(batch, &err) ? JNI_TRUE : JNI_FALSE;
}

}  // namespace
