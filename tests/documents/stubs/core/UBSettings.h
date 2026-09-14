#pragma once
#include <QtCore>
class UBSettings
{
public:
    inline static const QString documentUpdatedAt = QStringLiteral("updatedAt");
    inline static QString documentsPath;
    static QString userDocumentDirectory() { return documentsPath; }
    static constexpr int defaultThumbnailWidth = 160;
};
class UBStringUtils
{
public:
    static QString toUtcIsoDateTime(const QDateTime& date) { return date.toUTC().toString(Qt::ISODate); }
};
