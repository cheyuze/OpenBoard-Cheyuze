include(common.pri)
QT += core gui
TARGET = encoder-regression
!exists($$FFMPEG_ROOT/include/libavcodec/avcodec.h): error("Set FFMPEG_ROOT to a shared FFmpeg development installation")

# The deterministic microphone stub must precede production include paths.
INCLUDEPATH = $$quote($$PWD/stubs) $$quote($$OPENBOARD_ROOT/src) $$quote($$FFMPEG_ROOT/include) $$INCLUDEPATH
SOURCES += $$PWD/encoder_regression.cpp \
    $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBFFmpegVideoEncoder.cpp \
    $$OPENBOARD_ROOT/src/podcast/UBAbstractVideoEncoder.cpp
HEADERS += $$PWD/stubs/podcast/ffmpeg/UBMicrophoneInput.h \
    $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBFFmpegVideoEncoder.h \
    $$OPENBOARD_ROOT/src/podcast/UBAbstractVideoEncoder.h
LIBS += -L$$quote($$FFMPEG_ROOT/lib) -lavcodec -lavformat -lavutil -lswscale -lswresample
win32:LIBS += -lpsapi
win32:DEFINES += NOMINMAX
