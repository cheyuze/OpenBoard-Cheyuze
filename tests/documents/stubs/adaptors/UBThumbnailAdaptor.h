#pragma once
#include <QtGui>
#include "document/UBDocumentProxy.h"
class UBThumbnailAdaptor
{
public:
    static QPixmap get(std::shared_ptr<UBDocumentProxy>, int) { QPixmap image(8,8); image.fill(Qt::white); return image; }
    static QPixmap generateMissingThumbnail(std::shared_ptr<UBDocumentProxy> proxy, int index) { return get(proxy,index); }
    static QUrl thumbnailUrl(std::shared_ptr<UBDocumentProxy> proxy, int index) { return QUrl::fromLocalFile(proxy->path + QStringLiteral("/page%1.thumbnail.jpg").arg(index,3,10,QLatin1Char('0'))); }
    static void persistScene(std::shared_ptr<UBDocumentProxy>, std::shared_ptr<UBGraphicsScene>, int) {}
};
