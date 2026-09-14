#pragma once
#include <QGraphicsView>
class UBThumbnailArranger
{
public:
    qreal thumbnailWidth() const { return 160; }
    qreal availableViewWidth() const { return 320; }
    int columnCount() const { return 1; }
    QMarginsF margins() const { return {}; }
    QSizeF spacing() const { return {}; }
    QGraphicsView* thumbnailView() const { return nullptr; }
};
