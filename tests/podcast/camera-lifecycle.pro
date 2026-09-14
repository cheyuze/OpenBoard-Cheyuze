include(common.pri)
QT += core gui widgets multimedia
TARGET = camera-lifecycle-test
INCLUDEPATH += $$quote($$OPENBOARD_ROOT/src/podcast) $$quote($$MOC_DIR)
SOURCES += $$PWD/camera_lifecycle_test.cpp
win32:LIBS += -luser32

# Same single-translation-unit private-state injection as microphone-buffer.
CAMERA_MOC_HEADERS = $$OPENBOARD_ROOT/src/podcast/UBCameraPreviewWindow.h
qtPrepareLibExecTool(PODCAST_TEST_MOC, moc)
camera_moc.input = CAMERA_MOC_HEADERS
camera_moc.output = $$MOC_DIR/moc_UBCameraPreviewWindow.cpp
camera_moc.commands = $$PODCAST_TEST_MOC ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
camera_moc.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += camera_moc
