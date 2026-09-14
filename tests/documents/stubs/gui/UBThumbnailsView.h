#pragma once
#include <QGraphicsView>
#include "gui/UBThumbnailArranger.h"
class UBThumbnailsView : public QGraphicsView
{
public:
    UBThumbnailArranger* thumbnailArranger() const { return nullptr; }
};
