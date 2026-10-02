include(common.pri)
QT += core gui multimedia
TARGET = windows-audio-test
SOURCES += $$PWD/windows_audio_test.cpp \
    $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBWindowsAudioInput.cpp \
    $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBMicrophoneInput.cpp
SOURCES += $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBFFmpegVideoEncoder.cpp \
    $$OPENBOARD_ROOT/src/podcast/UBAbstractVideoEncoder.cpp
HEADERS += $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBMicrophoneInput.h \
    $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBWindowsAudioInput.h
HEADERS += $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBFFmpegVideoEncoder.h \
    $$OPENBOARD_ROOT/src/podcast/UBAbstractVideoEncoder.h
INCLUDEPATH += $$quote($$FFMPEG_ROOT/include)
LIBS += -L$$quote($$FFMPEG_ROOT/lib) -lswresample -lavutil -lole32 -lavrt -luuid
LIBS += -lavcodec -lavformat -lswscale
DEFINES += NOMINMAX
