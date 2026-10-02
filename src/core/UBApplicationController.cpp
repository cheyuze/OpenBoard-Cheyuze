/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */



#include "UBApplicationController.h"
#include "UBUpdateDownloadSupport.h"
#include "UBUpdateSources.h"
#include <QFutureWatcher>
#include <QtConcurrent>

#include "frameworks/UBPlatformUtils.h"
#include "frameworks/UBVersion.h"

#include "core/UBApplication.h"
#include "core/UBPersistenceManager.h"
#include "core/UBSettings.h"
#include "core/UBSetting.h"
#include "core/UBDocumentManager.h"
#include "core/UBDisplayManager.h"


#include "board/UBBoardView.h"
#include "board/UBBoardController.h"
#include "board/UBBoardPaletteManager.h"
#include "board/UBDrawingController.h"


#include "document/UBDocumentProxy.h"
#include "document/UBDocumentController.h"

#include "domain/UBGraphicsWidgetItem.h"

#include "desktop/UBDesktopPalette.h"
#include "desktop/UBDesktopAnnotationController.h"

#include "web/UBWebController.h"

#include "gui/UBScreenMirror.h"
#include "gui/UBMainWindow.h"
#include "gui/UBStartupHintsPalette.h"

#include "domain/UBGraphicsPixmapItem.h"

#include "podcast/UBPodcastController.h"

#include "network/UBNetworkAccessManager.h"

#include "ui_mainWindow.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif


#ifdef Q_OS_MAC
#include <Carbon/Carbon.h>
#endif

#include "core/memcheck.h"

namespace
{
    const QString kGitHubProjectUrl = QStringLiteral(
            "https://github.com/cheyuze/OpenBoard-Cheyuze");

    QString updateMessageHtml(const QString &text)
    {
        QString escapedText = text.toHtmlEscaped();
        escapedText.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
        return QStringLiteral(
                "<div>%1</div>"
                "<div style=\"margin-top: 12px;\">"
                "<a href=\"https://xiwang.cheyuze.top/openboard/\">网站下载</a> · "
                "<a href=\"%2\">GitHub 项目页面</a> · "
                "<a href=\"https://pan.baidu.com/s/1bM8S8NFruXXRo96MWpLOwA?pwd=8y86\">百度网盘（8y86）</a>"
                "</div>")
                .arg(escapedText, kGitHubProjectUrl);
    }

    void enableUpdateMessageLinks(QMessageBox &messageBox)
    {
        messageBox.setTextFormat(Qt::RichText);
        messageBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        const QList<QLabel *> labels = messageBox.findChildren<QLabel *>();
        for (QLabel *label : labels)
        {
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(true);
        }
    }

    void showUpdateInformation(QWidget *parent, const QString &title,
                               const QString &text)
    {
        QMessageBox messageBox(QMessageBox::Information, title,
                               updateMessageHtml(text), QMessageBox::Ok,
                               parent);
        enableUpdateMessageLinks(messageBox);
        messageBox.exec();
    }

    struct UBUpdateDownloadMonitor
    {
        qint64 resumeOffset = 0;
        qint64 lastOverallReceived = 0;
        qint64 overallReceived = 0;
        qint64 overallTotal = -1;
        int stalledIntervals = 0;
        bool switchSourceAbort = false;
        bool rangeUnsupported = false;
        bool writeFailed = false;
        bool headersChecked = false;
    };

    bool launchInstaller(QWidget *parent, const QString &path)
    {
#ifdef Q_OS_WIN
        const QString nativePath = QDir::toNativeSeparators(path);
        SHELLEXECUTEINFOW executeInfo = {};
        executeInfo.cbSize = sizeof(executeInfo);
        executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        executeInfo.hwnd = parent ? reinterpret_cast<HWND>(parent->winId()) : nullptr;
        executeInfo.lpVerb = L"runas";
        executeInfo.lpFile = reinterpret_cast<LPCWSTR>(nativePath.utf16());
        executeInfo.nShow = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&executeInfo))
            return false;

        if (executeInfo.hProcess)
            CloseHandle(executeInfo.hProcess);
        return true;
#else
        return QProcess::startDetached(path, QStringList());
#endif
    }
}

UBApplicationController::UBApplicationController(UBBoardView *pControlView,
                                                 UBBoardView *pDisplayView,
                                                 UBMainWindow* pMainWindow,
                                                 QObject* parent,
                                                 UBRightPalette* rightPalette)
    : QObject(parent)
    , mMainWindow(pMainWindow)
    , mControlView(pControlView)
    , mDisplayView(pDisplayView)
    , mMirror(0)
    , mMainMode(Board)
    , mAutomaticCheckForUpdates(false)
    , mCheckingForUpdates(false)
    , mIsShowingDesktop(false)
{
    mUninoteController = new UBDesktopAnnotationController(this, rightPalette);

    UBDisplayManager* displayManager = UBApplication::displayManager;

    connect(displayManager, SIGNAL(screenLayoutChanged()), this, SLOT(screenLayoutChanged()));
    connect(displayManager, SIGNAL(screenLayoutChanged()), mUninoteController, SLOT(screenLayoutChanged()));
    connect(displayManager, SIGNAL(screenLayoutChanged()), UBApplication::webController, SLOT(screenLayoutChanged()));
    connect(displayManager, &UBDisplayManager::availableScreenCountChanged,this,  [this](){
        initPreviousViews();
        UBApplication::displayManager->setPreviousDisplaysWidgets(mPreviousViews);
    });
    connect(mUninoteController, SIGNAL(imageCaptured(const QPixmap &, bool)), this, SLOT(addCapturedPixmap(const QPixmap &, bool)));
    connect(mUninoteController, SIGNAL(restoreUniboard()), this, SLOT(hideDesktop()));

    mBlackScene = std::make_shared<UBGraphicsScene>(nullptr);
    mBlackScene->setBackground(true, UBPageBackground::plain);

    if (displayManager->numScreens() >= 2 && displayManager->useMultiScreen())
    {
        mMirror = new UBScreenMirror();
    }

    connect(UBApplication::webController, SIGNAL(imageCaptured(const QPixmap &, bool, const QUrl&))
            , this, SLOT(addCapturedPixmap(const QPixmap &, bool, const QUrl&)));

    mNetworkAccessManager = new QNetworkAccessManager (this);
    connect(mNetworkAccessManager, &QNetworkAccessManager::finished,
            this, &UBApplicationController::updateRequestFinished);
    QTimer::singleShot (1000, this, SLOT (checkAtLaunch()));
}


UBApplicationController::~UBApplicationController()
{
    stopUpdateDownload();
    foreach(UBBoardView* view, mPreviousViews)
    {
        delete view;
    }
    delete mMirror;
    delete mUninoteController;
}


void UBApplicationController::initViewState(int horizontalPosition, int verticalPostition)
{
    mInitialHScroll = horizontalPosition;
    mInitialVScroll = verticalPostition;
}


void UBApplicationController::initScreenLayout(bool useMultiscreen)
{
    UBDisplayManager* displayManager = UBApplication::displayManager;
    UBBoardController* boardController = UBApplication::boardController;

    displayManager->initScreensByRole();
    initPreviousViews();
    displayManager->assignRoles();
    displayManager->setControlWidget(mMainWindow);
    displayManager->setDisplayWidget(mDisplayView);

    displayManager->setPreviousDisplaysWidgets(mPreviousViews);
    displayManager->setDesktopWidget(mUninoteController->drawingView());

    displayManager->setUseMultiScreen(useMultiscreen);

    adjustPreviousViews(boardController->activeSceneIndex(), boardController->selectedDocument());
    displayManager->positionScreens();
}


void UBApplicationController::screenLayoutChanged()
{
    initViewState(mControlView->horizontalScrollBar()->value(),
            mControlView->verticalScrollBar()->value());

    adaptToolBar();

    UBApplication::boardController->adjustDisplayViews();

    if (UBApplication::displayManager->hasDisplay())
    {
        UBApplication::boardController->setBoxing(mDisplayView->geometry());
    }
    else
    {
       UBApplication::boardController->setBoxing(QRect());
    }

    // update mirror if necessary
    UBDisplayManager* displayManager = UBApplication::displayManager;

    if (displayManager->numScreens() > 1 && displayManager->useMultiScreen() && !mMirror)
    {
        mMirror = new UBScreenMirror();
    }
    else if ((displayManager->numScreens() == 1 || !displayManager->useMultiScreen()) && mMirror)
    {
        delete mMirror;
        mMirror = nullptr;
    }
}


void UBApplicationController::adaptToolBar()
{
    bool highResolution = mMainWindow->width() > 1024;

    mMainWindow->actionClearPage->setVisible(Board == mMainMode && highResolution);
    mMainWindow->actionBoard->setVisible(Board != mMainMode || highResolution);
    mMainWindow->actionDocument->setVisible(Document != mMainMode || highResolution);
    mMainWindow->actionWeb->setVisible(false);
    mMainWindow->boardToolBar->setIconSize(QSize(highResolution ? 32 : 28,
            highResolution ? 26 : 24));

    mMainWindow->actionBoard->setEnabled(mMainMode != Board);
    mMainWindow->actionWeb->setEnabled(false);
    mMainWindow->actionDocument->setEnabled(mMainMode != Document);
    mMainWindow->webToolBar->hide();

    if (Document == mMainMode)
    {
        connect(UBApplication::instance(), SIGNAL(focusChanged(QWidget *, QWidget *)), UBApplication::documentController, SLOT(focusChanged(QWidget *, QWidget *)));
    }
    else
    {
        disconnect(UBApplication::instance(), SIGNAL(focusChanged(QWidget *, QWidget *)), UBApplication::documentController, SLOT(focusChanged(QWidget *, QWidget *)));
        if (Board == mMainMode)
            mMainWindow->actionDuplicate->setEnabled(true);
    }

    UBApplication::boardController->setToolbarTexts();

    UBApplication::webController->adaptToolBar();

}


void UBApplicationController::adjustDisplayView()
{
    if (mDisplayView)
    {
        qreal systemDisplayViewScaleFactor = 1.0;

        QSize pageSize = mControlView->size();
        QSize displaySize = mDisplayView->size();

        qreal hFactor = ((qreal)displaySize.width()) / ((qreal)pageSize.width());
        qreal vFactor = ((qreal)displaySize.height()) / ((qreal)pageSize.height());

        systemDisplayViewScaleFactor = qMax(hFactor, vFactor);

        QTransform tr;
        qreal scaleFactor = systemDisplayViewScaleFactor
                * UBApplication::boardController->currentZoom()
                * UBApplication::boardController->systemScaleFactor();

        tr.scale(scaleFactor, scaleFactor);

        QTransform recentTransform = mDisplayView->transform();

        if (recentTransform != tr)
            mDisplayView->setTransform(tr);

        QRect rect = mControlView->rect();
        mDisplayView->centerOn(mControlView->mapToScene(rect.center()));
    }
}


void UBApplicationController::adjustPreviousViews(int pActiveSceneIndex, std::shared_ptr<UBDocumentProxy> pActiveDocument)
{
    int viewIndex = pActiveSceneIndex;

    foreach(UBBoardView* previousView, mPreviousViews)
    {
        if (viewIndex > 0)
        {
            viewIndex--;

            std::shared_ptr<UBGraphicsScene> scene = UBPersistenceManager::persistenceManager()->loadDocumentScene(pActiveDocument, viewIndex);

            if (scene)
            {
                previousView->setScene(scene.get());

                qreal ratio = ((qreal)previousView->geometry().width()) / ((qreal)previousView->geometry().height());
                QRectF sceneRect = scene->normalizedSceneRect(ratio);
                qreal scaleRatio = previousView->geometry().width() / sceneRect.width();

                previousView->resetTransform();

                previousView->scale(scaleRatio, scaleRatio);

                previousView->centerOn(sceneRect.center());
            }
        }
        else
        {
            previousView->setScene(mBlackScene.get());
        }
    }
}


void UBApplicationController::blackout()
{
    UBApplication::displayManager->blackout();
}


void UBApplicationController::addCapturedPixmap(const QPixmap &pPixmap, bool pageMode, const QUrl& sourceUrl)
{
    if (!pPixmap.isNull())
    {
        // make all scaling calculations floating point
        const auto sf = UBApplication::boardController->systemScaleFactor();
        const QSizeF pageNominalSize{UBApplication::boardController->activeScene()->nominalSize()};
        const QSizeF pixmapSize{pPixmap.size()};

        QSizeF scaledSize{pixmapSize / sf};
        QSizeF newSize{scaledSize.boundedTo(pageNominalSize)};

        if (pageMode)
        {
            newSize.setHeight(pixmapSize.height());
        }

        scaledSize.scale(newSize, Qt::KeepAspectRatio);

        qreal scaleFactor = qMin(scaledSize.width() / pixmapSize.width(), scaledSize.height() / pixmapSize.height());

        QPointF pos{};

        if (pageMode)
        {
            pos.setY(pageNominalSize.height() / -2  + scaledSize.height() / 2);
        }

        UBApplication::boardController->paletteManager()->addItem(pPixmap, pos, scaleFactor, sourceUrl);
    }
}


void UBApplicationController::addCapturedEmbedCode(const QString& embedCode)
{
    if (!embedCode.isEmpty())
    {
        showBoard();

        const QString userWidgetPath = UBSettings::settings()->userInteractiveDirectory() + "/" + tr("Web"); // TODO UB 4.x synch with w3cWidget
        QDir userWidgetDir(userWidgetPath);

        int width = 300;
        int height = 150;

        QString widgetPath = UBGraphicsW3CWidgetItem::createHtmlWrapperInDir(embedCode, userWidgetDir,
                QSize(width, height), UBStringUtils::toCanonicalUuid(QUuid::createUuid()));

        if (widgetPath.length() > 0)
            UBApplication::boardController->downloadURL(QUrl::fromLocalFile(widgetPath));
    }
}


void UBApplicationController::showBoard()
{
    mMainWindow->webToolBar->hide();
    mMainWindow->documentToolBar->hide();
    mMainWindow->boardToolBar->show();

    if (mMainMode == Document)
    {
//        int selectedSceneIndex = UBApplication::documentController->getSelectedItemIndex();
//        if (selectedSceneIndex != -1)
//        {
//            UBApplication::boardController->setActiveDocumentScene(UBApplication::documentController->selectedDocument(), selectedSceneIndex);
//        }
    }

    mMainMode = Board;

    adaptToolBar();

    mirroringEnabled(false);

    mMainWindow->switchToBoardWidget();

    if (UBApplication::boardController)
        UBApplication::boardController->show();

    mIsShowingDesktop = false;
    UBPlatformUtils::hideMenuBarAndDock();
    UBDrawingController::drawingController()->setInDesktopMode(false);

    mUninoteController->hideWindow();

#ifdef Q_OS_WIN
    // Team Edition: keep the teaching board in a regular, resizable window.
    // Desktop annotation and presentation displays still use their dedicated
    // full-screen windows.
    mMainWindow->showNormal();
#else
    UBPlatformUtils::showFullScreen(mMainWindow);
#endif

    emit mainModeChanged(Board);

    UBApplication::boardController->freezeW3CWidgets(false);
}


void UBApplicationController::showInternet()
{
    // Browser mode has been removed from this edition. This guard also blocks
    // legacy shortcuts or saved UI state from reopening it.
    showBoard();
}


void UBApplicationController::showDocument()
{
    mMainWindow->webToolBar->hide();
    mMainWindow->boardToolBar->hide();
    UBPlatformUtils::hideMenuBarAndDock();
    mMainWindow->documentToolBar->show();

    mMainMode = Document;

    adaptToolBar();

    mirroringEnabled(false);

    mMainWindow->switchToDocumentsWidget();

    if (UBApplication::boardController)
    {
        UBApplication::boardController->persistCurrentScene();
        UBPersistenceManager::persistenceManager()->persistDocumentMetadata(UBApplication::boardController->selectedDocument());

        UBApplication::boardController->hide();
    }

    if (UBApplication::documentController)
    {
        UBApplication::documentController->show();
    }

    mMainWindow->show();

    mUninoteController->hideWindow();

    emit mainModeChanged(Document);
}

void UBApplicationController::showDesktop(bool dontSwitchFrontProcess)
{
    if (UBApplication::boardController)
        UBApplication::boardController->hide();

    mMainWindow->hide();
    mUninoteController->showWindow();

    if (mMirror)
    {
        // grab from screen instead of widget
        mMirror->setSourceWidget(nullptr);
    }

    mIsShowingDesktop = true;

    // Ensure the recorder controller is listening before desktopMode(true)
    // is emitted.  It lazily creates an independent recording palette, so
    // desktop recording remains accessible even though mMainWindow is hidden.
    UBPodcastController::instance();
    emit desktopMode(true);

    if (!dontSwitchFrontProcess) {
        UBPlatformUtils::bringPreviousProcessToFront();
    }

    UBDrawingController::drawingController()->setInDesktopMode(true);
    UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
}


void UBApplicationController::checkUpdate(const QUrl& url,
                                          const QList<QUrl>& urls,
                                          int urlIndex,
                                          int retryAttempt,
                                          int redirectCount)
{
    QList<QUrl> manifestUrls = urls;
    QUrl jsonUrl = url;

    if (manifestUrls.isEmpty())
    {
        if (mCheckingForUpdates)
            return;
        mCheckingForUpdates = true;
        const QUrl configuredUrl = url.isEmpty()
                ? UBSettings::settings()->appSoftwareUpdateURL->get().toUrl()
                : url;
        const QString configuredUrlString = configuredUrl.toString();

        // Try the maintained website even on profiles that still store the
        // old GitHub URL. Proxies may carry payloads, never verification data.
        manifestUrls = UBUpdateSources::manifests(configuredUrl);
        if (!UBUpdateSources::trustedManifest(configuredUrl))
            qWarning() << "Ignoring untrusted update manifest URL:" << configuredUrlString;

        // The initial URL argument may have come from a locally modified
        // setting. Select the first validated entry from our trusted list.
        jsonUrl = QUrl();
        urlIndex = 0;
        retryAttempt = 0;
    }

    if (jsonUrl.isEmpty())
    {
        if (urlIndex < 0 || urlIndex >= manifestUrls.size())
            return;
        jsonUrl = manifestUrls.at(urlIndex);
    }

    if (!UBUpdateSources::trustedManifest(jsonUrl) || redirectCount > 5)
    {
        qWarning() << "Blocked untrusted update manifest URL:" << jsonUrl;
        if (urlIndex + 1 < manifestUrls.size())
        {
            QTimer::singleShot(0, this, [this, manifestUrls, urlIndex]() {
                checkUpdate(QUrl(), manifestUrls, urlIndex + 1, 0);
            });
        }
        else
        {
            mCheckingForUpdates = false;
            if (isNoUpdateDisplayed)
                showUpdateInformation(mMainWindow, tr("Check for updates"),
                                  tr("Unable to check for updates securely."));
        }
        return;
    }

    qDebug() << "Checking for update at url: " << jsonUrl.toString();

    QNetworkRequest request(jsonUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("OpenBoard-cheyuze/%1").arg(qApp->applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Cache-Control", "no-cache");
    // Validate every redirect before making the next request.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    // Some networks used by our teachers reset GitHub's HTTP/2 streams while
    // Qt is downloading.  HTTP/1.1 is slower to negotiate but considerably
    // more reliable through those proxies and gateways.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    request.setTransferTimeout(10000);
#endif
    QNetworkReply *reply = mNetworkAccessManager->get(request);
    QStringList manifestUrlStrings;
    for (const QUrl &manifestUrl : manifestUrls)
        manifestUrlStrings.append(manifestUrl.toString());
    reply->setProperty("updateManifestUrls", manifestUrlStrings);
    reply->setProperty("updateManifestUrlIndex", urlIndex);
    reply->setProperty("updateManifestRetryAttempt", retryAttempt);
    reply->setProperty("updateManifestRedirectCount", redirectCount);
    connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > 256 * 1024)
            reply->abort();
    });

}



void UBApplicationController::updateRequestFinished(QNetworkReply * reply)
{
    QList<QUrl> manifestUrls;
    const QStringList manifestUrlStrings = reply->property("updateManifestUrls").toStringList();
    for (const QString &manifestUrlString : manifestUrlStrings)
        manifestUrls.append(QUrl(manifestUrlString));
    const int urlIndex = reply->property("updateManifestUrlIndex").toInt();
    const int retryAttempt = reply->property("updateManifestRetryAttempt").toInt();

    if (reply->error()) {
        qWarning() << "Error downloading update file: " << reply->errorString();

        if (retryAttempt < 1) {
            reply->deleteLater();
            QTimer::singleShot(500, this, [this, manifestUrls, urlIndex, retryAttempt]() {
                checkUpdate(QUrl(), manifestUrls, urlIndex, retryAttempt + 1);
            });
            return;
        }

        if (urlIndex + 1 < manifestUrls.size()) {
            reply->deleteLater();
            QTimer::singleShot(250, this, [this, manifestUrls, urlIndex]() {
                checkUpdate(QUrl(), manifestUrls, urlIndex + 1, 0);
            });
            return;
        }

        mCheckingForUpdates = false;
        if (isNoUpdateDisplayed)
            showUpdateInformation(mMainWindow, tr("Check for updates"),
                                  tr("Unable to check for updates. Please check your network connection and try again."));
        reply->deleteLater();
        return;
    }

    // Check if we are being redirected. If so, call checkUpdate again

    QVariant redirect_target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (!redirect_target.isNull()) {
        // The returned URL might be relative. resolved() creates an absolute url from it
        QUrl redirect_url(reply->url().resolved(redirect_target.toUrl()));

        checkUpdate(redirect_url, manifestUrls, urlIndex, retryAttempt,
                    reply->property("updateManifestRedirectCount").toInt() + 1);
        reply->deleteLater();
        return;
    }

    // No error and no redirect => we read the whole response

    QString responseString = QString::fromUtf8(reply->readAll());

    if (UBUpdateSources::validManifest(responseString.toUtf8())) {

        mCheckingForUpdates = false;
        downloadJsonFinished(responseString);
    }
    else {
        qWarning() << "Invalid update manifest returned by: " << reply->url();
        if (urlIndex + 1 < manifestUrls.size()) {
            reply->deleteLater();
            QTimer::singleShot(250, this, [this, manifestUrls, urlIndex]() {
                checkUpdate(QUrl(), manifestUrls, urlIndex + 1, 0);
            });
            return;
        }

        mCheckingForUpdates = false;
        if (isNoUpdateDisplayed)
            showUpdateInformation(mMainWindow, tr("Check for updates"),
                                  tr("The update information returned by the server is invalid."));
    }

    reply->deleteLater();
}

void UBApplicationController::initPreviousViews()
{
    int numPreviousViews = UBApplication::displayManager->numPreviousViews();

    // create the missing views
    for (int i = mPreviousViews.count(); i < numPreviousViews; i++)
    {
        UBBoardView *previousView = new UBBoardView(UBApplication::boardController, UBItemLayerType::FixedBackground, UBItemLayerType::Tool, 0);
        previousView->setInteractive(false);
        mPreviousViews.append(previousView);
    }

    // delete the superfluous views
    while (mPreviousViews.count() > numPreviousViews)
    {
        UBBoardView* view = mPreviousViews.takeLast();
        delete view;
    }
}


void UBApplicationController::downloadJsonFinished(QString currentJson)
{
    /*
      The .json files simply specify the latest version number available, and
      the URL to send the user to, so they can download it.

      They look like:

          {
            "version": "1.3.5",
            "url": "http://openboard.ch"
          }
    */

    QJsonObject jsonObject = QJsonDocument::fromJson(currentJson.toUtf8()).object();

    UBVersion installedVersion (qApp->applicationVersion());
    UBVersion jsonVersion (jsonObject.value("version").toString());

    qDebug() << "json version: " << jsonVersion.toUInt();
    qDebug() << "installed version: " << installedVersion.toUInt();

    if (jsonVersion > installedVersion) {
        QString notes;
        const QJsonValue notesValue = jsonObject.value("notes");
        if (notesValue.isArray()) {
            const QJsonArray notesArray = notesValue.toArray();
            for (const QJsonValue &note : notesArray)
                notes += QString("\n• %1").arg(note.toString());
        }
        else {
            notes = notesValue.toString();
        }

        QMessageBox messageBox(mMainWindow);
        messageBox.setIcon(QMessageBox::Information);
        messageBox.setWindowTitle(tr("Update available"));
        messageBox.setText(updateMessageHtml(
                tr("A new version %1 is available.").arg(jsonObject.value("version").toString())
                + "\n" + tr("Current version: %1").arg(qApp->applicationVersion())));
        if (!notes.trimmed().isEmpty())
            messageBox.setInformativeText(tr("What's new:") + notes);
        enableUpdateMessageLinks(messageBox);
        const QString baiduUrlString = jsonObject.value("baiduUrl").toString().trimmed();
        const QString baiduPassword = jsonObject.value("baiduPassword").toString().trimmed();
        const QList<QUrl> websiteUrls = UBUpdateSources::installers(jsonObject);
        const QList<QUrl> githubUrls = UBUpdateSources::installers(jsonObject, false);
        QPushButton *websiteButton = nullptr;
        if (!websiteUrls.isEmpty() && UBUpdateSources::websiteInstaller(websiteUrls.first()))
            websiteButton = messageBox.addButton(tr("Download from website (recommended)"), QMessageBox::AcceptRole);
        QPushButton *githubButton = messageBox.addButton(
                tr("Download from GitHub"), QMessageBox::ActionRole);
        githubButton->setEnabled(!githubUrls.isEmpty()
                                 && UBUpdateSources::githubInstaller(githubUrls.first()));
        QPushButton *baiduButton = nullptr;
        if (!baiduUrlString.isEmpty())
            baiduButton = messageBox.addButton(tr("Download from Baidu Netdisk"),
                                               QMessageBox::ActionRole);
        messageBox.addButton(tr("Remind me later"), QMessageBox::RejectRole);
        if (websiteButton)
            messageBox.setDefaultButton(websiteButton);
        messageBox.exec();

        if ((websiteButton && messageBox.clickedButton() == websiteButton)
                || messageBox.clickedButton() == githubButton) {
            const QList<QUrl> orderedUrls = messageBox.clickedButton() == websiteButton
                    ? websiteUrls : githubUrls;
            if (!orderedUrls.isEmpty())
                downloadUpdateInstaller(orderedUrls,
                                        jsonObject.value("version").toString(),
                                        jsonObject.value("sha256").toString(),
                                        QUrl(baiduUrlString), baiduPassword);
        }
        else if (baiduButton && messageBox.clickedButton() == baiduButton) {
            const QUrl baiduUrl(baiduUrlString);
            if (baiduUrl.isValid() && QDesktopServices::openUrl(baiduUrl)) {
                const QString passwordText = baiduPassword.isEmpty()
                        ? tr("No extraction code was provided.")
                        : tr("Baidu Netdisk extraction code: %1").arg(baiduPassword);
                QMessageBox::information(mMainWindow, tr("Baidu Netdisk download"),
                                          tr("The Baidu Netdisk share page has been opened in your browser.\n\n%1")
                                          .arg(passwordText));
            }
            else {
                QMessageBox::warning(mMainWindow, tr("Baidu Netdisk download"),
                                     tr("Unable to open the Baidu Netdisk share page."));
            }
        }
    }
    else if (isNoUpdateDisplayed) {
        showUpdateInformation(mMainWindow, tr("Check for updates"),
                              tr("You are using the latest version (%1).").arg(qApp->applicationVersion()));
    }
}

void UBApplicationController::closeUpdateDownload(
        const std::shared_ptr<UBUpdateDownloadSession> &session)
{
    if (session->progress)
    {
        // close() may itself emit canceled. Teardown is not user cancellation.
        disconnect(session->progress, nullptr, this, nullptr);
        session->progress->hide();
        session->progress->deleteLater();
    }
    if (mUpdateDownloadSession == session)
        mUpdateDownloadSession.reset();
}

void UBApplicationController::stopUpdateDownload()
{
    const auto session = mUpdateDownloadSession;
    if (!session)
        return;
    session->canceled = true;
    if (session->reply)
        disconnect(session->reply, nullptr, this, nullptr);
    if (session->progress)
    {
        disconnect(session->progress, nullptr, this, nullptr);
        // The dialog owns network managers and speed timers. Destroy them now,
        // not after the controller's callbacks and members have gone away.
        delete session->progress.data();
    }
    mUpdateDownloadSession.reset();
}

void UBApplicationController::downloadUpdateInstaller(const QList<QUrl> &urls,
                                                       const QString &version,
                                                       const QString &expectedSha256,
                                                       const QUrl &baiduUrl,
                                                       const QString &baiduPassword,
                                                       int urlIndex,
                                                       int retryAttempt,
                                                       QProgressDialog *progress)
{
    if (urlIndex < 0 || urlIndex >= urls.size())
        return;

    if (!progress)
    {
        if (mUpdateDownloadSession)
        {
            if (mUpdateDownloadSession->progress && mUpdateDownloadSession->canContinue())
            {
                mUpdateDownloadSession->progress->show();
                mUpdateDownloadSession->progress->raise();
                mUpdateDownloadSession->progress->activateWindow();
            }
            return; // Never allow two writers for the same installer/.part file.
        }
        progress = new QProgressDialog(tr("Downloading update %1...").arg(version),
                                       tr("Cancel"), 0, 100, mMainWindow);
        progress->setWindowTitle(tr("Download update"));
        progress->setWindowModality(Qt::NonModal);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setMinimumDuration(0);
        progress->setAttribute(Qt::WA_DeleteOnClose, false);
        progress->setWindowFlag(Qt::WindowMinimizeButtonHint, true);
        mUpdateDownloadSession = std::make_shared<UBUpdateDownloadSession>();
        mUpdateDownloadSession->progress = progress;
        const auto session = mUpdateDownloadSession;
        connect(progress, &QProgressDialog::canceled, this, [this, session]() {
            session->canceled = true;
            if (session->reply && !session->reply->isFinished())
                session->reply->abort();
            // File workers own the session until they stop. Do not permit a new
            // request to overwrite their files in the meantime.
            else if (!session->finishing)
                closeUpdateDownload(session);
        });
        progress->show();
    }
    const auto session = mUpdateDownloadSession;
    if (!session || session->progress != progress || !session->canContinue())
        return;

    const QUrl url = urls.at(urlIndex);
    const QString downloadDirectory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString fileName = QString("OpenBoard-cheyuze-%1-x64.exe").arg(version);
    const QString destination = QDir(downloadDirectory).filePath(fileName);
    const QString partialPath = destination + QStringLiteral(".part");

    if (!session->cacheChecked)
    {
        session->cacheChecked = true;
        session->finishing = true;
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this,
                [this, watcher, session, urls, version, expectedSha256,
                 baiduUrl, baiduPassword, destination, urlIndex, retryAttempt]() {
            const bool cached = watcher->result();
            watcher->deleteLater();
            session->finishing = false;
            if (!session->canContinue() || !session->progress)
            {
                closeUpdateDownload(session);
                return;
            }
            if (!cached)
            {
                downloadUpdateInstaller(urls, version, expectedSha256, baiduUrl,
                                        baiduPassword, urlIndex, retryAttempt, session->progress);
                return;
            }
            closeUpdateDownload(session);
            const auto installNow = QMessageBox::question(mMainWindow, tr("Download complete"),
                    tr("The update has already been downloaded. Install it now?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (installNow == QMessageBox::Yes)
            {
                if (launchInstaller(mMainWindow, destination))
                    QTimer::singleShot(500, qApp, []() { qApp->quit(); });
                else
                    QMessageBox::warning(mMainWindow, tr("Install update"),
                            tr("Unable to start the update installer. OpenBoard will remain running."));
            }
        });
        watcher->setFuture(QtConcurrent::run([destination, expectedSha256, session]() {
            return UBUpdateDownloadSupport::fileMatchesSha256(destination, expectedSha256, session->canceled);
        }));
        return;
    }

    QFile *file = new QFile(partialPath, this);
    if (!file->open(QIODevice::ReadWrite)) {
        closeUpdateDownload(session);
        mMainWindow->information(tr("Download update"),
                                 tr("Unable to save the installer to: %1").arg(destination));
        file->deleteLater();
        return;
    }

    const qint64 resumeOffset = file->size();
    if (!file->seek(resumeOffset))
    {
        closeUpdateDownload(session);
        file->close();
        file->deleteLater();
        mMainWindow->information(tr("Download update"),
                                 tr("Unable to continue the previous download."));
        return;
    }

    progress->setLabelText(tr("Connecting to download source %1 of %2...")
                           .arg(urlIndex + 1).arg(urls.size()));
    if (resumeOffset > 0)
        progress->setLabelText(tr("Continuing update download... %1 MB downloaded")
                               .arg(resumeOffset / 1024.0 / 1024.0, 0, 'f', 1));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("OpenBoard-cheyuze/%1").arg(qApp->applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (resumeOffset > 0)
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(resumeOffset) + "-");
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    request.setTransferTimeout(120000);
#endif
    // Keep installer traffic separate from the update-manifest manager, whose
    // finished signal parses every reply as JSON.
    QNetworkAccessManager *downloadManager = new QNetworkAccessManager(progress);
    QNetworkReply *reply = downloadManager->get(request);
    reply->setReadBufferSize(1024 * 1024);
    session->reply = reply;
    connect(reply, &QNetworkReply::finished, downloadManager, &QObject::deleteLater);
    const std::shared_ptr<UBUpdateDownloadMonitor> monitor =
            std::make_shared<UBUpdateDownloadMonitor>();
    monitor->resumeOffset = resumeOffset;
    monitor->lastOverallReceived = resumeOffset;
    monitor->overallReceived = resumeOffset;

    // Preserve slow but progressing downloads. Only change source after a
    // sustained period with no new bytes; the .part file is kept and the next
    // source receives a Range request for the exact saved offset.
    QTimer *speedTimer = new QTimer(progress);
    speedTimer->setInterval(5000);
    speedTimer->start();

    connect(reply, &QNetworkReply::readyRead, this, [reply, file, monitor, session]() {
        if (!session->canContinue())
            return;
        if (!monitor->headersChecked)
        {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 0)
                return;
            // Error pages must not be appended to a resumable installer.
            if (status >= 300)
            {
                // Drain the bounded reply buffer, otherwise a large error page
                // can fill it and prevent the request from ever finishing.
                reply->readAll();
                return;
            }

            monitor->headersChecked = true;
            if (monitor->resumeOffset > 0)
            {
                const QString contentRange = QString::fromLatin1(reply->rawHeader("Content-Range"));
                const QRegularExpression expression(
                        QStringLiteral("^bytes\\s+(\\d+)-(\\d+)/(\\d+|\\*)$"),
                        QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch match = expression.match(contentRange.trimmed());
                const bool validRange = status == 206 && match.hasMatch()
                        && match.captured(1).toLongLong() == monitor->resumeOffset
                        && match.captured(2).toLongLong() >= monitor->resumeOffset
                        && (match.captured(3) == QStringLiteral("*")
                            || match.captured(3).toLongLong() > match.captured(2).toLongLong());

                if (!validRange)
                {
                    monitor->rangeUnsupported = true;
                    reply->abort();
                    return;
                }

                if (match.captured(3) != QStringLiteral("*"))
                    monitor->overallTotal = match.captured(3).toLongLong();
            }
        }

        while (reply->bytesAvailable() > 0)
        {
            const QByteArray data = reply->read(64 * 1024);
            if (data.isEmpty() || file->write(data) != data.size())
            {
                monitor->writeFailed = true;
                reply->abort();
                return;
            }
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, progress,
            [progress, monitor](qint64 received, qint64 total) {
        monitor->overallReceived = monitor->resumeOffset + received;
        if (monitor->overallTotal <= 0 && total > 0)
            monitor->overallTotal = monitor->resumeOffset + total;

        if (monitor->overallTotal > 0) {
            progress->setRange(0, 100);
            progress->setValue(static_cast<int>((monitor->overallReceived * 100)
                                                / monitor->overallTotal));
            progress->setLabelText(tr("Downloading update... %1 MB / %2 MB")
                                   .arg(monitor->overallReceived / 1024.0 / 1024.0, 0, 'f', 1)
                                   .arg(monitor->overallTotal / 1024.0 / 1024.0, 0, 'f', 1));
        }
        else {
            progress->setRange(0, 0);
        }
    });
    connect(speedTimer, &QTimer::timeout, progress,
            [progress, reply, monitor, speedTimer, urlIndex, urls]() {
        if (monitor->overallReceived <= monitor->lastOverallReceived)
            ++monitor->stalledIntervals;
        else
            monitor->stalledIntervals = 0;

        monitor->lastOverallReceived = monitor->overallReceived;

        // Nine empty windows are about 45 seconds. A merely slow connection is
        // allowed to continue; only a real stall moves to another source.
        if (monitor->stalledIntervals >= 9 && urlIndex + 1 < urls.size())
        {
            monitor->switchSourceAbort = true;
            speedTimer->stop();
            reply->abort();
            progress->setLabelText(
                    tr("The current download source is not responding. Switching to a backup source and continuing the download..."));
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, progress, destination, urls, version,
             partialPath, expectedSha256, baiduUrl, baiduPassword,
             urlIndex, retryAttempt, monitor, speedTimer, session]() {
        speedTimer->stop();
        speedTimer->deleteLater();

        const auto closeProgress = [this, session]() {
            closeUpdateDownload(session);
        };

        const auto offerBaiduDownload = [this, baiduUrl, baiduPassword](const QString &reason) {
            QMessageBox messageBox(mMainWindow);
            messageBox.setIcon(QMessageBox::Warning);
            messageBox.setWindowTitle(tr("Download update"));
            messageBox.setText(reason);

            QPushButton *baiduButton = nullptr;
            if (baiduUrl.isValid() && !baiduUrl.isEmpty())
            {
                messageBox.setInformativeText(
                        tr("Automatic update download failed. You can download the installer from Baidu Netdisk."));
                baiduButton = messageBox.addButton(tr("Download from Baidu Netdisk"),
                                                   QMessageBox::AcceptRole);
            }
            messageBox.addButton(tr("Close"), QMessageBox::RejectRole);
            messageBox.exec();

            if (baiduButton && messageBox.clickedButton() == baiduButton)
            {
                if (QDesktopServices::openUrl(baiduUrl))
                {
                    const QString passwordText = baiduPassword.isEmpty()
                            ? tr("No extraction code was provided.")
                            : tr("Baidu Netdisk extraction code: %1").arg(baiduPassword);
                    QMessageBox::information(
                            mMainWindow, tr("Baidu Netdisk download"),
                            tr("The Baidu Netdisk share page has been opened in your browser.\n\n%1")
                            .arg(passwordText));
                }
                else
                {
                    QMessageBox::warning(mMainWindow, tr("Baidu Netdisk download"),
                                         tr("Unable to open the Baidu Netdisk share page."));
                }
            }
        };

        const auto retry = [this, session, urls, version, expectedSha256,
                            baiduUrl, baiduPassword](int delay, int nextSource, int nextAttempt) {
            // Tie the callback to the controller lifetime, and independently
            // validate the session/dialog after a cancel or dialog teardown.
            QTimer::singleShot(delay, this,
                    [this, session, urls, version, expectedSha256, baiduUrl,
                     baiduPassword, nextSource, nextAttempt]() {
                if (mUpdateDownloadSession == session && session->canContinue() && session->progress)
                    downloadUpdateInstaller(urls, version, expectedSha256, baiduUrl,
                                            baiduPassword, nextSource, nextAttempt, session->progress);
            });
        };

        const bool flushed = file->flush();
        file->close();
        session->reply.clear();
        if (!session->canContinue())
        {
            closeProgress();
            reply->deleteLater();
            file->deleteLater();
            return;
        }
        if (!flushed || monitor->writeFailed)
        {
            closeProgress();
            reply->deleteLater();
            file->deleteLater();
            mMainWindow->information(tr("Download update"), tr("Unable to save the downloaded installer."));
            return;
        }

        // HTTP 416 may mean the .part file is already complete. It must pass
        // the same background verification as every normal completed response.
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && httpStatus != 416) {
            const QNetworkReply::NetworkError error = reply->error();
            const QString errorText = reply->errorString();
            reply->deleteLater();
            file->deleteLater();

            if (error == QNetworkReply::OperationCanceledError)
            {
                if ((monitor->switchSourceAbort || monitor->rangeUnsupported)
                        && urlIndex + 1 < urls.size())
                {
                    retry(250, urlIndex + 1, 0);
                    return;
                }

                closeProgress();
                if (monitor->rangeUnsupported)
                {
                    offerBaiduDownload(
                            tr("No download source accepted the resume request. The downloaded part has been kept; please try again later."));
                }
                return;
            }

            // Retry transient failures without discarding the .part file.
            if (retryAttempt < 2) {
                retry(1200, urlIndex, retryAttempt + 1);
                return;
            }

            if (urlIndex + 1 < urls.size()) {
                retry(500, urlIndex + 1, 0);
                return;
            }

            closeProgress();
            offerBaiduDownload(tr("Update download failed: %1").arg(errorText));
            return;
        }

        reply->deleteLater();
        file->deleteLater();
        session->finishing = true;
        progress->setRange(0, 0);
        progress->setLabelText(tr("Verifying and saving the update..."));
        using Result = UBUpdateDownloadSupport::Result;
        auto *watcher = new QFutureWatcher<Result>(this);
        connect(watcher, &QFutureWatcher<Result>::finished, this,
                [this, watcher, session, partialPath, destination, closeProgress, offerBaiduDownload]() {
            const Result result = watcher->result();
            watcher->deleteLater();
            session->finishing = false;
            closeProgress();
            if (!session->canContinue() || result == Result::Canceled)
                return;
            if (result == Result::InvalidHash)
            {
                QFile::remove(partialPath);
                offerBaiduDownload(tr("Installer verification failed. The damaged partial file was removed; please try again."));
                return;
            }
            if (result != Result::Complete)
            {
                mMainWindow->information(tr("Download update"), tr("Unable to save the downloaded installer."));
                return;
            }
            const auto installNow = QMessageBox::question(mMainWindow, tr("Download complete"),
                    tr("The update has been downloaded. Install it now?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (installNow == QMessageBox::Yes)
            {
                if (launchInstaller(mMainWindow, destination))
                    QTimer::singleShot(500, qApp, []() { qApp->quit(); });
                else
                    QMessageBox::warning(mMainWindow, tr("Install update"),
                            tr("Unable to start the update installer. OpenBoard will remain running."));
            }
        });
        watcher->setFuture(QtConcurrent::run([partialPath, destination, expectedSha256, session]() {
            return UBUpdateDownloadSupport::verifyAndPromote(partialPath, destination, expectedSha256, session->canceled);
        }));
    });
}

void UBApplicationController::checkAtLaunch()
{
    if(UBSettings::settings()->appEnableAutomaticSoftwareUpdates->get().toBool()){
        isNoUpdateDisplayed = false;
        checkUpdate();
    }
}

void UBApplicationController::checkUpdateRequest()
{
    isNoUpdateDisplayed = true;
    checkUpdate();
}

void UBApplicationController::hideDesktop()
{
    if (mMainMode == Board)
    {
        showBoard();
    }
    else if (mMainMode == Internet)
    {
        showInternet();
    }
    else if (mMainMode == Document)
    {
        showDocument();
    }

    mIsShowingDesktop = false;

    UBApplication::displayManager->adjustScreens();

    emit desktopMode(false);
}

void UBApplicationController::setMirrorSourceWidget(QWidget* pWidget)
{
    if (mMirror)
    {
        mMirror->setSourceWidget(pWidget);
    }
}


void UBApplicationController::mirroringEnabled(bool enabled)
{
    if (mMirror)
    {
        if (enabled)
        {
            mMirror->start();
            UBApplication::displayManager->setDisplayWidget(mMirror);

        }
        else
        {
            UBApplication::displayManager->setDisplayWidget(mDisplayView);
            mMirror->stop();
        }

        mMirror->setVisible(enabled && UBApplication::displayManager->numScreens() > 1);
        mUninoteController->updateShowHideState(enabled);
        UBApplication::mainWindow->actionWebShowHideOnDisplay->setChecked(enabled);
    }
    else
    {
        UBApplication::displayManager->setDisplayWidget(mDisplayView);
    }
}



void UBApplicationController::closing()
{
    stopUpdateDownload();
    if (mMirror)
        mMirror->stop();

    if (mUninoteController)
    {
        mUninoteController->hideWindow();
        mUninoteController->close();
    }

    /*

    if (UBApplication::documentController)
        UBApplication::documentController->closing();

    */

    UBPersistenceManager::persistenceManager()->closing(); // ALTI/AOU - 20140616 : to update the file "documents/folders.xml"
}


void UBApplicationController::showMessage(const QString& message, bool showSpinningWheel)
{
    if (!UBApplication::closingDown())
    {
        if (mMainMode == Document)
        {
            UBApplication::boardController->hideMessage();
            UBApplication::documentController->showMessage(message, showSpinningWheel);
        }
        else
        {
            UBApplication::documentController->hideMessage();
            UBApplication::boardController->showMessage(message, showSpinningWheel);
        }
    }
}


void UBApplicationController::importFile(const QString& pFilePath)
{
    const QFile fileToOpen(pFilePath);

    if (!fileToOpen.exists())
        return;

    bool success = false;

    std::shared_ptr<UBDocumentProxy> document = UBDocumentManager::documentManager()->importFile(fileToOpen, "");

    success = (document != 0);

    if (success && document)
    {
        if (UBApplication::boardController)
        {
            UBApplication::boardController->setActiveDocumentScene(document, 0, true, true);
        }

        if (UBApplication::documentController)
        {
            UBApplication::documentController->selectDocument(document, true, true);
        }

        // This import operation happens when double-clicking on a UBZ for example.
        // The document is added and set as current document, so the user probably wants to see it immediately.
        showBoard();
    }
}

void UBApplicationController::useMultiScreen(bool use)
{
    if (use && !mMirror)
        mMirror = new UBScreenMirror();
    if (!use && mMirror) {
        mirroringEnabled(false);
        delete mMirror;
        mMirror = NULL;
    }

    UBApplication::displayManager->setUseMultiScreen(use);
    UBApplication::displayManager->adjustScreens();
    UBSettings::settings()->appUseMultiscreen->set(use);

}


QStringList UBApplicationController::widgetInlineJavaScripts()
{
    QString scriptDirPath = UBPlatformUtils::applicationResourcesDirectory() + "/widget-inline-js";
    QDir scriptDir(scriptDirPath);

    QStringList scripts;

    if (scriptDir.exists())
    {
        QStringList files = scriptDir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

        foreach(QString file, files)
        {
            QFile scriptFile(scriptDirPath + "/" + file);
            if (file.endsWith(".js") && scriptFile.open(QIODevice::ReadOnly))
            {
                QString s = QString::fromUtf8(scriptFile.readAll());

                if (s.length() > 0)
                    scripts << s;

            }
        }
    }

    std::sort(scripts.begin(), scripts.end());

    return scripts;
}



void UBApplicationController::actionCut()
{
    if (!UBApplication::closingDown())
    {
        if (mMainMode == Board)
        {
            UBApplication::boardController->cut();
        }
        else if(mMainMode == Document)
        {
            UBApplication::documentController->cut();
        }
        else if(mMainMode == Internet)
        {
            UBApplication::webController->cut();
        }
    }
}


void UBApplicationController::actionCopy()
{
    if (!UBApplication::closingDown())
    {
        if (mMainMode == Board)
        {
            UBApplication::boardController->copy();
        }
        else if(mMainMode == Document)
        {
            UBApplication::documentController->copy();
        }
        else if(mMainMode == Internet)
        {
            UBApplication::webController->copy();
        }
    }
}


void UBApplicationController::actionPaste()
{
    if (!UBApplication::closingDown())
    {
        if (mMainMode == Board)
        {
            UBApplication::boardController->paste();
        }
        else if (mMainMode == Document)
        {
            UBApplication::documentController->paste();
        }
        else if(mMainMode == Internet)
        {
            UBApplication::webController->paste();
        }
    }
}
