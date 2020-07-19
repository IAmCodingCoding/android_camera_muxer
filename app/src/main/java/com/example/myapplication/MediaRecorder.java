package com.example.myapplication;

import java.nio.ByteBuffer;

public class MediaRecorder {
    static {
        System.loadLibrary("media_recorder");
    }

    public native int init(String url, int pix_format, int width, int height, int fps, int video_bit_rate
            , int sample_format, int sample_rate, int channels, int audio_bit_rate);

    public native void write_video_data(ByteBuffer _y, int stride_y, ByteBuffer _u, int stride_u, ByteBuffer _v, int stride_v, long time);

    public native void write_audio_data(ByteBuffer data, int len, long time);

    public native void end();
}
