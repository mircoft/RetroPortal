package com.retroportal.app.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.AttributeSet
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import androidx.core.content.getSystemService
import com.retroportal.engine.NativeEngine

private data class VirtualControl(
    val label: String,
    val bounds: RectF,
    val androidKeyCode: Int,
)

private const val MAX_TOUCH_POINTERS = 10

/**
 * Virtual controls supporting up to ten simultaneous pointers with per-press haptics.
 */
class InputOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    private val buttons = mutableListOf<VirtualControl>()
    private val pointerHits = arrayOfNulls<VirtualControl>(MAX_TOUCH_POINTERS)
    private val outlinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = resources.displayMetrics.density * 2f
        color = 0x88FFFFFF.toInt()
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        color = 0x44FFFFFF
    }

    private val vibrator: Vibrator? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            context.getSystemService<VibratorManager>()?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            context.getSystemService()
        }

    init {
        isClickable = true
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        rebuildLayout(w.toFloat(), h.toFloat())
    }

    private fun rebuildLayout(width: Float, height: Float) {
        buttons.clear()
        val pad = width * 0.06f
        val btn = width * 0.12f
        val bottom = height - pad * 2

        buttons += VirtualControl(
            "↑",
            RectF(pad, bottom - btn * 3f, pad + btn, bottom - btn * 2f),
            KeyEvent.KEYCODE_DPAD_UP,
        )
        buttons += VirtualControl(
            "←",
            RectF(pad, bottom - btn * 2f, pad + btn, bottom - btn),
            KeyEvent.KEYCODE_DPAD_LEFT,
        )
        buttons += VirtualControl(
            "→",
            RectF(pad + btn * 1.2f, bottom - btn * 2f, pad + btn * 2.2f, bottom - btn),
            KeyEvent.KEYCODE_DPAD_RIGHT,
        )
        buttons += VirtualControl(
            "Act",
            RectF(width - pad - btn * 2f, bottom - btn * 2f, width - pad, bottom),
            KeyEvent.KEYCODE_ENTER,
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        for (b in buttons) {
            canvas.drawRoundRect(b.bounds, 12f, 12f, fillPaint)
            canvas.drawRoundRect(b.bounds, 12f, 12f, outlinePaint)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val pointerCount = event.pointerCount.coerceAtMost(MAX_TOUCH_POINTERS)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN,
            MotionEvent.ACTION_POINTER_DOWN,
            -> {
                val idx = event.actionIndex
                val pid = event.getPointerId(idx)
                if (pid >= MAX_TOUCH_POINTERS) {
                    return true
                }
                val x = event.getX(idx)
                val y = event.getY(idx)
                hitTest(x, y)?.let { vb ->
                    pointerHits[pid] = vb
                    triggerHaptic()
                    NativeEngine.nativeInjectKey(vb.androidKeyCode, true)
                }
            }

            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until pointerCount) {
                    val pid = event.getPointerId(i)
                    if (pid >= MAX_TOUCH_POINTERS) continue
                    val prev = pointerHits[pid]
                    val x = event.getX(i)
                    val y = event.getY(i)
                    val now = hitTest(x, y)
                    if (prev != null && now != prev) {
                        NativeEngine.nativeInjectKey(prev.androidKeyCode, false)
                        pointerHits[pid] = null
                        now?.let { n ->
                            pointerHits[pid] = n
                            triggerHaptic()
                            NativeEngine.nativeInjectKey(n.androidKeyCode, true)
                        }
                    }
                }
            }

            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_POINTER_UP,
            -> {
                val idx = event.actionIndex
                val pid = event.getPointerId(idx)
                pointerHits[pid]?.let { vb ->
                    NativeEngine.nativeInjectKey(vb.androidKeyCode, false)
                }
                pointerHits[pid] = null
            }

            MotionEvent.ACTION_CANCEL -> {
                for (idx in pointerHits.indices) {
                    pointerHits[idx]?.let { vb ->
                        NativeEngine.nativeInjectKey(vb.androidKeyCode, false)
                    }
                    pointerHits[idx] = null
                }
            }
        }
        return true
    }

    private fun hitTest(x: Float, y: Float): VirtualControl? {
        for (b in buttons) {
            if (b.bounds.contains(x, y)) return b
        }
        return null
    }

    private fun triggerHaptic() {
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            vibrator?.vibrate(
                VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK),
            )
        } else {
            @Suppress("DEPRECATION")
            vibrator?.vibrate(16)
        }
    }

}
