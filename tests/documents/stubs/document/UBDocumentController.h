#pragma once
#include "core/UBSettings.h"
#include "document/UBDocumentProxy.h"
class UBDocumentController
{
public:
    static QString tr(const char* text) { return QString::fromUtf8(text); }
};
