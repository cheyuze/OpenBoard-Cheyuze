include(common.pri)
QT += core gui widgets
TARGET = source-switch-test
INCLUDEPATH += $$quote($$MOC_DIR)
SOURCES += $$PWD/source_switch_test.cpp

# This test runs the production method against small Qt dependencies. The
# Windows build extracts the method verbatim rather than maintaining a copy.
!win32:error("source-switch.pro currently requires Windows PowerShell for source extraction")
SOURCE_SWITCH_INPUT = $$OPENBOARD_ROOT/src/podcast/UBPodcastController.cpp
source_switch.input = SOURCE_SWITCH_INPUT
source_switch.output = $$MOC_DIR/source_switch_method.inc
SOURCE_SWITCH_EXTRACTOR = $$relative_path($$PWD/extract-source-switch.ps1, $$OUT_PWD)
source_switch.commands = powershell.exe -NoProfile -ExecutionPolicy Bypass -File $$shell_quote($$shell_path($$SOURCE_SWITCH_EXTRACTOR)) -Source ${QMAKE_FILE_IN} -Destination ${QMAKE_FILE_OUT}
source_switch.depends = $$PWD/extract-source-switch.ps1
source_switch.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += source_switch
