TEMPLATE = app
TARGET = polygon_zoom_regression
CONFIG += console c++17 release
CONFIG -= app_bundle debug
QT += core gui widgets
SOURCES += $$PWD/polygon_zoom_regression.cpp
INCLUDEPATH += $$OUT_PWD
DESTDIR = $$OUT_PWD
