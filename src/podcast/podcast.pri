
HEADERS      += src/podcast/UBPodcastController.h \
                src/podcast/UBAbstractVideoEncoder.h \
                src/podcast/UBPodcastRecordingPalette.h \
                
SOURCES      += src/podcast/UBPodcastController.cpp \
                src/podcast/UBAbstractVideoEncoder.cpp \
                src/podcast/UBPodcastRecordingPalette.cpp \

win32 {

    SOURCES  += src/podcast/ffmpeg/UBFFmpegVideoEncoder.cpp \
                src/podcast/ffmpeg/UBMicrophoneInput.cpp \
                src/podcast/windowsmedia/UBWaveRecorder.cpp

    HEADERS  += src/podcast/ffmpeg/UBFFmpegVideoEncoder.h \
                src/podcast/ffmpeg/UBMicrophoneInput.h \
                src/podcast/windowsmedia/UBWaveRecorder.h

    FFMPEG_SHARED_ROOT = $$PWD/../../thirdparty/ffmpeg/ffmpeg-9.0.1-full_build-shared
    INCLUDEPATH += $$FFMPEG_SHARED_ROOT/include
    LIBS += -L$$FFMPEG_SHARED_ROOT/lib -lavformat -lavcodec -lswscale -lswresample -lavutil
}

macx {

    SOURCES  += src/podcast/ffmpeg/UBFFmpegVideoEncoder.cpp \
                src/podcast/ffmpeg/UBMicrophoneInput.cpp

    HEADERS  += src/podcast/ffmpeg/UBFFmpegVideoEncoder.h \
                src/podcast/ffmpeg/UBMicrophoneInput.h

    LIBS += -lavformat -lavcodec -lswscale -lswresample -lavutil
}

linux-g++* {
    HEADERS  += src/podcast/ffmpeg/UBFFmpegVideoEncoder.h \
                src/podcast/ffmpeg/UBMicrophoneInput.h

    SOURCES  += src/podcast/ffmpeg/UBFFmpegVideoEncoder.cpp \
                src/podcast/ffmpeg/UBMicrophoneInput.cpp


    DEPENDPATH += /usr/lib/x86_64-linux-gnu

    LIBS += -lavformat -lavcodec -lswscale -lavutil \
            -lva-x11 \
            -lva \
            -lxcb-shm \
            -lxcb-xfixes \
            -lxcb-render -lxcb-shape -lxcb -lX11 -lasound -lSDL -lx264 -lpthread -lvpx -lvorbisenc -lvorbis -ltheoraenc -ltheoradec -logg -lopus -lmp3lame -lfreetype -lfdk-aac -lass -llzma -lbz2 -lz -ldl -lswresample -lswscale -lavutil -lm

    FFMPEG_VERSION = $$system(ffmpeg --version|& grep -oP "version.*?\K[0-9]\.[0-9]")
    equals(FFMPEG_VERSION, 2.8) {
        LIBS -= -lswresample
        LIBS += -lavresample
    }


    QMAKE_CXXFLAGS += -std=c++11 # move this to OpenBoard.pro when we can use C++11 on all platforms
}
