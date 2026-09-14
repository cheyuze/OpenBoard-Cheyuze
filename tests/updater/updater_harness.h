#pragma once
#include <QtWidgets>
#include <QtNetwork>
#include <QtConcurrent>
#include <QFutureWatcher>
#include "UBUpdateDownloadSupport.h"

extern QString updateTestDownloadDirectory;
extern int installerLaunches;
inline bool launchInstaller(QWidget *, const QString &) { ++installerLaunches; return false; }
class TestWindow : public QWidget
{
public:
    QString lastError;
    void information(const QString &, const QString &message) { lastError = message; }
};
class UBApplicationController : public QObject
{
public:
    TestWindow *mMainWindow;
    std::shared_ptr<UBUpdateDownloadSession> mUpdateDownloadSession;
    explicit UBApplicationController(TestWindow *window) : mMainWindow(window) {}
    ~UBApplicationController() override { stopUpdateDownload(); }
    void closeUpdateDownload(const std::shared_ptr<UBUpdateDownloadSession> &session);
    void stopUpdateDownload();
    void downloadUpdateInstaller(const QList<QUrl> &urls, const QString &version,
            const QString &expectedSha256, const QUrl &baiduUrl = QUrl(),
            const QString &baiduPassword = QString(), int urlIndex = 0,
            int retryAttempt = 0, QProgressDialog *progress = nullptr);
};
