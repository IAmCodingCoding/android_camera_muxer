cmake_minimum_required(VERSION 3.4.1)

include_directories(. ffmpeg)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${CMAKE_ANDROID_ARCH_ABI})
add_library(
        media_recorder
        SHARED
        recorder.cpp)

target_link_libraries(
        media_recorder
        log
        avcodec
        avfilter
        avformat
        avutil
        fdk-aac
        openh264
        x264
        postproc
        swresample
        swscale
)