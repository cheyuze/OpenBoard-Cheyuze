#ifndef UB_LINE_TRANSFORM_H
#define UB_LINE_TRANSFORM_H
#include <QLineF>
#include <QTransform>
#include <cmath>

namespace UBLineTransform
{
// Map endpoints while preserving the transverse stroke width. Unlike an
// image-box scale, a longer segment must not become a thicker segment.
inline QTransform between(const QLineF& from,const QLineF& to)
{
    if(from.length()<.0001||to.length()<.0001)return QTransform();
    const QPointF u=(from.p2()-from.p1())/from.length();
    const QPointF v(-u.y(),u.x());
    const QPointF a=(to.p2()-to.p1())/to.length();
    const QPointF b(-a.y(),a.x());
    const qreal ratio=to.length()/from.length();
    const qreal m11=a.x()*ratio*u.x()+b.x()*v.x(),m12=a.y()*ratio*u.x()+b.y()*v.x();
    const qreal m21=a.x()*ratio*u.y()+b.x()*v.y(),m22=a.y()*ratio*u.y()+b.y()*v.y();
    const QPointF offset=to.p1()-QPointF(m11*from.x1()+m21*from.y1(),m12*from.x1()+m22*from.y1());
    return QTransform(m11,m12,m21,m22,offset.x(),offset.y());
}
}
#endif
