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
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>



#ifdef Q_OS_MAC
#include <Carbon/Carbon.h>
#endif

#include "core/memcheck.h"

namespace
{
    struct UBUpdateDownloadMonitor
    {
        QElapsedTimer interval;
        qint64 lastReceived = 0;
        int slowIntervals = 0;
        bool lowSpeedAbort = false;
    };
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
    mMainWindow->actionWeb->setVisible(Internet != mMainMode || highResolution);
    mMainWindow->boardToolBar->setIconSize(QSize(highResolution ? 48 : 42, mMainWindow->boardToolBar->iconSize().height()));

    mMainWindow->actionBoard->setEnabled(mMainMode != Board);
    mMainWindow->actionWeb->setEnabled(mMainMode != Internet);
    mMainWindow->actionDocument->setEnabled(mMainMode != Document);

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

    if (UBApplication::boardController)
    {
        UBApplication::boardController->persistCurrentScene();
        UBApplication::boardController->hide();
    }

    if (UBSettings::settings()->webUseExternalBrowser->get().toBool())
    {
        showDesktop(true);
        UBApplication::webController->show();
    }
    else
    {
        mMainWindow->boardToolBar->hide();
        mMainWindow->documentToolBar->hide();
        mMainWindow->webToolBar->show();

        mMainMode = Internet;

        adaptToolBar();

        mMainWindow->show();
        mUninoteController->hideWindow();

        UBApplication::webController->show();

        UBApplication::displayManager->adjustScreens();

        emit mainModeChanged(Internet);
    }
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
                                          int retryAttempt)
{
    QList<QUrl> manifestUrls = urls;
    QUrl jsonUrl = url;

    if (manifestUrls.isEmpty())
    {
        const QUrl configuredUrl = url.isEmpty()
                ? UBSettings::settings()->appSoftwareUpdateURL->get().toUrl()
                : url;
        const QString configuredUrlString = configuredUrl.toString();

        // The manifest is tiny, so free GitHub acceleration endpoints are a
        // practical zero-cost first line for networks where GitHub Raw is
        // unavailable.  The canonical URL remains the final source of truth.
        if (configuredUrlString.startsWith("https://raw.githubusercontent.com/",
                                           Qt::CaseInsensitive))
        {
            manifestUrls.append(QUrl("https://gh-proxy.org/" + configuredUrlString));
            manifestUrls.append(QUrl("https://gh-proxy.com/" + configuredUrlString));
        }
        manifestUrls.append(configuredUrl);
        urlIndex = 0;
        retryAttempt = 0;
    }

    if (jsonUrl.isEmpty())
    {
        if (urlIndex < 0 || urlIndex >= manifestUrls.size())
            return;
        jsonUrl = manifestUrls.at(urlIndex);
    }

    qDebug() << "Checking for update at url: " << jsonUrl.toString();

    QNetworkRequest request(jsonUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("OpenBoard-cheyuze/%1").arg(qApp->applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Cache-Control", "no-cache");
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

        if (isNoUpdateDisplayed)
            mMainWindow->information(tr("Check for updates"),
                                     tr("Unable to check for updates. Please check your network connection and try again."));
        reply->deleteLater();
        return;
    }

    // Check if we are being redirected. If so, call checkUpdate again

    QVariant redirect_target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (!redirect_target.isNull()) {
        // The returned URL might be relative. resolved() creates an absolute url from it
        QUrl redirect_url(reply->url().resolved(redirect_target.toUrl()));

        checkUpdate(redirect_url, manifestUrls, urlIndex, retryAttempt);
        reply->deleteLater();
        return;
    }

    // No error and no redirect => we read the whole response

    QString responseString = QString::fromUtf8(reply->readAll());

    if (!responseString.isEmpty() &&
            responseString.contains("version") &&
            responseString.contains("url")) {

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

        if (isNoUpdateDisplayed)
            mMainWindow->information(tr("Check for updates"),
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
        messageBox.setText(tr("A new version %1 is available.").arg(jsonObject.value("version").toString())
                           + "\n" + tr("Current version: %1").arg(qApp->applicationVersion()));
        if (!notes.trimmed().isEmpty())
            messageBox.setInformativeText(tr("What's new:") + notes);
        const QString baiduUrlString = jsonObject.value("baiduUrl").toString().trimmed();
        const QString baiduPassword = jsonObject.value("baiduPassword").toString().trimmed();
        QPushButton *downloadButton = messageBox.addButton(
                tr("Download from domestic mirrors"), QMessageBox::AcceptRole);
        QPushButton *baiduButton = nullptr;
        if (!baiduUrlString.isEmpty())
            baiduButton = messageBox.addButton(tr("Download from Baidu Netdisk"),
                                               QMessageBox::ActionRole);
        messageBox.addButton(tr("Remind me later"), QMessageBox::RejectRole);
        messageBox.exec();

        if (messageBox.clickedButton() == downloadButton) {
            QList<QUrl> urls;
            const QJsonValue urlsValue = jsonObject.value("urls");
            if (urlsValue.isArray()) {
                for (const QJsonValue &urlValue : urlsValue.toArray()) {
                    const QUrl candidate(urlValue.toString());
                    if (candidate.isValid() && !candidate.isEmpty() && !urls.contains(candidate))
                        urls.append(candidate);
                }
            }

            // Keep the original single URL field for compatibility and use it
            // as the last fallback when a future manifest supplies mirrors.
            const QUrl fallbackUrl(jsonObject.value("url").toString());
            if (fallbackUrl.isValid() && !fallbackUrl.isEmpty() && !urls.contains(fallbackUrl))
                urls.append(fallbackUrl);

            if (!urls.isEmpty())
                downloadUpdateInstaller(urls,
                                        jsonObject.value("version").toString(),
                                        jsonObject.value("sha256").toString());
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
        mMainWindow->information(tr("Check for updates"),
                                 tr("You are using the latest version (%1).").arg(qApp->applicationVersion()));
    }
}

void UBApplicationController::downloadUpdateInstaller(const QList<QUrl> &urls,
                                                       const QString &version,
                                                       const QString &expectedSha256,
                                                       int urlIndex,
                                                       int retryAttempt)
{
    if (urlIndex < 0 || urlIndex >= urls.size())
        return;

    const QUrl url = urls.at(urlIndex);
    const QString downloadDirectory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString fileName = QString("OpenBoard-cheyuze-%1-x64.exe").arg(version);
    const QString destination = QDir(downloadDirectory).filePath(fileName);

    QSaveFile *file = new QSaveFile(destination, this);
    if (!file->open(QIODevice::WriteOnly)) {
        mMainWindow->information(tr("Download update"),
                                 tr("Unable to save the installer to: %1").arg(destination));
        file->deleteLater();
        return;
    }

    QProgressDialog *progress = new QProgressDialog(tr("Downloading update %1...").arg(version),
                                                    tr("Cancel"), 0, 100, mMainWindow);
    progress->setWindowTitle(tr("Download update"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setAutoClose(false);
    progress->setMinimumDuration(0);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("OpenBoard-cheyuze/%1").arg(qApp->applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    request.setTransferTimeout(30000);
#endif
    // Keep installer traffic separate from the update-manifest manager, whose
    // finished signal parses every reply as JSON.
    QNetworkAccessManager *downloadManager = new QNetworkAccessManager(progress);
    QNetworkReply *reply = downloadManager->get(request);
    const std::shared_ptr<UBUpdateDownloadMonitor> monitor =
            std::make_shared<UBUpdateDownloadMonitor>();
    monitor->interval.start();

    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, progress,
            [progress, reply, monitor, urlIndex, urls](qint64 received, qint64 total) {
        if (total > 0) {
            progress->setRange(0, 100);
            progress->setValue(static_cast<int>((received * 100) / total));
            progress->setLabelText(tr("Downloading update... %1 MB / %2 MB")
                                   .arg(received / 1024.0 / 1024.0, 0, 'f', 1)
                                   .arg(total / 1024.0 / 1024.0, 0, 'f', 1));
        }
        else {
            progress->setRange(0, 0);
        }

        // A connected but unusably slow GitHub/CDN stream does not produce a
        // network error, so the normal retry path would never run.  Measure
        // three consecutive five-second windows and move to the next mirror
        // when throughput remains below 96 KiB/s.  The final source is kept
        // running so genuinely slow networks can still finish the download.
        const qint64 elapsed = monitor->interval.elapsed();
        if (elapsed >= 5000)
        {
            const qint64 bytesInWindow = received - monitor->lastReceived;
            const qint64 bytesPerSecond = elapsed > 0
                    ? bytesInWindow * 1000 / elapsed : 0;

            if (received >= 256 * 1024 && bytesPerSecond < 96 * 1024)
                ++monitor->slowIntervals;
            else
                monitor->slowIntervals = 0;

            monitor->lastReceived = received;
            monitor->interval.restart();

            if (monitor->slowIntervals >= 3 && urlIndex + 1 < urls.size())
            {
                monitor->lowSpeedAbort = true;
                reply->abort();
            }
        }
    });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, progress, destination, urls, version,
             expectedSha256, urlIndex, retryAttempt, monitor]() {
        progress->close();
        progress->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const QNetworkReply::NetworkError error = reply->error();
            const QString errorText = reply->errorString();
            file->cancelWriting();
            reply->deleteLater();
            file->deleteLater();

            if (error == QNetworkReply::OperationCanceledError)
            {
                if (monitor->lowSpeedAbort && urlIndex + 1 < urls.size())
                {
                    QTimer::singleShot(250, this,
                                       [this, urls, version, expectedSha256, urlIndex]() {
                        downloadUpdateInstaller(urls, version, expectedSha256,
                                                urlIndex + 1, 0);
                    });
                }
                return;
            }

            // Retry transient failures twice on the same source.  If the
            // manifest contains mirrors, move to the next source afterwards.
            if (retryAttempt < 2) {
                QTimer::singleShot(1200, this,
                                   [this, urls, version, expectedSha256, urlIndex, retryAttempt]() {
                    downloadUpdateInstaller(urls, version, expectedSha256,
                                            urlIndex, retryAttempt + 1);
                });
                return;
            }

            if (urlIndex + 1 < urls.size()) {
                QTimer::singleShot(500, this,
                                   [this, urls, version, expectedSha256, urlIndex]() {
                    downloadUpdateInstaller(urls, version, expectedSha256,
                                            urlIndex + 1, 0);
                });
                return;
            }

            mMainWindow->information(tr("Download update"),
                                     tr("Update download failed: %1").arg(errorText));
            return;
        }

        if (!file->commit()) {
            mMainWindow->information(tr("Download update"),
                                     tr("Unable to save the downloaded installer."));
            reply->deleteLater();
            file->deleteLater();
            return;
        }

        if (!expectedSha256.trimmed().isEmpty()) {
            QFile downloadedFile(destination);
            if (!downloadedFile.open(QIODevice::ReadOnly)) {
                mMainWindow->information(tr("Download update"), tr("Unable to verify the installer."));
                reply->deleteLater();
                file->deleteLater();
                return;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&downloadedFile);
            if (QString::fromLatin1(hash.result().toHex()).compare(expectedSha256,
                                                                   Qt::CaseInsensitive) != 0) {
                downloadedFile.close();
                QFile::remove(destination);
                mMainWindow->information(tr("Download update"),
                                         tr("Installer verification failed. Please try again."));
                reply->deleteLater();
                file->deleteLater();
                return;
            }
        }

        const QMessageBox::StandardButton installNow = QMessageBox::question(
            mMainWindow, tr("Download complete"),
            tr("The update has been downloaded. Install it now?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (installNow == QMessageBox::Yes && QProcess::startDetached(destination, QStringList()))
            qApp->quit();

        reply->deleteLater();
        file->deleteLater();
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
