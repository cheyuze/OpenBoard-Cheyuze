#pragma once
#include <QGraphicsRectItem>
#include <QtGui>
class UBThumbnail : public QGraphicsRectItem
{
public:
    UBThumbnail() { setFlag(ItemIsSelectable); }
    static qreal heightForWidth(qreal width) { return width * 0.75; }
    void setThumbnailSize(const QSizeF& size) { setRect(QRectF(QPointF(),size)); }
    void setColumn(int) {}
    void setRow(int) {}
    void setPixmap(const QPixmap&) {}
    void setSceneIndex(int index) { sceneNumber = index; }
    int sceneIndex() const { return sceneNumber; }
    void setPageName(const QString& value) { label = value; }
    void setDeletable(bool) {}
    int sceneNumber = -1;
    QString label;
};
