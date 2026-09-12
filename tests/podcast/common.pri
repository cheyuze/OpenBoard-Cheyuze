TEMPLATE = app
CONFIG += console c++17 release
CONFIG -= app_bundle debug debug_and_release

# The projects live under <repository>/tests/podcast. Overrides let the same
# harness verify a separately staged source checkout without modifying it.
isEmpty(OPENBOARD_ROOT): OPENBOARD_ROOT = $$(OPENBOARD_ROOT)
isEmpty(OPENBOARD_ROOT): OPENBOARD_ROOT = $$clean_path($$PWD/../..)
!exists($$OPENBOARD_ROOT/src/podcast/UBAbstractVideoEncoder.h): error("OPENBOARD_ROOT must point to the OpenBoard source checkout")
INCLUDEPATH += $$quote($$OPENBOARD_ROOT/src)

DESTDIR = $$OUT_PWD/bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
win32-msvc*: QMAKE_CXXFLAGS += /utf-8

isEmpty(FFMPEG_ROOT): FFMPEG_ROOT = $$(FFMPEG_ROOT)
isEmpty(FFMPEG_ROOT): FFMPEG_ROOT = $$OPENBOARD_ROOT/thirdparty/ffmpeg/ffmpeg-9.0.1-full_build-shared
