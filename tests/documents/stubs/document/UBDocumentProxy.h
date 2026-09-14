#pragma once
#include <QtCore>
class UBDocumentProxy
{
public:
    UBDocumentProxy(QString directory, int pages) : path(directory), count(pages) {}
    QString persistencePath() const { return path; }
    int pageCount() const { return count; }
    void setPageCount(int value) { count = value; }
    QString name() const { return QStringLiteral("test document"); }
    void setMetaData(const QString&, const QVariant&) {}
    QString path;
    int count;
};
class UBGraphicsScene {};
