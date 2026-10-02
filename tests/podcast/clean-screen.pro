include(common.pri)
QT += core gui widgets
TARGET = clean-screen-test
SOURCES += $$PWD/clean_screen_test.cpp $$OPENBOARD_ROOT/src/podcast/UBCleanScreenRecording.cpp
HEADERS += $$OPENBOARD_ROOT/src/podcast/UBCleanScreenRecording.h
win32:LIBS += -luser32 -ldwmapi
