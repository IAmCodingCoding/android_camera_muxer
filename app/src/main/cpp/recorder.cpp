#include <jni.h>
#include <string>
#include <mutex>
#include <android/log.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/imgutils.h>
}


AVFormatContext *out_format = nullptr;
int video_index = 0;
int audio_index = 0;
std::mutex write_lock;

AVCodecContext *videoEncoder = nullptr;
AVCodecContext *audioEncoder = nullptr;

AVAudioFifo *fifo_buffer = nullptr;

AVFrame *video_frame = nullptr;
AVPacket *video_packet = nullptr;

AVFrame *audio_frame = nullptr;
AVPacket *audio_packet = nullptr;


extern "C"
JNIEXPORT jint JNICALL
Java_com_example_myapplication_MediaRecorder_init(JNIEnv *env, jobject thiz, jstring _url,
                                                  jint pix_format,
                                                  jint width, jint height, jint fps,
                                                  jint video_bit_rate, jint sample_format,
                                                  jint sample_rate, jint channels,
                                                  jint audio_bit_rate) {
    const char *url = "/sdcard/output.mp4";
    int ret = avformat_alloc_output_context2(&out_format, nullptr, nullptr, url);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "zmy", "fail to alloc output context\n");
        return ret;
    }
    if (!(out_format->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&out_format->pb, url, AVIO_FLAG_WRITE, nullptr, nullptr);
        if (ret < 0) {
            __android_log_print(ANDROID_LOG_DEBUG, "zmy", "fail to open io context:%s\n",
                                av_err2str(ret));
        }
    }
    AVCodec *video_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (video_codec == nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "zmy", "not supported encoder:%d\n",
                            AV_CODEC_ID_H264);
        return -1;
    }
    videoEncoder = avcodec_alloc_context3(video_codec);
    videoEncoder->pix_fmt = (AVPixelFormat) pix_format;
    videoEncoder->width = width;
    videoEncoder->height = height;
    videoEncoder->time_base = {1, fps};
    videoEncoder->max_b_frames = 0;
    videoEncoder->bit_rate = video_bit_rate;
    videoEncoder->gop_size = 15;
    videoEncoder->thread_count = 8;
    if (out_format->oformat->flags & AVFMT_GLOBALHEADER) {
        videoEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    AVDictionary *p = nullptr;
    av_dict_set(&p, "preset", "fast", 0);
    av_dict_set(&p, "tune", "zerolatency", 0);
    ret = avcodec_open2(videoEncoder, videoEncoder->codec, &p);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "zmy", "fail to open encoder:%s\n",
                            video_codec->long_name);
        return ret;
    }
    video_frame = av_frame_alloc();
    av_image_alloc(video_frame->data, video_frame->linesize, width, width, videoEncoder->pix_fmt,
                   1);
    video_frame->width = width;
    video_frame->height = height;
    video_frame->format = videoEncoder->pix_fmt;
    video_packet = av_packet_alloc();


    AVStream *video_stream = avformat_new_stream(out_format, nullptr);
    ret = avcodec_parameters_from_context(video_stream->codecpar, videoEncoder);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "zmy",
                            "fail to fill paramters from video encoder\n");
        return ret;
    }
    video_index = video_stream->index;

    AVCodec *audio_codec = avcodec_find_encoder_by_name("libfdk_aac");
    if (audio_codec == nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "zmy", "not supported encoder:libfdk_aac\n");
        return -1;
    }
    audioEncoder = avcodec_alloc_context3(audio_codec);
    audioEncoder->sample_fmt = static_cast<AVSampleFormat>(sample_format);
    audioEncoder->time_base = {1, sample_rate};
    audioEncoder->bit_rate = audio_bit_rate;
    audioEncoder->sample_rate = sample_rate;
    audioEncoder->channels = channels;
    audioEncoder->channel_layout = (uint64_t) (av_get_default_channel_layout(channels));
    if (out_format->oformat->flags & AVFMT_GLOBALHEADER) {
        audioEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    ret = avcodec_open2(audioEncoder, audio_codec, nullptr);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "zmy", "fail to open encoder:%s\n",
                            audio_codec->long_name);
        return ret;
    }

    audio_frame = av_frame_alloc();
    audio_frame->sample_rate = sample_format;
    audio_frame->format = audioEncoder->sample_fmt;
    audio_frame->channels = channels;
    audio_frame->channel_layout = audioEncoder->channel_layout;
    av_samples_alloc(audio_frame->data, audio_frame->linesize, audio_frame->channels,
                     audioEncoder->frame_size,
                     (AVSampleFormat) audio_frame->format, 1);
    audio_packet = av_packet_alloc();


    AVStream *audio_stream = avformat_new_stream(out_format, nullptr);
    ret = avcodec_parameters_from_context(audio_stream->codecpar, audioEncoder);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "zmy",
                            "fail to fill paramters from audio encoder\n");
        return ret;
    }
    audio_index = audio_stream->index;


    fifo_buffer = av_audio_fifo_alloc((AVSampleFormat) sample_format, channels,
                                      audioEncoder->frame_size);
    ret = avformat_write_header(out_format, nullptr);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "zmy", "fail to write header\n");
    }
    return ret;
}


extern "C"
JNIEXPORT void JNICALL
Java_com_example_myapplication_MediaRecorder_write_1video_1data(JNIEnv *env, jobject thiz,
                                                                jobject _y, jint stride_y,
                                                                jobject _u, jint stride_u,
                                                                jobject _v, jint stride_v,
                                                                jlong time) {
    uint8_t *y = (uint8_t *) env->GetDirectBufferAddress(_y);
    uint8_t *u = (uint8_t *) env->GetDirectBufferAddress(_u);
    uint8_t *v = (uint8_t *) env->GetDirectBufferAddress(_v);
    int line_size[4] = {stride_y, stride_u, stride_v};
    const uint8_t *yuv[4] = {y, u, v};
    av_image_copy(video_frame->data, video_frame->linesize, yuv, line_size, videoEncoder->pix_fmt,
                  videoEncoder->width, videoEncoder->height);

    avcodec_send_frame(videoEncoder, video_frame);
    while (avcodec_receive_packet(videoEncoder, video_packet) >= 0) {
        video_packet->pts = (int64_t) ((time / 1000.0) /
                                       av_q2d(out_format->streams[video_index]->time_base));
        video_packet->stream_index = video_index;
        std::unique_lock<std::mutex> lock(write_lock);
        av_interleaved_write_frame(out_format, video_packet);
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_myapplication_MediaRecorder_write_1audio_1data(JNIEnv *env, jobject thiz,
                                                                jobject data, jint len,
                                                                jlong time) {
    uint8_t *audio_data = (uint8_t *) env->GetDirectBufferAddress(data);
    void *raw_audio_data[] = {audio_data};
    int nb_samples =
            (len / audioEncoder->channels) / av_get_bytes_per_sample(audioEncoder->sample_fmt);
    time -= (long) ((av_audio_fifo_size(fifo_buffer) / (double) audioEncoder->sample_rate) * 1000);
    av_audio_fifo_write(fifo_buffer, raw_audio_data, nb_samples);
    while (av_audio_fifo_size(fifo_buffer) >= audioEncoder->frame_size) {
        int read_samples = av_audio_fifo_read(fifo_buffer, (void **) audio_frame->data,
                                              audioEncoder->frame_size);
        audio_frame->nb_samples = read_samples;
        avcodec_send_frame(audioEncoder, audio_frame);
        int count = 0;
        while (avcodec_receive_packet(audioEncoder, audio_packet) >= 0) {
            int real_time = (int) (time + (count++) * audioEncoder->frame_size /
                                          audioEncoder->sample_rate * 1000);
            audio_packet->pts = (int64_t) ((real_time / 1000.0) / av_q2d(audioEncoder->time_base));
            audio_packet->stream_index = audio_index;
            std::unique_lock<std::mutex> lock(write_lock);
            av_interleaved_write_frame(out_format, audio_packet);
        }
        time += (read_samples / (double) audioEncoder->sample_rate) * 1000;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_myapplication_MediaRecorder_end(JNIEnv *env, jobject thiz) {
    av_write_trailer(out_format);
    if (!(out_format->flags & AVFMT_NOFILE)) {
        avio_close(out_format->pb);
    }
    avformat_free_context(out_format);
    avcodec_free_context(&audioEncoder);
    avcodec_free_context(&videoEncoder);
    av_audio_fifo_free(fifo_buffer);

    av_freep(&video_frame->data[0]);
    av_frame_free(&video_frame);
    av_packet_free(&video_packet);

    av_freep(&audio_frame->data[0]);
    av_frame_free(&audio_frame);
    av_packet_free(&audio_packet);

}