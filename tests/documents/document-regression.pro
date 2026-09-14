QT += core gui widgets testlib core5compat
CONFIG += console c++17 testcase release
CONFIG -= app_bundle debug
TEMPLATE = app
TARGET = document-regression
isEmpty(REPO): REPO = $$clean_path($$PWD/../..)
REPO = $$clean_path($$REPO)
isEmpty(THIRDPARTY): THIRDPARTY = $$clean_path($$REPO/../OpenBoard-ThirdParty)
INCLUDEPATH = $$PWD/stubs $$REPO/src $$INCLUDEPATH
DEPENDPATH = $$PWD/stubs $$REPO/src
SOURCES += $$PWD/document-regression.cpp $$PWD/test-support.cpp \
    $$REPO/src/adaptors/UBImportDocument.cpp \
    $$REPO/src/document/UBDocument.cpp \
    $$REPO/src/gui/UBThumbnailScene.cpp \
    $$REPO/src/frameworks/UBBackgroundLoader.cpp
HEADERS += $$REPO/src/adaptors/UBImportDocument.h \
    $$REPO/src/adaptors/UBImportAdaptor.h \
    $$REPO/src/gui/UBThumbnailScene.h \
    $$REPO/src/frameworks/UBBackgroundLoader.h
include($$THIRDPARTY/quazip/quazip.pri)
QMAKE_CXXFLAGS += /utf-8
QMAKE_CFLAGS += /utf-8
