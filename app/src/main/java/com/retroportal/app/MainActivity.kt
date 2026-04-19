package com.retroportal.app

import android.os.Bundle
import android.view.MotionEvent
import androidx.appcompat.app.AppCompatActivity
import com.retroportal.app.databinding.ActivityMainBinding
import com.retroportal.engine.NativeEngine

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val ok = NativeEngine.nativeInit(filesDir.absolutePath)
        if (!ok) {
            finish()
            return
        }

        binding.gameSurface.rawMotionListener = { event ->
            val rx = event.getAxisValue(MotionEvent.AXIS_RELATIVE_X)
            val ry = event.getAxisValue(MotionEvent.AXIS_RELATIVE_Y)
            NativeEngine.nativeInjectRelativePointer(rx, ry)
        }

        binding.inputOverlay.bringToFront()
    }

    override fun onDestroy() {
        NativeEngine.nativeShutdown()
        super.onDestroy()
    }
}
