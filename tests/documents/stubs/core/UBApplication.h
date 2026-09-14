#pragma once
#include <QtCore>
#include "gui/UBMainWindow.h"
class UBApplication
{
public:
    inline static bool isClosing = false;
    inline static UBMainWindow window;
    inline static UBMainWindow* mainWindow = &window;
    inline static QString lastMessage;
    static void showMessage(const QString& text, bool = false) { lastMessage = text; }
};
