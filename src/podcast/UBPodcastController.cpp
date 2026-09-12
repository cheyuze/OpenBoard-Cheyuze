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




#include "UBPodcastController.h"

#include "frameworks/UBFileSystemUtils.h"
#include "frameworks/UBStringUtils.h"
#include "frameworks/UBPlatformUtils.h"

#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "core/UBSetting.h"
#include "core/UBDisplayManager.h"
#include "desktop/UBCustomCaptureWindow.h"
#include "desktop/UBDesktopAnnotationController.h"

#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "board/UBBoardPaletteManager.h"

#include "gui/UBMainWindow.h"

#include "web/UBWebController.h"
#include "web/simplebrowser/webview.h"

#include "domain/UBGraphicsScene.h"

#include "UBAbstractVideoEncoder.h"

#include "UBPodcastRecordingPalette.h"
#include "UBCameraPreviewWindow.h"

#include <QFileDialog>
#include <QAudioDevice>
#include <QCameraDevice>
#include <QInputDialog>
#include <QMediaDevices>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <string>




#ifdef Q_OS_WIN
    #include <windows.h>
    #include "ffmpeg/UBFFmpegVideoEncoder.h"
    #include "windowsmedia/UBWaveRecorder.h"
#elif defined(Q_OS_OSX)
    #include "ffmpeg/UBFFmpegVideoEncoder.h"
    #include "ffmpeg/UBMicrophoneInput.h"
#elif defined(Q_OS_LINUX)
    #include "ffmpeg/UBFFmpegVideoEncoder.h"
    #include "ffmpeg/UBMicrophoneInput.h"
#endif

#include "core/memcheck.h"

namespace
{
    QString recordingWorkingDirectory()
    {
        QString temporaryRoot = QStandardPaths::writableLocation(
                QStandardPaths::TempLocation);
        if (temporaryRoot.isEmpty())
            temporaryRoot = QDir::tempPath();

        QDir temporaryDirectory(temporaryRoot);
        const QString recordingDirectory = QStringLiteral("OpenBoard/Recordings");
        if (temporaryDirectory.mkpath(recordingDirectory))
            return temporaryDirectory.filePath(recordingDirectory);

        // The system temporary directory should normally be writable.  Keep a
        // non-desktop fallback so an unusual TEMP configuration never exposes
        // the in-progress MP4 beside the user's finished lesson videos.
        const QString applicationData = QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation);
        QDir applicationDataDirectory(applicationData);
        if (!applicationData.isEmpty()
                && applicationDataDirectory.mkpath(QStringLiteral("Recordings")))
        {
            return applicationDataDirectory.filePath(QStringLiteral("Recordings"));
        }

        return temporaryRoot;
    }

    int execCenteredDialog(QDialog *dialog)
    {
        if (!dialog)
            return QDialog::Rejected;

        // Wait until Qt has created and laid out the top-level window.  Moving
        // it before show() is unreliable because its final frame size is not
        // known yet, especially with Windows display scaling enabled.
        QTimer::singleShot(0, dialog, [dialog]() {
            QScreen *screen = dialog->screen();
            if (!screen)
                screen = QGuiApplication::primaryScreen();
            if (!screen)
                return;

            QRect dialogGeometry = dialog->frameGeometry();
            dialogGeometry.moveCenter(screen->availableGeometry().center());
            dialog->move(dialogGeometry.topLeft());
        });

        return dialog->exec();
    }

#ifdef Q_OS_WIN
    struct CaptureWindowInfo
    {
        HWND handle = nullptr;
        QString title;
        QRect geometry;
    };

    BOOL CALLBACK collectCaptureWindows(HWND window, LPARAM data)
    {
        QList<CaptureWindowInfo> *windows =
                reinterpret_cast<QList<CaptureWindowInfo>*>(data);

        if (!windows || !IsWindowVisible(window) || IsIconic(window)
                || GetAncestor(window, GA_ROOT) != window)
        {
            return TRUE;
        }

        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (!(style & WS_CAPTION) || (extendedStyle & WS_EX_TOOLWINDOW))
            return TRUE;

        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId == GetCurrentProcessId())
            return TRUE;

        const int titleLength = GetWindowTextLengthW(window);
        if (titleLength <= 0)
            return TRUE;

        std::wstring titleBuffer(static_cast<size_t>(titleLength + 1), L'\0');
        GetWindowTextW(window, titleBuffer.data(), titleLength + 1);
        const QString title = QString::fromWCharArray(titleBuffer.c_str()).trimmed();
        if (title.isEmpty())
            return TRUE;

        RECT nativeRect = {};
        if (!GetWindowRect(window, &nativeRect))
            return TRUE;

        const QRect geometry(nativeRect.left, nativeRect.top,
                nativeRect.right - nativeRect.left,
                nativeRect.bottom - nativeRect.top);
        if (geometry.width() < 160 || geometry.height() < 90)
            return TRUE;

        windows->append({window, title, geometry});
        return TRUE;
    }

    QList<CaptureWindowInfo> availableCaptureWindows()
    {
        QList<CaptureWindowInfo> windows;
        EnumWindows(collectCaptureWindows, reinterpret_cast<LPARAM>(&windows));
        std::sort(windows.begin(), windows.end(),
                [](const CaptureWindowInfo& left, const CaptureWindowInfo& right) {
                    return left.title.localeAwareCompare(right.title) < 0;
                });
        return windows;
    }

    QScreen *qtScreenForNativeWindow(HWND window, MONITORINFOEXW *monitorInfo = nullptr)
    {
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        const HMONITOR monitor = MonitorFromWindow(window,
                MONITOR_DEFAULTTONEAREST);
        if (!monitor || !GetMonitorInfoW(monitor, &info))
            return QGuiApplication::primaryScreen();

        if (monitorInfo)
            *monitorInfo = info;

        const QString deviceName = QString::fromWCharArray(info.szDevice);
        foreach (QScreen *screen, QGuiApplication::screens())
        {
            if (screen->name().compare(deviceName, Qt::CaseInsensitive) == 0)
                return screen;
        }
        return QGuiApplication::primaryScreen();
    }

    QRect nativeWindowGeometry(quintptr windowId)
    {
        HWND window = reinterpret_cast<HWND>(windowId);
        RECT nativeRect = {};
        if (!window || !IsWindow(window) || !GetWindowRect(window, &nativeRect))
            return QRect();

        MONITORINFOEXW monitorInfo = {};
        QScreen *screen = qtScreenForNativeWindow(window, &monitorInfo);
        if (!screen)
            return QRect();

        const qreal scale = screen->devicePixelRatio() > 0.0
                ? screen->devicePixelRatio() : 1.0;
        const QRect logicalScreen = screen->geometry();
        return QRect(
                logicalScreen.left()
                    + qRound((nativeRect.left - monitorInfo.rcMonitor.left) / scale),
                logicalScreen.top()
                    + qRound((nativeRect.top - monitorInfo.rcMonitor.top) / scale),
                qRound((nativeRect.right - nativeRect.left) / scale),
                qRound((nativeRect.bottom - nativeRect.top) / scale));
    }

    QPixmap captureNativeWindow(HWND window)
    {
        RECT nativeRect = {};
        if (!window || !IsWindow(window) || IsIconic(window)
                || !GetWindowRect(window, &nativeRect))
            return QPixmap();

        const int width = nativeRect.right - nativeRect.left;
        const int height = nativeRect.bottom - nativeRect.top;
        if (width <= 0 || height <= 0)
            return QPixmap();

        HDC screenDc = GetDC(nullptr);
        HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
        HBITMAP bitmap = memoryDc
                ? CreateCompatibleBitmap(screenDc, width, height) : nullptr;
        if (!screenDc || !memoryDc || !bitmap)
        {
            if (bitmap)
                DeleteObject(bitmap);
            if (memoryDc)
                DeleteDC(memoryDc);
            if (screenDc)
                ReleaseDC(nullptr, screenDc);
            return QPixmap();
        }

        HGDIOBJ previousBitmap = SelectObject(memoryDc, bitmap);
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
        const BOOL printed = PrintWindow(window, memoryDc, PW_RENDERFULLCONTENT);
        SelectObject(memoryDc, previousBitmap);

        QImage image;
        if (printed)
        {
            image = QImage(width, height, QImage::Format_ARGB32);
            BITMAPINFO bitmapInfo = {};
            bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth = width;
            bitmapInfo.bmiHeader.biHeight = -height;
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;

            if (!GetDIBits(screenDc, bitmap, 0, height, image.bits(),
                    &bitmapInfo, DIB_RGB_COLORS))
            {
                image = QImage();
            }
            else
            {
                // GDI leaves the alpha channel undefined for ordinary HWNDs.
                // Make every captured target-window pixel fully opaque before
                // compositing OpenBoard's transparent annotation layer.
                for (int y = 0; y < image.height(); ++y)
                {
                    QRgb *line = reinterpret_cast<QRgb*>(image.scanLine(y));
                    for (int x = 0; x < image.width(); ++x)
                        line[x] = line[x] | 0xff000000;
                }
            }
        }

        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    }
#endif
}

UBPodcastController* UBPodcastController::sInstance = 0;

unsigned int UBPodcastController::sBackgroundColor = 0x00000000;  // BBGGRRAA


UBPodcastController::UBPodcastController(QObject* pParent)
    : QObject(pParent)
    , mVideoEncoder(0)
    , mInitialized(false)
    , mEmptyChapter(true)
    , mVideoFramesPerSecondAtStart(30)
    , mVideoFrameSizeAtStart(1024, 768)
    , mVideoBitsPerSecondAtStart(12000000)
    , mSourceWidget(0)
    , mIsDesktopMode(false)
    , mSourceScene(0)
    , mScreenGrabingTimerEventID(0)
    , mRecordingProgressTimerEventID(0)
    , mRecordingPalette(0)
    , mCameraPreview(0)
    , mRecordingState(Stopped)
    , mApplicationIsClosing(false)
    , mDefaultAudioInputDeviceAction(0)
    , mNoAudioInputDeviceAction(0)
    , mSmallVideoSizeAction(0)
    , mMediumVideoSizeAction(0)
    , mFullVideoSizeAction(0)
    , mFullScreenCaptureAction(0)
    , mAreaCaptureAction(0)
    , mApplicationWindowCaptureAction(0)
    , mDesktopCaptureMode(FullScreenCapture)
    , mDesktopCapturePrepared(false)
    , mDesktopCaptureWindowId(0)
{
    connect(UBApplication::applicationController, SIGNAL(mainModeChanged(UBApplicationController::MainMode)),
            this, SLOT(applicationMainModeChanged(UBApplicationController::MainMode)));

    connect(UBApplication::applicationController, SIGNAL(desktopMode(bool)),
            this, SLOT(applicationDesktopMode(bool)));

    connect(UBApplication::webController, SIGNAL(activeWebPageChanged(WebView*)),
            this, SLOT(webActiveWebPageChanged(WebView*)));

    connect(UBApplication::app(), SIGNAL(lastWindowClosed()),
            this, SLOT(applicationAboutToQuit()));
    // Menu Exit calls QCoreApplication::quit() directly and need not close a
    // window first. Both exit routes must finish the recording synchronously.
    connect(UBApplication::app(), SIGNAL(aboutToQuit()),
            this, SLOT(applicationAboutToQuit()));

}

UBPodcastController::~UBPodcastController()
{
    delete mCameraPreview;
    mCameraPreview = 0;
    delete mRecordingPalette;
    mRecordingPalette = 0;
}


void UBPodcastController::applicationAboutToQuit()
{
    mApplicationIsClosing = true;

    if(mRecordingState == Recording || mRecordingState == Paused)
    {
        stop();
    }

    // lastWindowClosed can be followed immediately by event-loop termination.
    // Finish the container and auto-save now, including a stop already in flight.
    if (mVideoEncoder)
        mVideoEncoder->finishPendingRecording();
}


void UBPodcastController::groupActionTriggered(QAction* action)
{
    Q_UNUSED(action);
    updateActionState();
}


void UBPodcastController::updateActionState()
{
    if (mSmallVideoSizeAction && mSmallVideoSizeAction->isChecked())
        UBSettings::settings()->podcastVideoSize->set("Small");
    else if (mFullVideoSizeAction && mFullVideoSizeAction->isChecked())
        UBSettings::settings()->podcastVideoSize->set("Full");
    else
        UBSettings::settings()->podcastVideoSize->set("Medium");

    if (mDefaultAudioInputDeviceAction && mDefaultAudioInputDeviceAction->isChecked())
         selectAudioInputDevice(QStringLiteral("Default"));
    else if (mNoAudioInputDeviceAction && mNoAudioInputDeviceAction->isChecked())
         selectAudioInputDevice(QStringLiteral("None"));
    else
    {
        foreach(QAction* action, mAudioInputDevicesActions)
        {
            if (action->isChecked())
            {
                selectAudioInputDevice(action->data().toString().isEmpty()
                        ? action->text() : action->data().toString());
                break;
            }
        }
    }

}

void UBPodcastController::widgetSizeChanged(const QSizeF size)
{
    qDebug() << "widgetSizeChanged to" << size << "video" << mVideoFrameSizeAtStart;
    mInitialized = false;
    mViewToVideoTransform.reset();

    QSizeF videoFrameSize(mVideoFrameSizeAtStart);
    qreal scaleHorizontal = videoFrameSize.width() / size.width();
    qreal scaleVertical = videoFrameSize.height() / size.height();
    qreal scale = qMin(scaleHorizontal, scaleVertical);

    mViewToVideoTransform.scale(scale, scale);

    QSizeF scaledWidgetSize = size * scale;
    int offsetX = (videoFrameSize.width() - scaledWidgetSize.width()) / 2;
    int offsetY = (videoFrameSize.height() - scaledWidgetSize.height()) / 2;

    mViewToVideoTransform.translate(offsetX / scale, offsetY / scale);
}


void UBPodcastController::setSourceWidget(QWidget* pWidget)
{
    if (mSourceWidget != pWidget)
    {
        // cleanup timer and event filter
        if (mScreenGrabingTimerEventID)
        {
            killTimer(mScreenGrabingTimerEventID);
            mScreenGrabingTimerEventID = 0;
        }

        if (mSourceWidget)
        {
            mSourceWidget->removeEventFilter(this);
        }

        // setup new source widget
        mSourceWidget = pWidget;
        mInitialized = false;
        mViewToVideoTransform.reset();
        mLatestCapture.fill(sBackgroundColor);

        if (mSourceWidget)
        {
            widgetSizeChanged(mSourceWidget->size());

            UBBoardView *bv = qobject_cast<UBBoardView *>(mSourceWidget);

            if (bv && !mIsDesktopMode)
            {
                connect(UBApplication::boardController, SIGNAL(activeSceneChanged()), this, SLOT(activeSceneChanged()));
                connect(UBApplication::boardController, SIGNAL(backgroundChanged()), this, SLOT(sceneBackgroundChanged()));
                connect(UBApplication::boardController, SIGNAL(controlViewportChanged()), this, SLOT(activeSceneChanged()));

                activeSceneChanged();
            }
            else
            {
                disconnect(UBApplication::boardController, SIGNAL(activeSceneChanged()), this, SLOT(activeSceneChanged()));
                disconnect(UBApplication::boardController, SIGNAL(backgroundChanged()), this, SLOT(sceneBackgroundChanged()));
                disconnect(UBApplication::boardController, SIGNAL(controlViewportChanged()), this, SLOT(activeSceneChanged()));

                mSourceScene = nullptr;

                startNextChapter();

                if (mIsDesktopMode || UBApplication::applicationController->displayMode() == UBApplicationController::Internet)
                {
                    mScreenGrabingTimerEventID  = startTimer(1000 / mVideoFramesPerSecondAtStart);
                }
            }

            mSourceWidget->installEventFilter(this);
        }
    }
}


UBPodcastController* UBPodcastController::instance()
{
    if(!sInstance)
        sInstance = new UBPodcastController(UBApplication::staticMemoryCleaner);

    return sInstance;
}


void UBPodcastController::start()
{
    if (mRecordingState == Stopped)
    {
        mInitialized = false;
        mEmptyChapter = true;
        mEncodingError.clear();
        mCaptureFailureStartedAt = -1;
        stopRecordingTimers();
        mMonotonicTimer.invalidate();
        mRecordingClock = UBRecordingClock();
        mSuggestedRecordingFilePath.clear();

        // Starting a recording used to call applicationMainModeChanged()
        // unconditionally.  In desktop mode that reset mIsDesktopMode and
        // switched the source back to the board, so the resulting video still
        // contained the whiteboard.  Preserve the actual mode at the moment
        // the user presses Record and size the video from that source.
        const bool desktopRecording = UBApplication::applicationController->isShowingDesktop();

#ifdef Q_OS_WIN
        // A selected application may have been closed or minimized while the
        // user was preparing the lesson.  Re-open the picker instead of
        // silently producing an empty or full-screen recording.
        if (desktopRecording
                && mDesktopCaptureMode == ApplicationWindowCapture
                && mDesktopCapturePrepared)
        {
            HWND targetWindow = reinterpret_cast<HWND>(mDesktopCaptureWindowId);
            if (!targetWindow || !IsWindow(targetWindow) || IsIconic(targetWindow))
                mDesktopCapturePrepared = false;
        }
#endif

        if (desktopRecording && !mDesktopCapturePrepared
                && !prepareDesktopCapture())
        {
            QSignalBlocker blocker(UBApplication::mainWindow->actionPodcastRecord);
            UBApplication::mainWindow->actionPodcastRecord->setChecked(false);
            if (mRecordingPalette)
            {
                mRecordingPalette->show();
                mRecordingPalette->raise();
            }
            return;
        }

        // Area/window selection may change the source rectangle after the
        // camera was enabled. Keep the on-screen avatar inside the exact
        // region that will be recorded.
        if (desktopRecording && mCameraPreview && mCameraPreview->isVisible())
            positionCameraPreview();

        const QSize sourceSize = desktopRecording
                ? currentDesktopCaptureRect().size()
                : UBApplication::boardController->controlView()->size();

        QSize recommendedSize(1024, 768);

        // Older OpenBoard profiles were fixed at 10 fps, which makes pen
        // strokes and cursor movement visibly judder. Keep explicit modern
        // values configurable, but transparently upgrade the legacy default.
        const int configuredFramesPerSecond =
                UBSettings::settings()->podcastFramesPerSecond->get().toInt();
        mVideoFramesPerSecondAtStart = configuredFramesPerSecond <= 10
                ? 30 : qBound(15, configuredFramesPerSecond, 60);

        int fullBitRate = UBSettings::settings()->podcastWindowsMediaBitsPerSecond->get().toInt();
        // Migrate the former 1.7 Mbps ceiling. At native 1080p resolution it
        // visibly softens small text and thin handwriting.
        if (fullBitRate <= 1700000)
            fullBitRate = 12000000;

        if (mSmallVideoSizeAction && mSmallVideoSizeAction->isChecked())
        {
            recommendedSize = QSize(640, 480);
            mVideoBitsPerSecondAtStart = fullBitRate / 4;
        }
        else if (mMediumVideoSizeAction && mMediumVideoSizeAction->isChecked())
        {
            recommendedSize = QSize(1024, 768);
            mVideoBitsPerSecondAtStart = fullBitRate / 2;
        }
        else if (mFullVideoSizeAction && mFullVideoSizeAction->isChecked())
        {
            recommendedSize = sourceSize;
            mVideoBitsPerSecondAtStart = fullBitRate;
        }

        QSize scaledSourceSize = sourceSize;
        scaledSourceSize.scale(recommendedSize, Qt::KeepAspectRatio);

        // Video width/height should be a multiple of 4

        int width = scaledSourceSize.width();
        int height = scaledSourceSize.height();

        if (width % 4 != 0)
                width = ((width / 4) * 4);

        if (height % 4 != 0)
                height = ((height / 4) * 4);

        mVideoFrameSizeAtStart = QSize(width, height);

        if (desktopRecording)
        {
            mIsDesktopMode = true;
            setSourceWidget(UBApplication::displayManager->widget(ScreenRole::Desktop));
        }
        else
        {
            applicationMainModeChanged(UBApplication::applicationController->displayMode());
        }

        // setSourceWidget() is intentionally a no-op when the source pointer
        // has not changed, but the selected output resolution may have.  Keep
        // the source-to-video transform in sync for every new recording.
        widgetSizeChanged(sourceSize);

#ifdef Q_OS_WIN
        // Encode H.264/AAC directly into MP4. This avoids the former WMV ->
        // MP4 post-processing pass and keeps both streams on one timeline.
        mVideoEncoder = new UBFFmpegVideoEncoder(this);  //deleted on stop
#elif defined(Q_OS_OSX)
        mVideoEncoder = new UBFFmpegVideoEncoder(this);
#elif defined(Q_OS_LINUX)
        mVideoEncoder = new UBFFmpegVideoEncoder(this);
#endif

        if (mVideoEncoder)
        {
            connect(mVideoEncoder, SIGNAL(encodingStatus(const QString&)), this, SLOT(encodingStatus(const QString&)));
            connect(mVideoEncoder, SIGNAL(encodingFinished(bool)), this, SLOT(encodingFinished(bool)));
            // Failure may occur while newPixmap() is still on the stack. Stop
            // on the next event-loop turn rather than destroying its encoder
            // reentrantly from a worker/capture callback.
            connect(mVideoEncoder, &UBAbstractVideoEncoder::encodingError,
                    this, &UBPodcastController::encodingError, Qt::QueuedConnection);

            if(mRecordingPalette)
            {
                connect(mVideoEncoder, SIGNAL(audioLevelChanged(quint8))
                        , mRecordingPalette, SLOT(audioLevelChanged(quint8)));
            }

            const QString selectedInput = selectedAudioInputDevice();
            mVideoEncoder->setRecordAudio(selectedInput != QStringLiteral("None"));

            const QString recordingDevice = selectedInput == QStringLiteral("Default")
                    ? QString() : selectedInput;

            mVideoEncoder->setAudioRecordingDevice(recordingDevice);

            mVideoEncoder->setFramesPerSecond(mVideoFramesPerSecondAtStart);
            mVideoEncoder->setVideoSize(mVideoFrameSizeAtStart);
            mVideoEncoder->setVideoBitsPerSecond(mVideoBitsPerSecondAtStart);

            mPartNumber = 0;

            mPodcastRecordingPath = UBSettings::settings()->userPodcastRecordingDirectory();

            qDebug() << "mPodcastRecordingPath: " << mPodcastRecordingPath;

            QString videoFileName;
            const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");

            mSuggestedRecordingFilePath = UBFileSystemUtils::nextAvailableFileName(
                    mPodcastRecordingPath + "/" + tr("讲题录制-%1").arg(timestamp)
                            + "." + mVideoEncoder->videoFileExtension(), " ");

            // Encode H.264/AAC directly into a unique MP4 under the system
            // temporary directory.  After the encoder has written the MP4
            // trailer and closed the file, encodingFinished() presents Save
            // As and moves the completed video to the user's chosen location.
            // Keeping the working file away from mPodcastRecordingPath avoids
            // showing an unfinished recording on the desktop.
            const QString workingDirectory = recordingWorkingDirectory();
            videoFileName = QDir(workingDirectory).filePath(
                    tr("OpenBoard录制-处理中-%1").arg(
                            QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz"))
                    + "." + mVideoEncoder->videoFileExtension());

            videoFileName = UBFileSystemUtils::nextAvailableFileName(videoFileName, " ");

            qDebug() << "Recording working file:" << videoFileName;

            mVideoEncoder->setVideoFileName(videoFileName);

            mLatestCapture = QImage(mVideoFrameSizeAtStart, QImage::Format_RGB32); //0xffRRGGBB

            if(mVideoEncoder->start())
            {
                mMonotonicTimer.start();
                mRecordingClock.start(0);
                mRecordingProgressTimerEventID = startTimer(100);
                emit recordingProgressChanged(0);
                setRecordingState(Recording);

                // Keep producing frames even when the board itself is unchanged,
                // so cursor movement used to guide an explanation is recorded.
                if (!mScreenGrabingTimerEventID)
                    mScreenGrabingTimerEventID = startTimer(1000 / mVideoFramesPerSecondAtStart);

                if (mSourceScene)
                {
                    processScenePaintEvent();
                }
                else
                {
                    processScreenGrabingTimerEvent();
                }
            }
            else
            {
                const QString error = mVideoEncoder->lastErrorMessage();
                UBAbstractVideoEncoder *failedEncoder = mVideoEncoder;
                mVideoEncoder = nullptr;
                disconnect(failedEncoder, nullptr, this, nullptr);
                failedEncoder->deleteLater();
                stopRecordingTimers();
                mMonotonicTimer.invalidate();
                QSignalBlocker blocker(UBApplication::mainWindow->actionPodcastRecord);
                UBApplication::mainWindow->actionPodcastRecord->setChecked(false);
                emit recordingStateChanged(Stopped);
                UBApplication::showMessage(tr("Failed to start encoder (%1)").arg(error), false);
            }
        }
        else
        {
            stopRecordingTimers();
            QSignalBlocker blocker(UBApplication::mainWindow->actionPodcastRecord);
            UBApplication::mainWindow->actionPodcastRecord->setChecked(false);
            emit recordingStateChanged(Stopped);
            UBApplication::showMessage(tr("No Podcast encoder available ..."), false);
        }
    }
}

void UBPodcastController::pause()
{
    if (mVideoEncoder && mRecordingState == Recording && mVideoEncoder->canPause())
    {
        sendLatestPixmapToEncoder();

        if (mVideoEncoder->pause())
        {
            mCaptureFailureStartedAt = -1;
            mRecordingClock.pause(mMonotonicTimer.elapsed());
            setRecordingState(Paused);
            emit recordingProgressChanged(elapsedRecordingMs());
        }
    }
}


void UBPodcastController::unpause()
{
    if (mVideoEncoder && mRecordingState == Paused && mVideoEncoder->canPause())
    {
        if (mVideoEncoder->unpause())
        {
             mCaptureFailureStartedAt = -1;
             mRecordingClock.resume(mMonotonicTimer.elapsed());
             setRecordingState(Recording);
             // Refresh content that may have changed during the pause.
             mInitialized = false;
             if (mSourceScene && !mIsDesktopMode)
                 processScenePaintEvent();
             else
                 processScreenGrabingTimerEvent();
        }
    }
}


void UBPodcastController::stop()
{
    if ((mRecordingState == Recording || mRecordingState == Paused) && mVideoEncoder)
    {
        const bool stoppedWhilePaused = (mRecordingState == Paused);

        stopRecordingTimers();
        mRecordingClock.stop(mMonotonicTimer.elapsed());
        emit recordingProgressChanged(elapsedRecordingMs());

        // pause() already submits the last visible frame before stopping the
        // capture clock. If recording is ended without resuming, submitting
        // another frame here would use wall-clock time that still includes
        // the paused interval and artificially extend the MP4 duration.
        if (!stoppedWhilePaused && mEncodingError.isEmpty())
            sendLatestPixmapToEncoder();

        setRecordingState(Stopping);

        mVideoEncoder->stop();
    }

    mSourceScene = nullptr;
}


bool UBPodcastController::eventFilter(QObject *obj, QEvent *event)
{
    if (mRecordingState == Recording && event->type() == QEvent::Resize)
    {
        QResizeEvent *resizeEvent = static_cast<QResizeEvent*>(event);
        widgetSizeChanged(resizeEvent->size());
    }

    if (mCameraPreview && mCameraPreview->isVisible()
            && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
    {
        QTimer::singleShot(0, this, &UBPodcastController::positionCameraPreview);
    }

    return QObject::eventFilter(obj, event);
}


void UBPodcastController::activeSceneChanged()
{
    if (mSourceScene)
    {
        disconnect(mSourceScene.get(), SIGNAL(changed(const QList<QRectF>&)),
                this, SLOT(sceneChanged(const QList<QRectF> &)));
    }

    mSourceScene = UBApplication::boardController->activeScene();

    connect(mSourceScene.get(), SIGNAL(changed(const QList<QRectF>&)),
        this, SLOT(sceneChanged(const QList<QRectF> &)));

    mInitialized = false;

    startNextChapter();

    UBBoardView *bv = qobject_cast<UBBoardView*>(mSourceWidget);
    if (bv)
    {
        QRectF viewportRect = bv->mapToScene(bv->geometry()).boundingRect();
        mSceneRepaintRectQueue.enqueue(viewportRect);
    }

    processScenePaintEvent();
}

void UBPodcastController::sceneBackgroundChanged()
{
    UBBoardView *bv = qobject_cast<UBBoardView*>(mSourceWidget);

    if (bv)
    {
        mInitialized = false;
    }

    processScenePaintEvent();
}


qint64 UBPodcastController::elapsedRecordingMs() const
{
    return mRecordingClock.elapsed(mMonotonicTimer.isValid() ? mMonotonicTimer.elapsed() : 0);
}


void UBPodcastController::stopRecordingTimers()
{
    if (mScreenGrabingTimerEventID)
    {
        killTimer(mScreenGrabingTimerEventID);
        mScreenGrabingTimerEventID = 0;
    }
    if (mRecordingProgressTimerEventID)
    {
        killTimer(mRecordingProgressTimerEventID);
        mRecordingProgressTimerEventID = 0;
    }
}


void UBPodcastController::startNextChapter()
{
    if (mVideoEncoder && !mEmptyChapter)
    {
        //punch chapter in
        ++mPartNumber;
        mVideoEncoder->newChapter(tr("Part %1").arg(mPartNumber), elapsedRecordingMs());
        mEmptyChapter = true;
        qDebug() << "Start chapter" << mPartNumber;
    }
}


void UBPodcastController::sceneChanged(const QList<QRectF> & region)
{
    if(mRecordingState != Recording)
        return;

    bool shouldRepaint = (mSceneRepaintRectQueue.length() == 0);

    UBBoardView *bv = qobject_cast<UBBoardView *>(mSourceWidget);
    if (bv)
    {
        QRectF viewportRect = bv->mapToScene(QRect(0, 0, bv->width(), bv->height())).boundingRect();
        foreach(const QRectF rect, region)
        {
            QRectF maxRect = rect.intersected(viewportRect);
            mSceneRepaintRectQueue.enqueue(maxRect);
        }

        if (shouldRepaint)
            QTimer::singleShot(1000.0 / mVideoFramesPerSecondAtStart, this, SLOT(processScenePaintEvent()));

    }
}


void UBPodcastController::processScenePaintEvent()
{
    if(mRecordingState != Recording)
        return;

    UBBoardView *bv = qobject_cast<UBBoardView *>(mSourceWidget);

    if(!bv)
        return;

    QRectF repaintRect;

    if (!mInitialized)
    {
        mSceneRepaintRectQueue.clear();
        repaintRect = bv->mapToScene(QRect(0, 0, bv->width(), bv->height())).boundingRect();

        if (bv->scene()->isDarkBackground())
            mLatestCapture.fill(Qt::black);
        else
            mLatestCapture.fill(Qt::white);

        mInitialized = true;
    }
    else
    {
        while(mSceneRepaintRectQueue.size() > 0)
        {
            repaintRect = repaintRect.united(mSceneRepaintRectQueue.dequeue());
        }
    }

    if (!repaintRect.isNull())
    {
        std::shared_ptr<UBGraphicsScene> scene = bv->scene();

        QPainter p(&mLatestCapture);

        p.setTransform(mViewToVideoTransform);
        p.setTransform(bv->viewportTransform(), true);

        p.setRenderHints(QPainter::Antialiasing);
        p.setRenderHints(QPainter::SmoothPixmapTransform);

        repaintRect.adjust(-1, -1, 1, 1);

        p.setClipRect(repaintRect);

        if (scene->isDarkBackground())
            p.fillRect(repaintRect, Qt::black);
        else
            p.fillRect(repaintRect, Qt::white);

        scene->setRenderingContext(UBGraphicsScene::Podcast);

        scene->render(&p, repaintRect, repaintRect);

        scene->setRenderingContext(UBGraphicsScene::Screen);

        p.end();
        sendLatestPixmapToEncoder();
    }
}


void UBPodcastController::applicationMainModeChanged(UBApplicationController::MainMode pMode)
{
    mIsDesktopMode = false;

    if (pMode == UBApplicationController::Internet)
    {
        setSourceWidget(UBApplication::webController->controlView());
    }
    else
    {
        setSourceWidget(UBApplication::boardController->controlView());
    }
}


void UBPodcastController::applicationDesktopMode(bool displayed)
{
    mIsDesktopMode = displayed;

    if (displayed)
    {
        setSourceWidget(UBApplication::displayManager->widget(ScreenRole::Desktop));

        // The main OpenBoard window is hidden in desktop mode.  Keep the
        // recorder as an independent, always-on-top tool window and make it
        // available immediately so recording can be started from the desktop.
        toggleRecordingPalette(true);
        QSignalBlocker blocker(UBApplication::mainWindow->actionPodcast);
        UBApplication::mainWindow->actionPodcast->setChecked(true);
        positionRecordingPalette(true);
        mRecordingPalette->setNativeOwner(
                UBApplication::applicationController->uninotesController()->drawingView());
    }

    else
    {
        mDesktopCapturePrepared = false;
        if (mRecordingPalette)
            mRecordingPalette->setNativeOwner(UBApplication::mainWindow);
        applicationMainModeChanged(UBApplication::applicationController->displayMode());
        if (mRecordingPalette && mRecordingPalette->isVisible())
            positionRecordingPalette(false);
    }

    if (mCameraPreview && mCameraPreview->isVisible())
        positionCameraPreview();
}


void UBPodcastController::webActiveWebPageChanged(WebView* pWebView)
{
    if(UBApplication::applicationController->displayMode() == UBApplicationController::Internet)
    {
        setSourceWidget(pWebView);
    }
}


void UBPodcastController::encodingStatus(const QString& pStatus)
{
    UBApplication::showMessage(pStatus, true);
}


void UBPodcastController::encodingError(const QString& message)
{
    // Queued notifications from an already-finished session must never stop a
    // later recording. Only the currently attached encoder owns this session.
    if (!mVideoEncoder || sender() != mVideoEncoder.data())
        return;

    reportRecordingFailure(message);
}


void UBPodcastController::reportRecordingFailure(const QString& message)
{
    if (mEncodingError.isEmpty())
        mEncodingError = message.isEmpty() ? tr("未知录制错误") : message;

    qWarning() << "Recording stopped after encoding failure:" << mEncodingError;
    if (mRecordingState == Recording || mRecordingState == Paused)
        stop();
}


void UBPodcastController::encodingFinished(bool ok)
{
    if (!mVideoEncoder || sender() != mVideoEncoder.data())
        return;

    UBAbstractVideoEncoder *finishedEncoder = mVideoEncoder;
    const QString workingFilePath = finishedEncoder->videoFileName();
    const QString error = mEncodingError.isEmpty()
            ? finishedEncoder->lastErrorMessage() : mEncodingError;
    // A Save As / error dialog runs a nested event loop. Detach the encoder
    // before opening it so stale signals and deferred deletion cannot be
    // confused with another recording.
    mVideoEncoder = nullptr;
    disconnect(finishedEncoder, nullptr, this, nullptr);
    finishedEncoder->deleteLater();
    stopRecordingTimers();
    if (mMonotonicTimer.isValid())
        mRecordingClock.stop(mMonotonicTimer.elapsed());
    setRecordingState(Stopping);

    if (ok && mEncodingError.isEmpty())
    {
        const QString finalVideoFilePath = saveRecordingAs(workingFilePath);
        if (finalVideoFilePath.isEmpty())
        {
            QFile::remove(workingFilePath);
            UBApplication::showMessage(tr("The recording was discarded."), false);
        }
        else
        {
            mPodcastRecordingPath = QFileInfo(finalVideoFilePath).absolutePath();

            if (!mApplicationIsClosing)
            {
                QString location;

                if (mPodcastRecordingPath == QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
                    location = tr("on your desktop ...");
                else
                {
                    location = tr("in folder %1").arg(mPodcastRecordingPath);
                }

                UBApplication::showMessage(tr("Podcast created %1").arg(location), false);
            }
        }
    }
    else
    {
        const QString reason = error.isEmpty() ? tr("未知录制错误") : error;
        QString message = tr("录制遇到错误，已停止，避免继续生成画面不更新的视频。\n\n原因：%1").arg(reason);
        if (QFileInfo::exists(workingFilePath))
            message += tr("\n\n已保留录制文件，可能包含可恢复的内容：\n%1").arg(QDir::toNativeSeparators(workingFilePath));
        else
            message += tr("\n\n未生成可恢复的录制文件。");

        qWarning() << message;
        UBApplication::showMessage(message, false);
        if (!mApplicationIsClosing)
        {
            QMessageBox errorDialog(QMessageBox::Warning, tr("录制已停止"),
                    message, QMessageBox::Ok, UBApplication::mainWindow);
            errorDialog.setTextFormat(Qt::PlainText);
            execCenteredDialog(&errorDialog);
        }
    }

    mEncodingError.clear();
    setRecordingState(Stopped);
}


QString UBPodcastController::saveRecordingAs(const QString& temporaryFilePath)
{
    if (temporaryFilePath.isEmpty())
        return temporaryFilePath;

    const QFileInfo temporaryInfo(temporaryFilePath);
    QString suggestedPath = mSuggestedRecordingFilePath;
    if (suggestedPath.isEmpty())
    {
        suggestedPath = UBFileSystemUtils::nextAvailableFileName(
                QDir(mPodcastRecordingPath).filePath(
                        tr("讲题录制-%1.%2")
                                .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),
                                     temporaryInfo.suffix())), " ");
    }

    // The recording palette is normally docked near the lower-right corner.
    // Using it as the owner makes Windows place both the native Save As dialog
    // and the discard confirmation beside the palette.  Own all recording
    // dialogs from the main window so they are centred on the application (and
    // therefore on the current screen) regardless of palette position.
    QWidget* dialogParent = UBApplication::mainWindow;
    QString destination;
    if (!mApplicationIsClosing)
    {
        while (destination.isEmpty())
        {
            QFileDialog saveDialog(dialogParent);
            saveDialog.setOption(QFileDialog::DontUseNativeDialog, true);
            saveDialog.setWindowTitle(tr("保存录制视频"));
            saveDialog.setAcceptMode(QFileDialog::AcceptSave);
            saveDialog.setFileMode(QFileDialog::AnyFile);
            saveDialog.setNameFilter(tr("MP4 视频 (*.mp4)"));
            saveDialog.setDefaultSuffix(QStringLiteral("mp4"));
            // The Windows file-system model can expose English type names
            // (for example "File Folder") even when the dialog itself is
            // translated.  The list view keeps this recording workflow
            // compact and avoids mixing those system-provided labels into the
            // otherwise fully Chinese interface.
            saveDialog.setViewMode(QFileDialog::List);
            saveDialog.setLabelText(QFileDialog::LookIn,
                    QStringLiteral("保存位置："));
            saveDialog.setLabelText(QFileDialog::FileName,
                    QStringLiteral("文件名："));
            saveDialog.setLabelText(QFileDialog::FileType,
                    QStringLiteral("文件类型："));
            saveDialog.setLabelText(QFileDialog::Accept,
                    QStringLiteral("保存"));
            saveDialog.setLabelText(QFileDialog::Reject,
                    QStringLiteral("取消"));
            saveDialog.setDirectory(QFileInfo(suggestedPath).absolutePath());
            saveDialog.selectFile(QFileInfo(suggestedPath).fileName());
            saveDialog.resize(900, 560);

            if (execCenteredDialog(&saveDialog) == QDialog::Accepted)
                destination = saveDialog.selectedFiles().value(0);

            if (!destination.isEmpty())
                break;

            QMessageBox discardDialog(QMessageBox::Question,
                    tr("放弃录制"),
                    tr("未保存的录制视频将会丢失。是否确认放弃本次录制？"),
                    QMessageBox::Yes | QMessageBox::No,
                    dialogParent);
            discardDialog.setDefaultButton(QMessageBox::No);
            const QMessageBox::StandardButton discard =
                    static_cast<QMessageBox::StandardButton>(
                            execCenteredDialog(&discardDialog));
            if (discard == QMessageBox::Yes)
                return QString();
        }
    }

    // Closing the application bypasses the Save As dialog. Keep the recording
    // under its unique suggested name so the shutdown path remains recoverable.
    if (destination.isEmpty())
        destination = UBFileSystemUtils::nextAvailableFileName(suggestedPath, " ");

    if (QFileInfo(destination).suffix().isEmpty())
        destination += ".mp4";

    destination = QDir::toNativeSeparators(destination);
    const QString source = QDir::toNativeSeparators(temporaryFilePath);
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(destination).absoluteFilePath())
        return destination;

    // A destination that does not exist can usually be reached with a cheap
    // rename.  This keeps normal saves instant when the working file and the
    // selected folder are on the same volume.
    if (!QFile::exists(destination) && QFile::rename(source, destination))
        return destination;

    // QSaveFile writes to a sibling temporary file and only replaces the
    // destination during commit().  Therefore an existing lesson is retained
    // if a cross-volume copy, disk write, or final replacement fails.
    QFile sourceFile(source);
    QSaveFile destinationFile(destination);
    bool copySucceeded = sourceFile.open(QIODevice::ReadOnly)
            && destinationFile.open(QIODevice::WriteOnly);
    QByteArray buffer;
    while (copySucceeded && !sourceFile.atEnd())
    {
        buffer = sourceFile.read(4 * 1024 * 1024);
        if (buffer.isEmpty() && sourceFile.error() != QFileDevice::NoError)
        {
            copySucceeded = false;
            break;
        }

        if (!buffer.isEmpty() && destinationFile.write(buffer) != buffer.size())
            copySucceeded = false;
    }

    sourceFile.close();
    if (copySucceeded)
        copySucceeded = destinationFile.commit();
    else
        destinationFile.cancelWriting();

    if (copySucceeded)
    {
        QFile::remove(source);
        return destination;
    }

    const QString saveError = tr("无法将视频保存到所选位置。原有文件没有被修改，录制视频仍保存在：\n%1")
            .arg(source);
    qWarning() << saveError;
    UBApplication::showMessage(saveError, false);
    if (!mApplicationIsClosing)
    {
        QMessageBox saveErrorDialog(QMessageBox::Warning,
                tr("保存录制视频"), saveError, QMessageBox::Ok, dialogParent);
        saveErrorDialog.setTextFormat(Qt::PlainText);
        execCenteredDialog(&saveErrorDialog);
    }
    return source;
}


void UBPodcastController::sendLatestPixmapToEncoder()
{
    if (mVideoEncoder)
    {
        // QWidget/QGraphicsScene rendering does not include the operating-system
        // cursor. Composite it onto a copy so it appears in the video without
        // leaving cursor trails in mLatestCapture.
        QImage frame = mLatestCapture.copy();
        const QPoint globalCursorPos = QCursor::pos();
        QPoint sourceCursorPos;

        if (mIsDesktopMode)
        {
            sourceCursorPos = globalCursorPos - currentDesktopCaptureRect().topLeft();
        }
        else if (mSourceWidget)
        {
            sourceCursorPos = mSourceWidget->mapFromGlobal(globalCursorPos);
        }

        const QSize sourceSize = mIsDesktopMode
                ? currentDesktopCaptureRect().size()
                : (mSourceWidget ? mSourceWidget->size() : QSize());

        if (QRect(QPoint(0, 0), sourceSize).contains(sourceCursorPos))
        {
            QWidget* cursorWidget = QApplication::widgetAt(globalCursorPos);
            QCursor cursor = QApplication::overrideCursor()
                    ? *QApplication::overrideCursor()
                    : (cursorWidget ? cursorWidget->cursor() : QCursor(Qt::ArrowCursor));

            if (cursor.shape() != Qt::BlankCursor)
            {
                QPainter cursorPainter(&frame);
                cursorPainter.setRenderHint(QPainter::Antialiasing, true);
                cursorPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                cursorPainter.setTransform(mViewToVideoTransform);

                const QPixmap cursorPixmap = cursor.pixmap();
                if (!cursorPixmap.isNull())
                {
                    cursorPainter.drawPixmap(sourceCursorPos - cursor.hotSpot(), cursorPixmap);
                }
                else
                {
                    // Native system cursors do not expose a pixmap in Qt. Draw a
                    // familiar high-contrast arrow as a portable fallback.
                    QPolygon arrow;
                    arrow << sourceCursorPos
                          << sourceCursorPos + QPoint(0, 22)
                          << sourceCursorPos + QPoint(6, 16)
                          << sourceCursorPos + QPoint(11, 27)
                          << sourceCursorPos + QPoint(16, 25)
                          << sourceCursorPos + QPoint(11, 14)
                          << sourceCursorPos + QPoint(20, 14);
                    cursorPainter.setPen(QPen(Qt::black, 2));
                    cursorPainter.setBrush(Qt::white);
                    cursorPainter.drawPolygon(arrow);
                }
            }
        }

        compositeCameraPreview(frame);
        mVideoEncoder->newPixmap(frame, elapsedRecordingMs());
    }

    mEmptyChapter = false;
}

void UBPodcastController::timerEvent(QTimerEvent *event)
{
    if (mRecordingState == Recording)
    {
        if (event->timerId() == mScreenGrabingTimerEventID)
        {
            if (mSourceScene && !mIsDesktopMode)
                sendLatestPixmapToEncoder();
            else
                processScreenGrabingTimerEvent();
        }
        else if (event->timerId() == mRecordingProgressTimerEventID)
        {
            emit recordingProgressChanged(elapsedRecordingMs());
        }
    }
}

void UBPodcastController::processScreenGrabingTimerEvent()
{
    if (mRecordingState != Recording)
        return;

    QPixmap widgetContent;

    if (mIsDesktopMode)
    {
        // A captured application window can be moved while recording. Follow
        // its current top-right corner so the visible and encoded previews
        // remain aligned.
        if (mCameraPreview && mCameraPreview->isVisible())
            positionCameraPreview();

        widgetContent = grabDesktopCapture();
        if (widgetContent.isNull())
        {
            captureFrameUnavailable();
            return;
        }
    }
    else
    {
        if (!mSourceWidget || mSourceWidget->size().isEmpty())
        {
            captureFrameUnavailable();
            return;
        }
        // render web view
        widgetContent = QPixmap(mSourceWidget->size());
        if (widgetContent.isNull())
        {
            captureFrameUnavailable();
            return;
        }
        QPainter p(&widgetContent);
        mSourceWidget->render(&p);
    }

    mCaptureFailureStartedAt = -1;
    QPainter p(&mLatestCapture);

    if (!mInitialized)
    {
        mLatestCapture.fill(sBackgroundColor);
        mInitialized = true;
    }

    QRectF targetRect = mViewToVideoTransform.mapRect(QRectF(0, 0, widgetContent.width(), widgetContent.height()));

    p.setRenderHints(QPainter::Antialiasing);
    p.setRenderHints(QPainter::SmoothPixmapTransform);
    p.drawPixmap(targetRect.left(), targetRect.top(), widgetContent.scaled(targetRect.width(), targetRect.height(),  Qt::KeepAspectRatio, Qt::SmoothTransformation));

    p.end();
    sendLatestPixmapToEncoder();
}


void UBPodcastController::captureFrameUnavailable()
{
    const qint64 now = mMonotonicTimer.elapsed();
    if (mCaptureFailureStartedAt < 0)
    {
        mCaptureFailureStartedAt = now;
        qWarning() << "Recording frame could not be captured; waiting for source recovery";
    }
    else if (now - mCaptureFailureStartedAt >= 3000)
    {
        reportRecordingFailure(tr("连续 3 秒无法获取录制画面。请检查录制窗口是否已关闭或最小化。"));
    }
}


QStringList UBPodcastController::audioRecordingDevices()
{
    // The FFmpeg encoder records through Qt Multimedia on every supported
    // platform. Enumerating through the same API ensures that a selected name
    // can be resolved by the encoder instead of silently falling back to the
    // default microphone on Windows.
    return UBMicrophoneInput::availableDevicesNames();
}

QStringList UBPodcastController::audioOutputDevices() const
{
    QStringList devices;
    for (const QAudioDevice &device : QMediaDevices::audioOutputs())
    {
        if (!device.description().isEmpty()
                && !devices.contains(device.description()))
        {
            devices << device.description();
        }
    }
    return devices;
}

QStringList UBPodcastController::cameraDevices() const
{
    QStringList devices;
    for (const QCameraDevice &device : QMediaDevices::videoInputs())
    {
        if (!device.description().isEmpty()
                && !devices.contains(device.description()))
        {
            devices << device.description();
        }
    }
    return devices;
}

QString UBPodcastController::selectedAudioInputDevice() const
{
    return UBSettings::settings()->podcastAudioRecordingDevice->get().toString();
}

QString UBPodcastController::selectedAudioOutputDevice() const
{
    return UBSettings::settings()->podcastAudioOutputDevice->get().toString();
}

QString UBPodcastController::selectedCameraDevice() const
{
    return UBSettings::settings()->podcastCameraDevice->get().toString();
}

void UBPodcastController::selectAudioInputDevice(const QString &deviceName)
{
    UBSettings::settings()->podcastAudioRecordingDevice->set(
            deviceName.isEmpty() ? QStringLiteral("Default") : deviceName);
}

void UBPodcastController::selectAudioOutputDevice(const QString &deviceName)
{
    UBSettings::settings()->podcastAudioOutputDevice->set(
            deviceName.isEmpty() ? QStringLiteral("Default") : deviceName);
}

void UBPodcastController::selectCameraDevice(const QString &deviceName)
{
    const QString selection = deviceName.isEmpty()
            ? QStringLiteral("Default") : deviceName;
    UBSettings::settings()->podcastCameraDevice->set(selection);

    if (mCameraPreview)
    {
        mCameraPreview->setCameraDeviceName(selection == QStringLiteral("Default")
                ? QString() : selection);
    }
}


void UBPodcastController::recordToggled(bool record)
{
    if ((mRecordingState == Stopped) && record)
        start();
    else
        stop();
}

void UBPodcastController::pauseToggled(bool paused)
{
    if ((mRecordingState == Recording) && paused)
        pause();
    else
        unpause();
}


void UBPodcastController::toggleRecordingPalette(bool visible)
{
    if(!mRecordingPalette)
    {
        // A parentless UBFloatingPalette is a frameless, always-on-top tool
        // window.  This is intentional: the main window is hidden while the
        // desktop annotation layer is active.
        mRecordingPalette = new UBPodcastRecordingPalette(0);

        // The palette is top-level and therefore is not owned by the main
        // window.  Delete it while the main-window actions it references are
        // still alive, and clear the pointer before this controller is later
        // destroyed by the static-memory cleaner.
        connect(UBApplication::mainWindow, &QObject::destroyed, this, [this]() {
            delete mCameraPreview;
            mCameraPreview = 0;
            delete mRecordingPalette;
            mRecordingPalette = 0;
        });

        mRecordingPalette->adjustSizeAndPosition();
        mRecordingPalette->setCustomPosition(true);
        positionRecordingPalette(mIsDesktopMode);

        connect(UBApplication::mainWindow->actionPodcastRecord, SIGNAL(triggered(bool))
             , this, SLOT(recordToggled(bool)));

        connect(UBApplication::mainWindow->actionPodcastPause, SIGNAL(toggled(bool))
             , this, SLOT(pauseToggled(bool)));

        connect(this, SIGNAL(recordingStateChanged(UBPodcastController::RecordingState))
                , mRecordingPalette, SLOT(recordingStateChanged(UBPodcastController::RecordingState)));
        connect(this, SIGNAL(recordingProgressChanged(qint64))
                , mRecordingPalette, SLOT(recordingProgressChanged(qint64)));
        connect(mRecordingPalette, &UBPodcastRecordingPalette::cameraToggled,
                this, &UBPodcastController::cameraToggled);
    }

    mRecordingPalette->setVisible(visible);
    if (visible)
        mRecordingPalette->raise();
}

void UBPodcastController::positionRecordingPalette(bool desktopMode)
{
    if (!mRecordingPalette)
        return;

    if (desktopMode)
    {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;

        const QRect available = screen->availableGeometry();
        const int left = available.center().x() - mRecordingPalette->width() / 2;
        const int top = available.bottom() - mRecordingPalette->height() - 28;
        mRecordingPalette->move(left, top);
        return;
    }

    QWidget *controlView = UBApplication::boardController->controlView();
    const int left = controlView->width() * 0.75 - mRecordingPalette->width() / 2;
    const int top = controlView->height() - mRecordingPalette->height() - UBSettings::boardMargin;
    mRecordingPalette->move(controlView->mapToGlobal(QPoint(left, top)));
}


void UBPodcastController::setRecordingState(RecordingState pRecordingState)
{
    if(mRecordingState != pRecordingState)
    {
        mRecordingState = pRecordingState;
        emit recordingStateChanged(mRecordingState);
    }
}


QList<QAction*> UBPodcastController::audioRecordingDevicesActions()
{
    if (mAudioInputDevicesActions.length() == 0)
    {
        QString settingsDevice = UBSettings::settings()->podcastAudioRecordingDevice->get().toString();

        mDefaultAudioInputDeviceAction = new QAction(QStringLiteral("默认麦克风"), this);
        mDefaultAudioInputDeviceAction->setData(QStringLiteral("Default"));
        QAction *checkedAction = mDefaultAudioInputDeviceAction;

        mNoAudioInputDeviceAction = new QAction(QStringLiteral("不使用麦克风"), this);
        mNoAudioInputDeviceAction->setData(QStringLiteral("None"));

        if (settingsDevice == "None")
            checkedAction = mNoAudioInputDeviceAction;

        mAudioInputDevicesActions << mNoAudioInputDeviceAction;
        mAudioInputDevicesActions << mDefaultAudioInputDeviceAction;

        foreach(QString audioDevice, audioRecordingDevices())
        {
            QAction* act = new QAction(audioDevice, this);
            act->setData(audioDevice);
            act->setCheckable(true);
            mAudioInputDevicesActions << act;
            if (settingsDevice == audioDevice)
                checkedAction = act;
        }

        QActionGroup* audioInputActionGroup = new QActionGroup(this);
        audioInputActionGroup->setExclusive(true);

        foreach(QAction* action, mAudioInputDevicesActions)
        {
            audioInputActionGroup->addAction(action);
            action->setCheckable(true);
        }
        checkedAction->setChecked(true);

        connect(audioInputActionGroup, SIGNAL(triggered(QAction*)), this, SLOT(groupActionTriggered(QAction*)));
    }

    return mAudioInputDevicesActions;

}


QList<QAction*> UBPodcastController::videoSizeActions()
{
    if (mVideoSizesActions.length() == 0)
    {
        mSmallVideoSizeAction = new QAction(
                QStringLiteral("流畅（最高 640×480）"), this);
        mMediumVideoSizeAction = new QAction(
                QStringLiteral("标准（最高 1024×768）"), this);
        mFullVideoSizeAction = new QAction(
                QStringLiteral("高清（原始分辨率）"), this);

        mVideoSizesActions << mSmallVideoSizeAction;
        mVideoSizesActions << mMediumVideoSizeAction;
        mVideoSizesActions << mFullVideoSizeAction;

        QActionGroup* videoSizeActionGroup = new QActionGroup(this);
        videoSizeActionGroup->setExclusive(true);

        foreach(QAction* videoSizeAction, mVideoSizesActions)
        {
            videoSizeAction->setCheckable(true);
            videoSizeActionGroup->addAction(videoSizeAction);
        }

        QString videoSize = UBSettings::settings()->podcastVideoSize->get().toString();

        if (videoSize == "Small")
            mSmallVideoSizeAction->setChecked(true);
        else if (videoSize == "Full")
            mFullVideoSizeAction->setChecked(true);
        else
            mMediumVideoSizeAction->setChecked(true);

        connect(videoSizeActionGroup, SIGNAL(triggered(QAction*)), this, SLOT(groupActionTriggered(QAction*)));
    }

    return mVideoSizesActions;
}

QRect UBPodcastController::cameraPreviewSourceRect() const
{
    if (mIsDesktopMode)
    {
        if (mDesktopCapturePrepared && !currentDesktopCaptureRect().isEmpty())
            return currentDesktopCaptureRect();

        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = UBApplication::displayManager->screen(ScreenRole::Desktop);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        return screen ? screen->availableGeometry() : QRect();
    }

    if (mSourceWidget)
        return QRect(mSourceWidget->mapToGlobal(QPoint(0, 0)),
                mSourceWidget->size());

    return UBApplication::mainWindow
            ? UBApplication::mainWindow->geometry() : QRect();
}

void UBPodcastController::positionCameraPreview()
{
    if (!mCameraPreview)
        return;

    const QRect sourceRect = cameraPreviewSourceRect();
    if (sourceRect.isEmpty())
        return;

    mCameraPreview->setInteractionBounds(sourceRect);

    // Once the presenter has moved or resized the preview, preserve that
    // choice. setInteractionBounds() still keeps it inside the recorded area.
    if (mCameraPreview->hasUserAdjustedGeometry())
        return;

    const int previewWidth = qBound(180,
            qRound(sourceRect.width() * 0.14), 280);
    const int previewHeight = qRound(previewWidth * 0.625);
    const int margin = qBound(16, sourceRect.width() / 80, 28);

    const QSize desiredSize(previewWidth, previewHeight);
    const QPoint desiredPosition(sourceRect.right() - previewWidth - margin + 1,
            sourceRect.top() + margin);
    if (mCameraPreview->size() != desiredSize)
        mCameraPreview->resize(desiredSize);
    if (mCameraPreview->pos() != desiredPosition)
        mCameraPreview->move(desiredPosition);
}

void UBPodcastController::cameraToggled(bool enabled)
{
    if (!mRecordingPalette)
        return;

    if (!mCameraPreview)
    {
        mCameraPreview = new UBCameraPreviewWindow(nullptr);
        connect(mCameraPreview, &UBCameraPreviewWindow::cameraUnavailable,
                this, [this](const QString &message) {
            if (mRecordingPalette)
                mRecordingPalette->setCameraChecked(false);
            QMessageBox::warning(mRecordingPalette,
                    QStringLiteral("摄像头"), message);
        });
    }

    if (!enabled)
    {
        mCameraPreview->stopCamera();
        return;
    }

    const QString selectedDevice = selectedCameraDevice();
    mCameraPreview->setCameraDeviceName(
            selectedDevice == QStringLiteral("Default")
                    ? QString() : selectedDevice);
    positionCameraPreview();
    if (!mCameraPreview->startCamera())
        mRecordingPalette->setCameraChecked(false);
}

void UBPodcastController::compositeCameraPreview(QImage &frame)
{
    if (!mCameraPreview || !mCameraPreview->isVisible()
            || !mCameraPreview->hasFrame() || frame.isNull())
    {
        return;
    }

    const QRect sourceRect = cameraPreviewSourceRect();
    if (sourceRect.isEmpty())
        return;

    const QRect previewGlobalRect(mCameraPreview->pos(), mCameraPreview->size());
    if (!sourceRect.intersects(previewGlobalRect))
        return;

    const QRect previewInSource = previewGlobalRect.translated(
            -sourceRect.topLeft());
    const QImage preview = mCameraPreview->renderedPreview();
    if (preview.isNull())
        return;

    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setTransform(mViewToVideoTransform);
    painter.drawImage(previewInSource, preview);
}

QList<QAction*> UBPodcastController::desktopCaptureModeActions()
{
    if (mDesktopCaptureModeActions.isEmpty())
    {
        mFullScreenCaptureAction = new QAction(QStringLiteral("全屏"), this);
        mAreaCaptureAction = new QAction(QStringLiteral("选区"), this);
        mApplicationWindowCaptureAction = new QAction(QStringLiteral("窗口"), this);

        mFullScreenCaptureAction->setData(static_cast<int>(FullScreenCapture));
        mAreaCaptureAction->setData(static_cast<int>(AreaCapture));
        mApplicationWindowCaptureAction->setData(static_cast<int>(ApplicationWindowCapture));

        mDesktopCaptureModeActions << mFullScreenCaptureAction
                                   << mAreaCaptureAction
                                   << mApplicationWindowCaptureAction;

        QActionGroup *captureModeGroup = new QActionGroup(this);
        captureModeGroup->setExclusive(true);
        foreach (QAction *captureAction, mDesktopCaptureModeActions)
        {
            captureAction->setCheckable(true);
            captureModeGroup->addAction(captureAction);
        }
        mFullScreenCaptureAction->setChecked(true);

        connect(captureModeGroup, SIGNAL(triggered(QAction*)),
                this, SLOT(desktopCaptureModeTriggered(QAction*)));
    }

    return mDesktopCaptureModeActions;
}

void UBPodcastController::desktopCaptureModeTriggered(QAction *action)
{
    if (!action)
        return;

    setDesktopCaptureMode(
            static_cast<DesktopCaptureMode>(action->data().toInt()));
}

void UBPodcastController::setDesktopCaptureMode(DesktopCaptureMode mode)
{
    mDesktopCaptureMode = mode;
    mDesktopCapturePrepared = false;

    foreach (QAction *captureAction, desktopCaptureModeActions())
    {
        const bool selected = captureAction->data().toInt()
                == static_cast<int>(mode);
        if (captureAction->isChecked() != selected)
        {
            QSignalBlocker blocker(captureAction);
            captureAction->setChecked(selected);
        }
    }
}

bool UBPodcastController::prepareDesktopCapture()
{
    mDesktopCapturePrepared = false;
    mDesktopCaptureWindowId = 0;
    mDesktopCaptureWindowTitle.clear();

    QScreen *desktopScreen = UBApplication::displayManager->screen(ScreenRole::Desktop);
    if (!desktopScreen)
    {
        QMessageBox::warning(mRecordingPalette, QStringLiteral("屏幕录制"),
                QStringLiteral("没有找到可录制的屏幕。"));
        return false;
    }

    if (mDesktopCaptureMode == FullScreenCapture)
    {
        mDesktopCaptureRect = desktopScreen->geometry();
        mDesktopCapturePrepared = true;
        return true;
    }

    if (mDesktopCaptureMode == AreaCapture)
    {
        const QPixmap screenPixmap = UBApplication::displayManager->grab(ScreenRole::Desktop);
        if (screenPixmap.isNull())
        {
            QMessageBox::warning(mRecordingPalette, QStringLiteral("选区录制"),
                    QStringLiteral("无法获取屏幕画面，请重试。"));
            return false;
        }

        UBCustomCaptureWindow captureWindow(nullptr);
        if (captureWindow.execute(screenPixmap) != QDialog::Accepted)
            return false;

        const QRect selectedRect = captureWindow.selectedRect();
        if (selectedRect.width() < 16 || selectedRect.height() < 16)
            return false;

        mDesktopCaptureRect = QRect(desktopScreen->geometry().topLeft()
                + selectedRect.topLeft(), selectedRect.size());
        mDesktopCapturePrepared = true;
        return true;
    }

#ifdef Q_OS_WIN
    const QList<CaptureWindowInfo> windows = availableCaptureWindows();
    if (windows.isEmpty())
    {
        QMessageBox::information(mRecordingPalette, QStringLiteral("应用窗口录制"),
                QStringLiteral("没有找到可录制的应用窗口。请先打开目标应用。"));
        return false;
    }

    QStringList labels;
    for (int index = 0; index < windows.size(); ++index)
    {
        const CaptureWindowInfo &window = windows.at(index);
        labels << QStringLiteral("%1. %2（%3 × %4）")
                .arg(index + 1).arg(window.title)
                .arg(window.geometry.width()).arg(window.geometry.height());
    }

    bool accepted = false;
    const QString selected = QInputDialog::getItem(mRecordingPalette,
            QStringLiteral("选择应用窗口"),
            QStringLiteral("请选择要录制的应用窗口（录制期间请勿最小化）："),
            labels, 0, false, &accepted,
            Qt::Dialog | Qt::WindowStaysOnTopHint);
    if (!accepted)
        return false;

    const int selectedIndex = labels.indexOf(selected);
    if (selectedIndex < 0)
        return false;

    const CaptureWindowInfo &selectedWindow = windows.at(selectedIndex);
    mDesktopCaptureWindowId = reinterpret_cast<quintptr>(selectedWindow.handle);
    mDesktopCaptureWindowTitle = selectedWindow.title;
    mDesktopCaptureRect = nativeWindowGeometry(mDesktopCaptureWindowId);
    mDesktopCapturePrepared = !mDesktopCaptureRect.isEmpty();
    if (!mDesktopCapturePrepared)
    {
        QMessageBox::warning(mRecordingPalette, QStringLiteral("应用窗口录制"),
                QStringLiteral("无法读取所选应用窗口的位置，请重新选择。"));
        return false;
    }
    return true;
#else
    QMessageBox::information(mRecordingPalette, QStringLiteral("应用窗口录制"),
            QStringLiteral("当前系统暂不支持应用窗口录制，请使用全屏或选区录制。"));
    return false;
#endif
}

QRect UBPodcastController::currentDesktopCaptureRect() const
{
#ifdef Q_OS_WIN
    if (mDesktopCaptureMode == ApplicationWindowCapture && mDesktopCaptureWindowId)
    {
        const QRect currentGeometry = nativeWindowGeometry(mDesktopCaptureWindowId);
        if (!currentGeometry.isEmpty())
            return currentGeometry;
    }
#endif
    return mDesktopCaptureRect;
}

QPixmap UBPodcastController::grabDesktopCapture() const
{
#ifdef Q_OS_WIN
    if (mDesktopCaptureMode == ApplicationWindowCapture && mDesktopCaptureWindowId)
    {
        HWND window = reinterpret_cast<HWND>(mDesktopCaptureWindowId);
        if (!IsWindow(window) || IsIconic(window))
            return QPixmap();

        const QRect windowRect = currentDesktopCaptureRect();
        QScreen *screen = QGuiApplication::screenAt(windowRect.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return QPixmap();

        // PrintWindow asks the target HWND to render into an off-screen GDI
        // bitmap.  Unlike QScreen::grabWindow(), this does not copy pixels
        // from the target's on-screen rectangle, so overlapping windows and
        // desktop content cannot leak into the recording.
        QPixmap windowContent = captureNativeWindow(window);
        if (windowContent.isNull())
            return QPixmap();

        windowContent.setDevicePixelRatio(screen->devicePixelRatio());

        // Desktop ink lives in OpenBoard's transparent top-level window and
        // is therefore not part of the target application's native surface.
        // Render just that annotation layer over the captured window.
        UBDesktopAnnotationController *desktopController =
                UBApplication::applicationController->uninotesController();
        if (desktopController)
        {
            const QPixmap annotations = desktopController->grabAnnotations(
                    windowRect, windowContent.devicePixelRatio());
            if (!annotations.isNull())
            {
                QPainter painter(&windowContent);
                painter.drawPixmap(QPointF(0, 0), annotations);
            }
        }
        return windowContent;
    }
#endif
    return UBApplication::displayManager->grabGlobal(currentDesktopCaptureRect());
}
