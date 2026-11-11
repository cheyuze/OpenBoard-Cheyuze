#ifndef UBREGIONFILL_H
#define UBREGIONFILL_H

#include <QImage>
#include <QPainterPath>
#include <QRegion>
#include <QVector>
#include <algorithm>

// Bounded, non-destructive region detection. No document mutation or event
// pumping occurs here; an open region is an error, never a whole-page fill.
namespace UBRegionFill {
struct Result { QPainterPath path; QString error; };
inline QVector<quint8> dilate(const QVector<quint8>& mask, int w, int h, int radius)
{
    QVector<int> sums((w+1)*(h+1), 0);
    for (int y=0; y<h; ++y) {
        int row=0;
        for (int x=0; x<w; ++x) {
            row += mask[y*w+x];
            sums[(y+1)*(w+1)+x+1] = sums[y*(w+1)+x+1]+row;
        }
    }
    QVector<quint8> result(w*h, 0);
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
        const int l=std::max(0,x-radius), r=std::min(w,x+radius+1);
        const int t=std::max(0,y-radius), b=std::min(h,y+radius+1);
        result[y*w+x] = sums[b*(w+1)+r]-sums[t*(w+1)+r]-sums[b*(w+1)+l]+sums[t*(w+1)+l] > 0;
    }
    return result;
}
inline Result detect(const QImage& source, QPoint seed, int gapRadius=4)
{
    const int w=source.width(), h=source.height();
    if (w<3 || h<3 || w>1024 || h>1024 || !source.rect().contains(seed))
        return {{}, QStringLiteral("请在画布内选择区域")};
    const QImage image=source.convertToFormat(QImage::Format_RGB32);
    QVector<quint8> edge(w*h,0);
    for (int y=0; y<h; ++y) {
        const auto* row=reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x=0; x<w; ++x)
            edge[y*w+x] = std::min({qRed(row[x]),qGreen(row[x]),qBlue(row[x])}) < 175;
    }
    const int radius=std::clamp(gapRadius,0,12);
    const auto closed=dilate(edge,w,h,radius);
    const int start=seed.y()*w+seed.x();
    if (closed[start]) return {{},QStringLiteral("请点击区域内部，离边线稍远一些")};
    QVector<quint8> inside(w*h,0);
    QVector<int> queue; queue.reserve(w*h/4); queue.append(start); inside[start]=1;
    bool touchesEdge=false;
    for (int head=0; head<queue.size(); ++head) {
        const int p=queue[head], x=p%w, y=p/w;
        if (!x || !y || x==w-1 || y==h-1) { touchesEdge=true; break; }
        for (int n : {p-1,p+1,p-w,p+w}) if (!closed[n] && !inside[n]) {
            inside[n]=1; queue.append(n);
        }
    }
    if (touchesEdge || queue.size()>w*h*3/4)
        return {{},QStringLiteral("边界未闭合或区域过大，请使用圈选填充")};
    if (queue.size()<16) return {{},QStringLiteral("区域太小，请重新选择")};
    const auto expanded=dilate(inside,w,h,radius);
    QVector<QRect> runs;
    for (int y=0; y<h; ++y) {
        int x=0;
        while (x<w) {
            while (x<w && (!expanded[y*w+x] || edge[y*w+x])) ++x;
            const int begin=x;
            while (x<w && expanded[y*w+x] && !edge[y*w+x]) ++x;
            if (x>begin) runs.append(QRect(begin,y,x-begin,1));
        }
    }
    if (runs.size()>12000) return {{},QStringLiteral("边界过于复杂，请使用圈选填充")};
    QRegion region; region.setRects(runs.constData(),runs.size());
    QPainterPath path; path.addRegion(region); path=path.simplified();
    path.setFillRule(Qt::OddEvenFill);
    return {path,{}};
}
}
#endif
