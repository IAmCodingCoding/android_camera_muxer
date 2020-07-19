package com.example.myapplication

import android.Manifest
import android.graphics.Matrix
import android.hardware.camera2.CaptureRequest
import android.media.AudioFormat.CHANNEL_IN_STEREO
import android.media.AudioFormat.ENCODING_PCM_16BIT
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Bundle
import android.util.Log
import android.util.Range
import android.util.Size
import android.view.Surface
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.camera2.Camera2Config
import androidx.camera.core.*
import com.tbruyelle.rxpermissions2.RxPermissions
import io.reactivex.disposables.Disposable
import kotlinx.android.synthetic.main.activity_main.*
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {
    companion object {
        const val PIX_FMT_YUV420P = 0
        const val PIX_FMT_NV12 = 23

        const val SAMPLE_FMT_FLT = 1
    }

    private var audio_thread: Thread? = null
    private var isCameraWorking: Boolean = false;
    private var audioRecord: AudioRecord? = null
    private var dispose: Disposable? = null
    private val rxPermissions = RxPermissions(this)
    private val executor = Executors.newFixedThreadPool(3)
    private var isRecording = true
    private val recorder = com.example.myapplication.MediaRecorder()


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        dispose = rxPermissions.request(
            Manifest.permission.CAMERA,
            Manifest.permission.WRITE_EXTERNAL_STORAGE,
            Manifest.permission.RECORD_AUDIO
        ).subscribe {
            init()
            startPreview()
            startRecordAudio()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        isRecording = false
        audio_thread?.join()
        recorder.end()
    }

    private fun init() {
        recorder.init(
            "/sdcard/output.mp4", PIX_FMT_YUV420P, 640, 480, 30, (1024 * 1034 * 0.5).toInt()
            , SAMPLE_FMT_FLT, 44100, 2, 64000
        )
    }

    private fun startRecordAudio() {
        audio_thread = thread {
            val recordBufSize = AudioRecord.getMinBufferSize(
                44100,
                CHANNEL_IN_STEREO,
                ENCODING_PCM_16BIT
            )  //audioRecord能接受的最小的buffer大小
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                44100,
                CHANNEL_IN_STEREO,
                ENCODING_PCM_16BIT,
                recordBufSize
            )
            val buffer = ByteBuffer.allocateDirect(recordBufSize)
            audioRecord?.startRecording()
            while (isRecording) {
                buffer.clear()
                val len = audioRecord?.read(buffer, recordBufSize)
                if (isCameraWorking) {
                    recorder.write_audio_data(buffer, len!!, getTime())
                }
            }
            audioRecord?.release()
        }
    }

    private var startTime = 0L
    @Synchronized
    private fun getTime(): Long {
        return if (startTime == 0L) {
            startTime = System.currentTimeMillis()
            0L
        } else {
            System.currentTimeMillis() - startTime
        }
    }

    private fun startPreview() {
        texture_view.post {
            startCamera()
        }
    }


    private fun startCamera() {
        val analyzerConfig = ImageAnalysisConfig.Builder().apply {
            setTargetResolution(Size(480, 640))
            setLensFacing(CameraX.LensFacing.FRONT)
            setImageReaderMode(
                ImageAnalysis.ImageReaderMode.ACQUIRE_LATEST_IMAGE
            )
        }
        val analyzerUseCase = ImageAnalysis(analyzerConfig.build()).apply {
            setAnalyzer(executor,
                ImageAnalysis.Analyzer { image, rotationDegrees ->
                    isCameraWorking = true;
                    var planes = image.image?.planes
                    planes?.let {
                        recorder.write_video_data(
                            it[0].buffer, it[0].rowStride
                            , it[1].buffer, it[1].rowStride
                            , it[2].buffer, it[2].rowStride
                            , getTime()
                        )
                    }
                })
        }

        // Create configuration object for the texture_view use case
        val previewConfig = PreviewConfig.Builder().apply {
            setTargetResolution(Size(480, 640))
            setLensFacing(CameraX.LensFacing.FRONT)
        }
        Camera2Config.Extender(previewConfig)
            .setCaptureRequestOption(CaptureRequest.FLASH_MODE, CaptureRequest.FLASH_MODE_OFF)
            .setCaptureRequestOption(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(30, 30))

        val preview = Preview(previewConfig.build())

        // Every time the texture_view is updated, recompute layout
        preview.setOnPreviewOutputUpdateListener {
            // To update the SurfaceTexture, we have to remove it and re-add it
            val parent = texture_view.parent as ViewGroup
            parent.removeView(texture_view)
            parent.addView(texture_view, 0)
            texture_view.surfaceTexture = it.surfaceTexture
            updateTransform()
        }

        // Bind use cases to lifecycle
        // If Android Studio complains about "this" being not a LifecycleOwner
        // try rebuilding the project or updating the appcompat dependency to
        // version 1.1.0 or higher.
        CameraX.bindToLifecycle(this, preview, analyzerUseCase)

    }

    private fun updateTransform() {
        val matrix = Matrix()

        // Compute the center of the view finder
        val centerX = texture_view.width / 2f
        val centerY = texture_view.height / 2f

        // Correct preview output to account for display rotation
        val rotationDegrees = when (texture_view.display.rotation) {
            Surface.ROTATION_0 -> 0
            Surface.ROTATION_90 -> 90
            Surface.ROTATION_180 -> 180
            Surface.ROTATION_270 -> 270
            else -> return
        }
        matrix.postRotate(-rotationDegrees.toFloat(), centerX, centerY)
        Log.d("zmy", "updateTransform----rotationDegrees=$rotationDegrees")
        // Finally, apply transformations to our TextureView
        texture_view.setTransform(matrix)
    }

}
