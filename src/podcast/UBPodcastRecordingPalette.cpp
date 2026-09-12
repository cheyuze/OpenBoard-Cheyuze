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




#include "UBPodcastRecordingPalette.h"

#include "UBPodcastController.h"

#include "core/UBApplication.h"

#include "gui/UBResources.h"

#include "core/UBSettings.h"

#include "gui/UBMainWindow.h"

#include "core/memcheck.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
    enum class RecordingDeviceIcon
    {
        Microphone,
        Speaker,
        Camera
    };

    QIcon recordingDeviceIcon(RecordingDeviceIcon type)
    {
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(241, 245, 249), 2.8,
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        if (type == RecordingDeviceIcon::Microphone)
        {
            painter.drawRoundedRect(QRectF(17, 7, 14, 24), 7, 7);
            painter.drawArc(QRectF(11, 19, 26, 20), 180 * 16, 180 * 16);
            painter.drawLine(QPointF(24, 39), QPointF(24, 44));
            painter.drawLine(QPointF(17, 44), QPointF(31, 44));
        }
        else if (type == RecordingDeviceIcon::Speaker)
        {
            QPainterPath body;
            body.moveTo(7, 20);
            body.lineTo(15, 20);
            body.lineTo(25, 11);
            body.lineTo(25, 37);
            body.lineTo(15, 28);
            body.lineTo(7, 28);
            body.closeSubpath();
            painter.drawPath(body);
            painter.drawArc(QRectF(24, 15, 13, 18), -70 * 16, 140 * 16);
            painter.drawArc(QRectF(23, 10, 21, 28), -65 * 16, 130 * 16);
        }
        else
        {
            painter.drawRoundedRect(QRectF(8.5, 14.5, 25, 20), 4, 4);
            painter.drawEllipse(QPointF(21, 24.5), 5.5, 5.5);

            QPainterPath lens;
            lens.moveTo(34, 20);
            lens.lineTo(41, 16.5);
            lens.lineTo(41, 32.5);
            lens.lineTo(34, 29);
            lens.closeSubpath();
            painter.drawPath(lens);
        }
        return QIcon(pixmap);
    }
}

UBPodcastRecordingPalette::UBPodcastRecordingPalette(QWidget *parent)
     : UBActionPalette(Qt::Horizontal, parent)
     , mTimerLabel(nullptr)
     , mLevelMeter(nullptr)
     , mCameraAction(nullptr)
     , mCameraEnabledAction(nullptr)
     , mMicrophoneMenu(nullptr)
     , mSpeakerMenu(nullptr)
     , mCameraMenu(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoMousePropagation, true);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setWindowTitle(QStringLiteral("录制控制"));
    setGrip(false);
    setWindowOpacity(0.98);

    QLayout *paletteLayout = layout();
    paletteLayout->setContentsMargins(12, 9, 12, 9);
    paletteLayout->setSpacing(6);

    QAction *recordAction = UBApplication::mainWindow->actionPodcastRecord;
    QAction *pauseAction = UBApplication::mainWindow->actionPodcastPause;
    QAction *configAction = UBApplication::mainWindow->actionPodcastConfig;

    recordAction->setIcon(QIcon(":/images/podcast/record.svg"));
    recordAction->setText(QStringLiteral("录制"));
    recordAction->setToolTip(QStringLiteral("录制"));
    pauseAction->setIcon(QIcon(":/images/podcast/pause.svg"));
    pauseAction->setText(QStringLiteral("暂停"));
    pauseAction->setToolTip(QStringLiteral("暂停"));
    configAction->setIcon(QIcon(":/images/podcast/settings.svg"));
    configAction->setToolTip(QStringLiteral("录制设置"));

    addAction(recordAction);
    addAction(pauseAction);

    mTimerLabel = new QLabel(this);
    mTimerLabel->setAlignment(Qt::AlignCenter);
    mTimerLabel->setMinimumWidth(56);
    mTimerLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #F8FAFC; font-size: 15px; font-weight: 600; "
        "font-family: 'Segoe UI'; background: transparent; border: none; "
        "padding: 0 4px; }"));
    recordingProgressChanged(0);

    layout()->addWidget(mTimerLabel);

    mLevelMeter = new UBVuMeter(this);
    mLevelMeter->setFixedSize(5, 26);

    layout()->addWidget(mLevelMeter);

    mCameraAction = new QAction(recordingDeviceIcon(RecordingDeviceIcon::Camera),
            QStringLiteral("摄像头"), this);
    mCameraAction->setCheckable(true);
    mCameraAction->setToolTip(QStringLiteral("开启摄像头"));
    connect(mCameraAction, &QAction::toggled,
            this, &UBPodcastRecordingPalette::cameraToggled);
    addAction(mCameraAction);

    addAction(configAction);

    const QString buttonStyle = QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent; "
        "border-radius: 10px; padding: 6px; }"
        "QToolButton:hover { background: rgba(255, 255, 255, 32); "
        "border-color: rgba(255, 255, 255, 38); }"
        "QToolButton:pressed, QToolButton:checked { background: rgba(59, 130, 246, 58); "
        "border-color: rgba(96, 165, 250, 105); }"
        "QToolButton:disabled { background: transparent; border-color: transparent; }"
        "QToolButton::menu-indicator { image: none; }"
    );

    for (QAction *action : {recordAction, pauseAction,
                            mCameraAction, configAction})
    {
        if (QToolButton *button = getButtonFromAction(action))
        {
            button->setFixedSize(40, 40);
            button->setIconSize(QSize(24, 24));
            button->setStyleSheet(buttonStyle);
        }
    }

    mMicrophoneMenu = new QMenu(QStringLiteral("麦克风"), this);
    mMicrophoneMenu->setIcon(recordingDeviceIcon(RecordingDeviceIcon::Microphone));
    connect(mMicrophoneMenu, &QMenu::aboutToShow,
            this, &UBPodcastRecordingPalette::populateMicrophoneMenu);

    mSpeakerMenu = new QMenu(QStringLiteral("扬声器"), this);
    mSpeakerMenu->setIcon(recordingDeviceIcon(RecordingDeviceIcon::Speaker));
    connect(mSpeakerMenu, &QMenu::aboutToShow,
            this, &UBPodcastRecordingPalette::populateSpeakerMenu);

    mCameraMenu = new QMenu(QStringLiteral("摄像头"), this);
    mCameraMenu->setIcon(recordingDeviceIcon(RecordingDeviceIcon::Camera));
    connect(mCameraMenu, &QMenu::aboutToShow,
            this, &UBPodcastRecordingPalette::populateCameraMenu);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    foreach(QObject* menuWidget, configAction->associatedObjects())
#else
    foreach(QWidget* menuWidget, configAction->associatedWidgets())
#endif
    {
        QToolButton *tb = qobject_cast<QToolButton*>(menuWidget);

        if (tb && !tb->menu())
        {
            tb->setIconSize(QSize(24, 24));
            tb->setObjectName("ubButtonMenu");
            tb->setPopupMode(QToolButton::InstantPopup);
            QMenu* menu = new QMenu(this);

            foreach(QAction* videoSizeAction, UBPodcastController::instance()->videoSizeActions())
            {
                menu->addAction(videoSizeAction);
            }

            menu->addSeparator();
            menu->addMenu(mMicrophoneMenu);
            menu->addMenu(mSpeakerMenu);
            menu->addMenu(mCameraMenu);

            tb->setMenu(menu);
        }
    }

    adjustSize();
    ensureSystemTopMost();
}


UBPodcastRecordingPalette::~UBPodcastRecordingPalette()
{
    // NOOP
}


void UBPodcastRecordingPalette::recordingStateChanged(UBPodcastController::RecordingState state)
{
    QAction *recordAction = UBApplication::mainWindow->actionPodcastRecord;
    QAction *pauseAction = UBApplication::mainWindow->actionPodcastPause;

    if (state == UBPodcastController::Recording)
    {
        recordAction->setChecked(true);
        recordAction->setEnabled(true);
        recordAction->setIcon(QIcon(":/images/podcast/stop.svg"));
        recordAction->setText(QStringLiteral("停止"));
        recordAction->setToolTip(QStringLiteral("停止"));

        pauseAction->setChecked(false);
        pauseAction->setEnabled(true);
        pauseAction->setIcon(QIcon(":/images/podcast/pause.svg"));
        pauseAction->setText(QStringLiteral("暂停"));
        pauseAction->setToolTip(QStringLiteral("暂停"));

        //UBApplication::mainWindow->actionPodcastMic->setEnabled(false);

        UBApplication::mainWindow->actionPodcastConfig->setEnabled(false);
    }
    else if (state == UBPodcastController::Stopped)
    {
        recordAction->setChecked(false);
        recordAction->setEnabled(true);
        recordAction->setIcon(QIcon(":/images/podcast/record.svg"));
        recordAction->setText(QStringLiteral("录制"));
        recordAction->setToolTip(QStringLiteral("录制"));

        pauseAction->setChecked(false);
        pauseAction->setEnabled(false);
        pauseAction->setIcon(QIcon(":/images/podcast/pause.svg"));
        pauseAction->setText(QStringLiteral("暂停"));
        pauseAction->setToolTip(QStringLiteral("暂停"));

        //UBApplication::mainWindow->actionPodcastMic->setEnabled(true);
        UBApplication::mainWindow->actionPodcastConfig->setEnabled(true);
    }
    else if (state == UBPodcastController::Paused)
    {
        recordAction->setChecked(true);
        recordAction->setEnabled(true);
        recordAction->setIcon(QIcon(":/images/podcast/stop.svg"));
        recordAction->setText(QStringLiteral("停止"));
        recordAction->setToolTip(QStringLiteral("停止"));

        pauseAction->setChecked(true);
        pauseAction->setEnabled(true);
        pauseAction->setIcon(QIcon(":/images/podcast/play.svg"));
        pauseAction->setText(QStringLiteral("播放"));
        pauseAction->setToolTip(QStringLiteral("播放"));

        //UBApplication::mainWindow->actionPodcastMic->setEnabled(false);
        UBApplication::mainWindow->actionPodcastConfig->setEnabled(false);
    }
    else
    {
        recordAction->setIcon(QIcon(":/images/podcast/stop.svg"));
        recordAction->setText(QStringLiteral("停止"));
        recordAction->setToolTip(QStringLiteral("停止"));
        recordAction->setEnabled(false);
        pauseAction->setEnabled(false);
        UBApplication::mainWindow->actionPodcastConfig->setEnabled(false);
    }

    ensureSystemTopMost();
}

void UBPodcastRecordingPalette::recordingProgressChanged(qint64 ms)
{
    const qint64 min = qMax<qint64>(0, ms) / 60000;
    const qint64 seconds = (qMax<qint64>(0, ms) / 1000) % 60;

    mTimerLabel->setText(QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0')));
}


void UBPodcastRecordingPalette::audioLevelChanged(quint8 level)
{
    mLevelMeter->setVolume(level);
}


UBVuMeter::UBVuMeter(QWidget* pParent)
    : QWidget(pParent)
    , mVolume(0)
{
    // NOOP
}


UBVuMeter::~UBVuMeter()
{
    // NOOP
}

void UBVuMeter::setVolume(quint8 pVolume)
{
    if (mVolume != pVolume)
    {
        mVolume = pVolume;
        update();
    }
}


void UBVuMeter::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track(0, 0, width(), height());
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(71, 85, 105));
    painter.drawRoundedRect(track, width() / 2.0, width() / 2.0);

    const qreal levelHeight = track.height() * mVolume / 255.0;
    if (levelHeight > 0.5)
    {
        QRectF levelRect(0, track.bottom() - levelHeight + 1, width(), levelHeight);
        QColor levelColor = mVolume > 220 ? QColor(248, 113, 113) : QColor(52, 211, 153);
        painter.setBrush(levelColor);
        painter.drawRoundedRect(levelRect, width() / 2.0, width() / 2.0);
    }
}

void UBPodcastRecordingPalette::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient background(panel.topLeft(), panel.bottomLeft());
    background.setColorAt(0.0, QColor(30, 41, 59, 248));
    background.setColorAt(1.0, QColor(15, 23, 42, 248));
    painter.setBrush(background);
    painter.setPen(QPen(QColor(148, 163, 184, 95), 1.0));
    painter.drawRoundedRect(panel, radius(), radius());
}

int UBPodcastRecordingPalette::radius()
{
    return 15;
}

void UBPodcastRecordingPalette::setCameraChecked(bool checked)
{
    if (mCameraAction && mCameraAction->isChecked() != checked)
    {
        QSignalBlocker blocker(mCameraAction);
        mCameraAction->setChecked(checked);
    }
    if (mCameraEnabledAction && mCameraEnabledAction->isChecked() != checked)
    {
        QSignalBlocker blocker(mCameraEnabledAction);
        mCameraEnabledAction->setChecked(checked);
    }
    if (mCameraAction)
    {
        mCameraAction->setToolTip(checked
                ? QStringLiteral("关闭摄像头")
                : QStringLiteral("开启摄像头"));
    }
}

void UBPodcastRecordingPalette::populateMicrophoneMenu()
{
    mMicrophoneMenu->clear();
    UBPodcastController *controller = UBPodcastController::instance();
    const QString selected = controller->selectedAudioInputDevice();

    auto addChoice = [this, controller, &selected](const QString &label,
            const QString &value) {
        QAction *action = mMicrophoneMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(selected == value);
        connect(action, &QAction::triggered, this,
                [controller, value]() { controller->selectAudioInputDevice(value); });
    };

    addChoice(QStringLiteral("不使用麦克风"), QStringLiteral("None"));
    addChoice(QStringLiteral("默认麦克风"), QStringLiteral("Default"));
    mMicrophoneMenu->addSeparator();
    const QStringList devices = controller->audioRecordingDevices();
    for (const QString &device : devices)
        addChoice(device, device);
    if (devices.isEmpty())
    {
        QAction *empty = mMicrophoneMenu->addAction(QStringLiteral("未检测到麦克风"));
        empty->setEnabled(false);
    }
}

void UBPodcastRecordingPalette::populateSpeakerMenu()
{
    mSpeakerMenu->clear();
    UBPodcastController *controller = UBPodcastController::instance();
    const QString selected = controller->selectedAudioOutputDevice();

    auto addChoice = [this, controller, &selected](const QString &label,
            const QString &value) {
        QAction *action = mSpeakerMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(selected == value);
        connect(action, &QAction::triggered, this,
                [controller, value]() { controller->selectAudioOutputDevice(value); });
    };

    addChoice(QStringLiteral("默认扬声器"), QStringLiteral("Default"));
    mSpeakerMenu->addSeparator();
    const QStringList devices = controller->audioOutputDevices();
    for (const QString &device : devices)
        addChoice(device, device);
    if (devices.isEmpty())
    {
        QAction *empty = mSpeakerMenu->addAction(QStringLiteral("未检测到扬声器"));
        empty->setEnabled(false);
    }
}

void UBPodcastRecordingPalette::populateCameraMenu()
{
    mCameraEnabledAction = nullptr;
    mCameraMenu->clear();
    UBPodcastController *controller = UBPodcastController::instance();

    mCameraEnabledAction = mCameraMenu->addAction(QStringLiteral("启用摄像头"));
    mCameraEnabledAction->setCheckable(true);
    mCameraEnabledAction->setChecked(mCameraAction->isChecked());
    connect(mCameraEnabledAction, &QAction::toggled, this, [this](bool enabled) {
        if (mCameraAction->isChecked() != enabled)
        {
            QSignalBlocker blocker(mCameraAction);
            mCameraAction->setChecked(enabled);
        }
        emit cameraToggled(enabled);
    });

    mCameraMenu->addSeparator();
    const QString selected = controller->selectedCameraDevice();
    auto addChoice = [this, controller, &selected](const QString &label,
            const QString &value) {
        QAction *action = mCameraMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(selected == value);
        connect(action, &QAction::triggered, this,
                [controller, value]() { controller->selectCameraDevice(value); });
    };

    addChoice(QStringLiteral("默认摄像头"), QStringLiteral("Default"));
    mCameraMenu->addSeparator();
    const QStringList devices = controller->cameraDevices();
    for (const QString &device : devices)
        addChoice(device, device);
    if (devices.isEmpty())
    {
        QAction *empty = mCameraMenu->addAction(QStringLiteral("未检测到摄像头"));
        empty->setEnabled(false);
    }
}

void UBPodcastRecordingPalette::moveEvent(QMoveEvent *event)
{
    UBActionPalette::moveEvent(event);
    ensureSystemTopMost();
}

void UBPodcastRecordingPalette::showEvent(QShowEvent *event)
{
    UBActionPalette::showEvent(event);
    ensureSystemTopMost();
}

void UBPodcastRecordingPalette::setNativeOwner(QWidget *owner)
{
#ifdef Q_OS_WIN
    if (!winId())
        return;

    const HWND ownerWindow = owner
            ? reinterpret_cast<HWND>(owner->winId()) : nullptr;
    SetWindowLongPtr(reinterpret_cast<HWND>(winId()), GWLP_HWNDPARENT,
            reinterpret_cast<LONG_PTR>(ownerWindow));
    ensureSystemTopMost();
#else
    Q_UNUSED(owner);
#endif
}

void UBPodcastRecordingPalette::ensureSystemTopMost()
{
    if (!isWindow())
        return;

    if (isVisible())
        raise();

#ifdef Q_OS_WIN
    if (winId())
    {
        SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
}
