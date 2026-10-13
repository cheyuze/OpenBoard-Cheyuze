#ifndef UBIMAGETRANSFORM_H
#define UBIMAGETRANSFORM_H

#include <QLineF>
#include <QRectF>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>

// Image-only geometry. Transforms stay in the existing serialized/undoable
// QGraphicsItem transform; no image resampling or recording-path work.
namespace UBImageTransform
{
enum Handle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

inline QPointF point(const QRectF& r, int handle)
{
    switch (handle) {
    case TopLeft: return r.topLeft();
    case Top: return {r.center().x(), r.top()};
    case TopRight: return r.topRight();
    case Right: return {r.right(), r.center().y()};
    case BottomRight: return r.bottomRight();
    case Bottom: return {r.center().x(), r.bottom()};
    case BottomLeft: return r.bottomLeft();
    default: return {r.left(), r.center().y()};
    }
}

inline QTransform resized(const QRectF& bounds, const QTransform& initial,
        int handle, const QPointF& localDelta, bool keepRatio,
        qreal minX = 0.01, qreal minY = 0.01)
{
    if (bounds.width() <= 0 || bounds.height() <= 0 || handle < 0 || handle > 7)
        return initial;
    const QPointF anchor = point(bounds, (handle + 4) % 8);
    const QPointF vector = point(bounds, handle) - anchor;
    qreal sx = qFuzzyIsNull(vector.x()) ? 1 : (vector.x() + localDelta.x()) / vector.x();
    qreal sy = qFuzzyIsNull(vector.y()) ? 1 : (vector.y() + localDelta.y()) / vector.y();
    if (keepRatio) {
        qreal scale = 1;
        if (qFuzzyIsNull(vector.x())) scale = sy;
        else if (qFuzzyIsNull(vector.y())) scale = sx;
        else scale = QPointF::dotProduct(vector + localDelta, vector)
                   / QPointF::dotProduct(vector, vector);
        sx = sy = std::max(scale, std::max(minX, minY));
    } else {
        sx = std::max(sx, minX);
        sy = std::max(sy, minY);
    }
    if (!std::isfinite(sx) || !std::isfinite(sy)) return initial;
    QTransform local;
    local.translate(anchor.x(), anchor.y());
    local.scale(sx, sy);
    local.translate(-anchor.x(), -anchor.y());
    return local * initial;
}

inline qreal angle(const QTransform& transform)
{
    const QPointF axis = transform.map(QPointF(1, 0)) - transform.map(QPointF(0, 0));
    return qRadiansToDegrees(std::atan2(axis.y(), axis.x()));
}

inline qreal normalizeAngle(qreal angle)
{
    angle = std::fmod(angle + 180, 360);
    return (angle < 0 ? angle + 360 : angle) - 180;
}

inline QTransform rotated(const QRectF& bounds, const QTransform& initial, qreal degrees)
{
    const QPointF center = initial.map(bounds.center());
    QTransform rotation;
    rotation.translate(center.x(), center.y());
    rotation.rotate(degrees);
    rotation.translate(-center.x(), -center.y());
    return initial * rotation;
}

inline QTransform flipped(const QRectF& bounds, const QTransform& initial, bool horizontal)
{
    QTransform flip;
    flip.translate(bounds.center().x(), bounds.center().y());
    flip.scale(horizontal ? -1 : 1, horizontal ? 1 : -1);
    flip.translate(-bounds.center().x(), -bounds.center().y());
    return flip * initial;
}
}
#endif
