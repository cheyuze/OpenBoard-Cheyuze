TEMPLATE = app
TARGET = input_state_regression
CONFIG += console c++17 release
CONFIG -= app_bundle debug
QT += core gui widgets
SOURCES += $$PWD/input_state_regression.cpp
INCLUDEPATH += $$OUT_PWD
DESTDIR = $$OUT_PWD
