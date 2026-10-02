#pragma once
#include <QtWidgets>
#include <QtNetwork>
#include "UBUpdateSources.h"

struct TestSetting { QVariant get() const { return QStringLiteral("https://raw.githubusercontent.com/cheyuze/OpenBoard-Cheyuze/main/update.json"); } };
struct UBSettings
{
    TestSetting setting;
    TestSetting *appSoftwareUpdateURL = &setting;
    static UBSettings *settings() { static UBSettings instance; return &instance; }
};
extern int metadataErrors;
inline void showUpdateInformation(QWidget *, const QString &, const QString &) { ++metadataErrors; }
class UBApplicationController : public QObject
{
public:
    QWidget *mMainWindow = nullptr;
    bool mCheckingForUpdates = false;
    bool isNoUpdateDisplayed = true;
    QNetworkAccessManager *mNetworkAccessManager;
    int manifestsReceived = 0;
    explicit UBApplicationController(QNetworkAccessManager *manager) : mNetworkAccessManager(manager)
    {
        connect(manager, &QNetworkAccessManager::finished, this, &UBApplicationController::updateRequestFinished);
    }
    void checkUpdate(const QUrl &url = QUrl(), const QList<QUrl> &urls = {}, int urlIndex = 0,
                     int retryAttempt = 0, int redirectCount = 0);
    void updateRequestFinished(QNetworkReply *reply);
    void downloadJsonFinished(QString) { ++manifestsReceived; }
};
