#ifndef UBGRAPHICSPIXMAPITEMDELEGATE_H
#define UBGRAPHICSPIXMAPITEMDELEGATE_H

#include "UBGraphicsItemDelegate.h"
#include <QPointer>

class QGraphicsProxyWidget;
class UBGraphicsPixmapItem;

class UBGraphicsPixmapItemDelegate : public UBGraphicsItemDelegate
{
public:
    explicit UBGraphicsPixmapItemDelegate(UBGraphicsPixmapItem* item);
    ~UBGraphicsPixmapItemDelegate() override;
    void createControls() override;
    void freeControls() override;
    bool controlsExist() const override;
    void positionHandles() override;
    bool mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    bool mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    bool mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    bool keyPressEvent(QKeyEvent* event) override;
    void remove(bool canUndo = true) override;
    void openMenu(const QPoint& globalPosition);
    void setInteractionActive(bool active);

private:
    void createActions();
    void createMenu();
    void editGeometry();
    void applyTransform(const QTransform& transform);
    void rotate(qreal degrees);
    QPointer<QGraphicsProxyWidget> mImageToolbar;
    QAction* mGeometryAction = nullptr;
    QAction* mRotateAction = nullptr;
    QAction* mCopyAction = nullptr;
    bool mInteracting = false;
    bool mDragging = false;
    QPointF mPressScene;
    QPointF mPressPosition;
};
#endif
