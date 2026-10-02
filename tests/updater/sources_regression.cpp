#include "metadata_harness.h"
#include <stdexcept>
#include <functional>

int metadataErrors = 0;
static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static const QString site = "https://xiwang.cheyuze.top/openboard/releases/1.9.1/OpenBoard-cheyuze-1.9.1-x64.exe";
static const QString github = "https://github.com/cheyuze/OpenBoard-Cheyuze/releases/download/v1.9.1/OpenBoard-cheyuze-1.9.1-x64.exe";
static QByteArray manifest()
{
    return QJsonDocument(QJsonObject{{"version", "1.9.1"}, {"sha256", QString(64, 'a')},
        {"websiteUrl", site}, {"url", github}, {"urls", QJsonArray{site, github, "https://ghproxy.net/" + github}}}).toJson();
}
struct Answer { QByteArray body = manifest(); QUrl redirect; bool error = false; };
class Reply : public QNetworkReply
{
public:
    QByteArray body;
    qint64 offset = 0;
    Reply(const QNetworkRequest &request, Answer answer, QObject *parent) : QNetworkReply(parent), body(answer.body)
    {
        setUrl(request.url()); setRequest(request); setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this, answer] {
            if (answer.error) { setError(ConnectionRefusedError, "Synthetic offline source"); }
            if (!answer.redirect.isEmpty()) setAttribute(QNetworkRequest::RedirectionTargetAttribute, answer.redirect);
            emit readyRead();
            if (!isFinished()) { setFinished(true); emit finished(); }
        });
    }
    qint64 bytesAvailable() const override { return body.size() - offset + QNetworkReply::bytesAvailable(); }
    void abort() override { if (!isFinished()) { setError(OperationCanceledError, "Aborted"); setFinished(true); emit finished(); } }
    qint64 readData(char *data, qint64 max) override
    {
        const qint64 count = qMin(max, qint64(body.size()) - offset);
        if (!count) return -1;
        memcpy(data, body.constData() + offset, size_t(count)); offset += count; return count;
    }
};
class Manager : public QNetworkAccessManager
{
public:
    QList<QUrl> requests;
    std::function<Answer(const QUrl &)> answer = [](const QUrl &) { return Answer{}; };
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        require(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy,
                "metadata redirects must be validated before following");
        requests.append(request.url()); return new Reply(request, answer(request.url()), this);
    }
};
static void finish(UBApplicationController &controller)
{
    QElapsedTimer timer; timer.start();
    while (controller.mCheckingForUpdates && timer.elapsed() < 8000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    require(!controller.mCheckingForUpdates, "metadata chain never finished");
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        require(UBUpdateSources::validManifest(manifest()), "valid release rejected");
        require(!UBUpdateSources::validManifest("<html>version url login</html>"), "HTML accepted as metadata");
        require(!UBUpdateSources::validManifest("{\"version\":\"1.9.1\",\"url\":\"https://github.com\"}"), "hashless manifest accepted");
        const auto json = QJsonDocument::fromJson(manifest()).object();
        require(UBUpdateSources::installers(json).first() == QUrl(site), "website is not preferred");
        require(UBUpdateSources::installers(json, false).first() == QUrl(github), "GitHub choice ignored");
        require(UBUpdateSources::installers(json).size() == 3, "deduplication or mirror fallback failed");
        const auto sources = UBUpdateSources::manifests(QUrl("https://raw.githubusercontent.com/cheyuze/OpenBoard-Cheyuze/main/update.json"));
        require(sources.first() == UBUpdateSources::websiteManifest(), "old profile still prefers GitHub");
        for (const QString &bad : {"http://xiwang.cheyuze.top/openboard/update.json", "https://xiwang.cheyuze.top.evil.test/openboard/update.json",
             "https://xiwang.cheyuze.top/auth/login", "https://user@xiwang.cheyuze.top/openboard/update.json",
             "https://xiwang.cheyuze.top:444/openboard/update.json", "https://ghproxy.net/https://github.com/update.json"})
            require(!UBUpdateSources::trustedManifest(QUrl(bad)), "untrusted manifest URL allowed");
        require(!UBUpdateSources::allowedInstaller(QUrl("file:///tmp/setup.exe")), "local installer URL allowed");
        require(!UBUpdateSources::allowedInstaller(QUrl("https://evil.test/setup.exe")), "untrusted installer allowed");
        qInfo() << "PASS source ordering, strict HTTPS origins, manifest parsing and installer validation";
        {
            Manager network; UBApplicationController controller(&network);
            controller.checkUpdate(); controller.checkUpdate(); finish(controller);
            require(network.requests.size() == 1 && controller.manifestsReceived == 1, "website success/duplicate-click failed");
            qInfo() << "PASS website-only update check (no GitHub request), repeated-click guard";
        }
        {
            Manager network; network.answer = [](const QUrl &url) { Answer a; a.error = url.host() == "xiwang.cheyuze.top"; return a; };
            UBApplicationController controller(&network); controller.checkUpdate(); finish(controller);
            require(controller.manifestsReceived == 1 && network.requests.size() == 3, "offline website fallback failed");
            qInfo() << "PASS website failure retries then GitHub fallback";
        }
        for (int mode = 0; mode < 3; ++mode) {
            Manager network; network.answer = [mode](const QUrl &url) {
                Answer a;
                if (url.host() == "xiwang.cheyuze.top") {
                    if (mode == 0) a.body = "<html>version url login</html>";
                    if (mode == 1) a.redirect = QUrl("https://evil.test/update.json");
                    if (mode == 2) a.body = QByteArray(300 * 1024, 'x');
                }
                return a;
            };
            UBApplicationController controller(&network); controller.checkUpdate(); finish(controller);
            require(controller.manifestsReceived == 1, "invalid/redirect/oversized fallback failed");
            for (auto url : network.requests) require(url.host() != "evil.test", "followed untrusted redirect");
        }
        qInfo() << "PASS login HTML, malicious redirects and oversized bodies fall back safely";
        {
            Manager network; network.answer = [](const QUrl &url) { Answer a; if (url.host() == "xiwang.cheyuze.top") a.redirect = url; return a; };
            UBApplicationController controller(&network); controller.checkUpdate(); finish(controller);
            require(controller.manifestsReceived == 1 && network.requests.size() == 7, "redirect loop is not bounded");
            qInfo() << "PASS redirect-loop limit";
        }
        {
            const int before = metadataErrors;
            Manager network; network.answer = [](const QUrl &) { Answer a; a.error = true; return a; };
            UBApplicationController controller(&network); controller.checkUpdate(); finish(controller);
            require(metadataErrors == before + 1 && controller.manifestsReceived == 0, "all-sources failure not surfaced");
            network.answer = [](const QUrl &) { return Answer{}; }; controller.checkUpdate(); finish(controller);
            require(controller.manifestsReceived == 1, "cannot retry after failure");
            qInfo() << "PASS all-sources failure and manual retry recovery";
        }
        qInfo() << "ALL UPDATE SOURCE TESTS PASSED";
    } catch (const std::exception &error) { qCritical() << "FAIL" << error.what(); return 1; }
    return 0;
}
