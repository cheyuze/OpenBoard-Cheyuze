#pragma once
#include <QtCore>
class UBFileSystemUtils
{
public:
    inline static bool forceDependencyFailure = false;
    static QString digitFileFormat(const QString& format, int index) { return format.arg(index,3,10,QLatin1Char('0')); }
    static bool copyDir(const QString& source, const QString& target)
    {
        if (forceDependencyFailure || !QDir().mkpath(target)) return false;
        for (const QFileInfo& entry : QDir(source).entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot))
        {
            const QString to = QDir(target).filePath(entry.fileName());
            if (entry.isDir() ? !copyDir(entry.absoluteFilePath(), to) : (!QFile::exists(to) && !QFile::copy(entry.absoluteFilePath(),to)))
                return false;
        }
        return true;
    }
};
