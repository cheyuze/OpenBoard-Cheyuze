#ifndef UBUPDATEDOWNLOADSUPPORT_H
#define UBUPDATEDOWNLOADSUPPORT_H

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkReply>
#include <QPointer>
#include <QProgressDialog>
#include <QSaveFile>
#include <atomic>

// One session outlives individual requests and retry timers. Cancellation must
// also reach file verification/copy work, which runs away from the GUI thread.
struct UBUpdateDownloadSession
{
    QPointer<QProgressDialog> progress;
    QPointer<QNetworkReply> reply;
    std::atomic_bool canceled{false};
    bool cacheChecked = false;
    bool finishing = false;

    bool canContinue() const { return !canceled.load(); }
};

namespace UBUpdateDownloadSupport
{
    enum class Result { Complete, InvalidHash, IoError, Canceled };

    inline Result verifyFile(const QString &path, const QString &expected,
                             const std::atomic_bool &canceled)
    {
        if (canceled.load())
            return Result::Canceled;
        if (expected.trimmed().size() != 64)
            return Result::InvalidHash;
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly))
            return Result::IoError;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        // Heap allocation: a 1 MiB local array exhausted the Windows GUI
        // thread's entire 1 MiB stack at the end of every successful download.
        QByteArray buffer(64 * 1024, Qt::Uninitialized);
        while (!input.atEnd())
        {
            if (canceled.load())
                return Result::Canceled;
            const qint64 count = input.read(buffer.data(), buffer.size());
            if (count <= 0)
                return Result::IoError;
            hash.addData(QByteArray::fromRawData(buffer.constData(), static_cast<int>(count)));
        }
        if (canceled.load())
            return Result::Canceled;
        if (input.error() != QFileDevice::NoError)
            return Result::IoError;
        return QString::fromLatin1(hash.result().toHex()).compare(expected.trimmed(), Qt::CaseInsensitive) == 0
                ? Result::Complete : Result::InvalidHash;
    }

    inline bool fileMatchesSha256(const QString &path, const QString &expected,
                                  const std::atomic_bool &canceled)
    {
        return verifyFile(path, expected, canceled) == Result::Complete;
    }

    inline Result promoteDownloadedFile(const QString &partialPath, const QString &destination,
                                         const std::atomic_bool &canceled)
    {
        if (canceled.load())
            return Result::Canceled;
        QFile input(partialPath);
        if (!input.open(QIODevice::ReadOnly))
            return Result::IoError;
        QSaveFile output(destination);
        // Keep atomic replacement: a failure must not damage an older installer.
        if (!output.open(QIODevice::WriteOnly))
            return Result::IoError;
        QByteArray buffer(64 * 1024, Qt::Uninitialized);
        while (!input.atEnd())
        {
            if (canceled.load())
                return Result::Canceled;
            const qint64 count = input.read(buffer.data(), buffer.size());
            if (count <= 0 || output.write(buffer.constData(), count) != count)
                return Result::IoError;
        }
        if (canceled.load())
            return Result::Canceled;
        if (input.error() != QFileDevice::NoError || !output.commit())
            return Result::IoError;
        input.close();
        QFile::remove(partialPath);
        return Result::Complete;
    }

    inline Result verifyAndPromote(const QString &partialPath, const QString &destination,
                                   const QString &expected, const std::atomic_bool &canceled)
    {
        const Result verified = verifyFile(partialPath, expected, canceled);
        if (verified != Result::Complete)
            return verified;
        return promoteDownloadedFile(partialPath, destination, canceled);
    }
}

#endif
