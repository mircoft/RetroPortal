# Third-party dependencies

RetroPortal integrates native components as **pinned Git submodules** or **prefab binaries** consumed by CMake:

| Component | Purpose | Integration |
|-----------|---------|-------------|
| Box64 | x86\_64 → AArch64 JIT | Spawned executable from JNI `ProcessHost`; pin revision in CI. |
| Wine | Win32 syscall personality | Combined with Box64 via `posix_spawn`; `WINEPREFIX` rooted under app storage. |
| Vulkan / ANGLE loaders | GLES and Vulkan backends | Loaded at runtime via `dlopen` in `GpuBackend.cpp`. |
| DXVK-native / Mesa Zink headers | Future D3D translation | Vendor headers copied under `third_party/gralloc_headers/` when licensed; linked per backend policy. |

Add submodules **after** license review:

```bash
git submodule add https://github.com/ptitSeb/box64.git third_party/box64
```

Wine on Android follows the same patching model as established Wine-on-Android ports; consume a verified tree rather than fetching at app runtime.
