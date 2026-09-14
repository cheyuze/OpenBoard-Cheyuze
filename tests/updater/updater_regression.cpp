#include "updater_harness.h"
#include <functional>
#include <iostream>
#include <stdexcept>

QString updateTestDownloadDirectory;
int installerLaunches = 0;
static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static bool waitFor(const std::function<bool()> &done, int timeout = 10000)
{
    QElapsedTimer clock; clock.start();
    while (!done() && clock.elapsed() < timeout)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return done();
}
static void waitMs(int ms) { QElapsedTimer t; t.start(); waitFor([&] { return t.elapsed() >= ms; }, ms + 100); }
static void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path); require(file.open(QIODevice::WriteOnly), "open test fixture");
    require(file.write(data) == data.size(), "write test fixture");
}
static QByteArray readFile(const QString &path) { QFile file(path); require(file.open(QIODevice::ReadOnly), "read test file"); return file.readAll(); }

class Server : public QTcpServer
{
public:
    QByteArray payload = QByteArray(2 * 1024 * 1024 + 17, 'x');
    QHash<QString, int> requests;
    explicit Server(QObject *parent = nullptr) : QTcpServer(parent)
    {
        for (int i = 0; i < payload.size(); ++i) payload[i] = char(i % 251);
        require(listen(QHostAddress::LocalHost, 0), "listen on loopback");
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections())
            {
                auto *socket = nextPendingConnection();
                auto header = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, header] {
                    *header += socket->readAll();
                    if (!header->contains("\r\n\r\n")) return;
                    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    const QString path = QString::fromLatin1(header->split(' ').value(1));
                    ++requests[path];
                    const auto match = QRegularExpression("Range: bytes=(\\d+)-", QRegularExpression::CaseInsensitiveOption)
                            .match(QString::fromLatin1(*header));
                    const qint64 offset = match.hasMatch() ? match.captured(1).toLongLong() : 0;
                    if (path == "/error") { socket->write("HTTP/1.1 503 Unavailable\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n"); socket->write(payload); socket->disconnectFromHost(); return; }
                    if (path == "/416") { socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
                    QByteArray body = path == "/bad" ? QByteArray(payload.size(), 'z') : payload;
                    QByteArray response = "HTTP/1.1 200 OK\r\n";
                    if (offset && path != "/ignore")
                    {
                        body = body.mid(offset);
                        response = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + QByteArray::number(offset) + "-"
                                + QByteArray::number(payload.size() - 1) + "/" + QByteArray::number(payload.size()) + "\r\n";
                    }
                    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
                    socket->write(response);
                    if (path == "/slow") { socket->write(body.left(4096)); return; }
                    socket->write(body); socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url(const QString &path) const { return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path)); }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    app.setApplicationVersion("1.8.5-test");
    QTemporaryDir directory; require(directory.isValid(), "isolated test directory");
    updateTestDownloadDirectory = directory.path();
    int completedPrompts = 0, warningPrompts = 0;
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, [&] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
            {
                if (box->windowTitle() == "Download complete") { ++completedPrompts; box->done(QMessageBox::No); }
                else { ++warningPrompts; box->reject(); }
            }
    });
    dialogs.start(5);
    Server server;
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(server.payload, QCryptographicHash::Sha256).toHex());
    auto target = [&](const QString &version) { return directory.filePath("OpenBoard-cheyuze-" + version + "-x64.exe"); };
    TestWindow window;
    UBApplicationController controller(&window);
    auto done = [&] { return !controller.mUpdateDownloadSession; };
    auto cancel = [&] {
        require(bool(controller.mUpdateDownloadSession), "cancel has active session");
        QMetaObject::invokeMethod(controller.mUpdateDownloadSession->progress, "canceled", Qt::DirectConnection);
    };
    try
    {
        controller.downloadUpdateInstaller({server.url("/ok")}, "normal", hash);
        require(waitFor(done), "normal transfer completed");
        require(readFile(target("normal")) == server.payload, "normal transfer bytes");
        require(!QFile::exists(target("normal") + ".part"), "successful transfer removes part");
        require(completedPrompts == 1 && installerLaunches == 0, "declined install never launches");
        std::cout << "PASS normal completion, SHA and no installation\n";

        const int requestCount = server.requests["/ok"];
        controller.downloadUpdateInstaller({server.url("/ok")}, "normal", hash);
        require(waitFor(done), "cached transfer completed");
        require(server.requests["/ok"] == requestCount && completedPrompts == 2, "cached installer reused without network");
        std::cout << "PASS cached installer verification\n";

        writeFile(target("resume") + ".part", server.payload.left(100003));
        controller.downloadUpdateInstaller({server.url("/ok")}, "resume", hash);
        require(waitFor(done) && readFile(target("resume")) == server.payload, "range resume bytes");
        std::cout << "PASS range resume\n";

        writeFile(target("fallback") + ".part", server.payload.left(100003));
        controller.downloadUpdateInstaller({server.url("/ignore"), server.url("/ok")}, "fallback", hash);
        require(waitFor(done) && readFile(target("fallback")) == server.payload, "range source switch preserves part");
        std::cout << "PASS unsupported range switches source\n";

        writeFile(target("fullpart") + ".part", server.payload);
        controller.downloadUpdateInstaller({server.url("/416")}, "fullpart", hash);
        require(waitFor(done) && readFile(target("fullpart")) == server.payload, "416 completed part verified");
        std::cout << "PASS completed partial file (416)\n";

        const QByteArray oldInstaller("old installer must survive");
        writeFile(target("badhash"), oldInstaller);
        controller.downloadUpdateInstaller({server.url("/bad")}, "badhash", hash);
        require(waitFor(done) && readFile(target("badhash")) == oldInstaller, "invalid hash preserves old installer");
        require(!QFile::exists(target("badhash") + ".part"), "invalid partial removed");
        std::cout << "PASS hash mismatch preserves existing installer\n";

        controller.downloadUpdateInstaller({server.url("/slow")}, "duplicate", hash);
        const auto originalSession = controller.mUpdateDownloadSession;
        controller.downloadUpdateInstaller({server.url("/slow")}, "duplicate", hash);
        require(controller.mUpdateDownloadSession == originalSession, "second click reuses one session");
        require(waitFor([&] { return server.requests["/slow"] == 1; }), "slow request started");
        waitMs(50); cancel(); require(waitFor(done), "active request canceled");
        require(server.requests["/slow"] == 1 && !QFile::exists(target("duplicate")), "single writer and no final file on cancel");
        std::cout << "PASS duplicate click and active cancellation\n";

        writeFile(target("retrycancel") + ".part", server.payload.left(100003));
        controller.downloadUpdateInstaller({server.url("/error")}, "retrycancel", hash);
        require(waitFor([&] { return server.requests["/error"] == 1 && controller.mUpdateDownloadSession && !controller.mUpdateDownloadSession->reply && !controller.mUpdateDownloadSession->finishing; }), "retry waiting state");
        require(readFile(target("retrycancel") + ".part") == server.payload.left(100003), "HTTP error body not written to part");
        cancel(); require(waitFor(done), "cancel during retry wait"); waitMs(1450);
        require(server.requests["/error"] == 1, "cancel prevents delayed retry");
        std::cout << "PASS retry cancellation and error-body isolation\n";

        auto *temporaryController = new UBApplicationController(&window);
        temporaryController->downloadUpdateInstaller({server.url("/error")}, "destroyretry", hash);
        require(waitFor([&] { return server.requests["/error"] == 2 && temporaryController->mUpdateDownloadSession
                    && !temporaryController->mUpdateDownloadSession->reply && !temporaryController->mUpdateDownloadSession->finishing; }), "owner destroy in retry wait");
        QPointer<QProgressDialog> retiringProgress = temporaryController->mUpdateDownloadSession->progress;
        delete temporaryController;
        require(!retiringProgress, "owner teardown destroys retry progress"); waitMs(1450);
        require(server.requests["/error"] == 2, "owner teardown cancels retry callback");
        temporaryController = new UBApplicationController(&window);
        temporaryController->downloadUpdateInstaller({server.url("/slow")}, "destroyactive", hash);
        require(waitFor([&] { return server.requests["/slow"] == 2; }), "owner destroy with active reply");
        retiringProgress = temporaryController->mUpdateDownloadSession->progress;
        QPointer<QNetworkReply> retiringReply = temporaryController->mUpdateDownloadSession->reply;
        delete temporaryController;
        require(!retiringProgress && !retiringReply, "owner teardown destroys request and speed timer owner");
        std::cout << "PASS controller destruction during retry wait and active transfer\n";

        std::atomic_bool canceled{true};
        writeFile(directory.filePath("copy.part"), server.payload);
        writeFile(directory.filePath("copy.exe"), oldInstaller);
        using namespace UBUpdateDownloadSupport;
        require(verifyAndPromote(directory.filePath("copy.part"), directory.filePath("copy.exe"), hash, canceled) == Result::Canceled, "worker cancellation");
        require(readFile(directory.filePath("copy.exe")) == oldInstaller && QFile::exists(directory.filePath("copy.part")), "cancellation preserves both files");
        canceled = false;
        require(verifyFile(directory.filePath("does-not-exist.part"), hash, canceled) == Result::IoError, "read errors are not hash mismatches");
        require(promoteDownloadedFile(directory.filePath("copy.part"), directory.filePath("missing/copy.exe"), canceled) == Result::IoError, "copy I/O failure");
        require(QFile::exists(directory.filePath("copy.part")), "copy failure keeps source");
        require(verifyAndPromote(directory.filePath("copy.part"), directory.filePath("copy.exe"), hash, canceled) == Result::Complete, "copy fits 1 MiB process stack");
        require(readFile(directory.filePath("copy.exe")) == server.payload, "atomic copy bytes");
        std::cout << "PASS worker cancellation, I/O failure, atomic replacement and 1 MiB stack\n";

        QThreadPool::globalInstance()->waitForDone();
        std::cout << "All updater regressions passed. Only loopback traffic and temporary files were used.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (controller.mUpdateDownloadSession) { controller.mUpdateDownloadSession->canceled = true; if (controller.mUpdateDownloadSession->reply) controller.mUpdateDownloadSession->reply->abort(); }
        QThreadPool::globalInstance()->waitForDone();
        return 1;
    }
}
