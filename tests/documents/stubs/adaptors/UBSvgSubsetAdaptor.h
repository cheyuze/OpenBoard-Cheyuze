#pragma once
#include "document/UBDocumentProxy.h"
#include <functional>
class UBSvgSubsetAdaptor
{
public:
    inline static std::function<void()> afterUuid;
    static void setSceneUuid(std::shared_ptr<UBDocumentProxy>, int, const QUuid&) { if (afterUuid) afterUuid(); }
};
