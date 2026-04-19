package com.retroportal.app.ui

import android.content.Context
import android.os.Build
import android.util.AttributeSet
import android.util.Log
import android.view.MotionEvent
import android.view.View

/**
 * Host surface for gameplay rendering and relative mouse deltas when a pointer is captured.
 * Uses [requestPointerCapture] on Android O+ so FPS-style mouse look stays bounded to this view.
 */
class GameSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    var rawMotionListener: ((MotionEvent) -> Unit)? = null

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (hasPointerCapture() &&
            (event.source and android.view.InputDevice.SOURCE_MOUSE) != 0
        ) {
            rawMotionListener?.invoke(event)
            return true
        }
        return super.dispatchGenericMotionEvent(event)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    try {
                        if (!hasPointerCapture()) {
                            requestPointerCapture()
                            Log.i(TAG, "Pointer capture requested for mouse-look.")
                        }
                    } catch (e: Exception) {
                        Log.w(TAG, "Pointer capture unavailable", e)
                    }
                }
                parent.requestDisallowInterceptTouchEvent(true)
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && hasPointerCapture()) {
                    try {
                        releasePointerCapture()
                    } catch (e: Exception) {
                        Log.w(TAG, "releasePointerCapture failed", e)
                    }
                }
            }
        }
        return true
    }

    companion object {
        private const val TAG = "GameSurfaceView"
    }
}
