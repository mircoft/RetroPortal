package com.retroportal.engine

/**
 * JNI façade to the native Box64/Wine orchestration layer, guest VM manager,
 * and GPU command submission.
 */
object NativeEngine {
    init {
        System.loadLibrary("retroportal_native")
    }

    external fun nativeInit(filesDir: String): Boolean

    external fun nativeShutdown()

    external fun nativeGpuInitialize(): Boolean

    external fun nativeVmAllocateRegion(
        guestBase: Long,
        sizeBytes: Long,
        permMask: Int,
    ): Boolean

    external fun nativeSpawnWine(
        winePrefix: String,
        box64Path: String,
        winePath: String,
        exePath: String,
        envLines: Array<String>,
    ): Int

    external fun nativeDrainStdout(): String

    external fun nativeDrainStderr(): String

    external fun nativeTerminateProcess(graceMs: Long): Boolean

    external fun nativeVmStatsJson(): String

    external fun nativeGpuSubmitTestFrame(): Boolean

    external fun nativeInjectKey(androidKeyCode: Int, down: Boolean)

    external fun nativeInjectRelativePointer(rx: Float, ry: Float)

    const val PERM_READ: Int = 1
    const val PERM_WRITE: Int = 2
    const val PERM_EXEC: Int = 4
}
