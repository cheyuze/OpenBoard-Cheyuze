TEMPLATE = app
TARGET = eraser_preview_regression
CONFIG += console c++17 release
CONFIG -= app_bundle debug
QT += core gui widgets
SOURCES += $$PWD/eraser_preview_regression.cpp
INCLUDEPATH += $$OUT_PWD
DESTDIR = $$OUT_PWD
