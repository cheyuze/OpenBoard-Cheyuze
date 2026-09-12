include(common.pri)
QT += core gui multimedia
TARGET = microphone-buffer-test
INCLUDEPATH += $$quote($$OPENBOARD_ROOT/src/podcast/ffmpeg) $$quote($$MOC_DIR)
SOURCES += $$PWD/microphone_buffer_test.cpp

# The harness includes the production implementation and moc in one translation
# unit so its private-state fault injection uses identical MSVC member symbols.
# Generate moc, but do not compile it as a second, duplicate object.
MICROPHONE_MOC_HEADERS = $$OPENBOARD_ROOT/src/podcast/ffmpeg/UBMicrophoneInput.h
qtPrepareLibExecTool(PODCAST_TEST_MOC, moc)
microphone_moc.input = MICROPHONE_MOC_HEADERS
microphone_moc.output = $$MOC_DIR/moc_UBMicrophoneInput.cpp
microphone_moc.commands = $$PODCAST_TEST_MOC ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
microphone_moc.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += microphone_moc
