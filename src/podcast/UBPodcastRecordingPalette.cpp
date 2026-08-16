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

UBPodcastRecordingPalette::UBPodcastRecordingPalette(QWidget *parent)
     : UBActionPalette(Qt::Horizontal, parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
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

    for (QAction *action : {recordAction, pauseAction, configAction})
    {
        if (QToolButton *button = getButtonFromAction(action))
        {
            button->setFixedSize(40, 40);
            button->setIconSize(QSize(24, 24));
            button->setStyleSheet(buttonStyle);
        }
    }

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

            foreach(QAction* audioInputAction, UBPodcastController::instance()->audioRecordingDevicesActions())
            {
                menu->addAction(audioInputAction);
            }

            menu->addSeparator();

            foreach(QAction* videoSizeAction, UBPodcastController::instance()->videoSizeActions())
            {
                menu->addAction(videoSizeAction);
            }

            menu->addSeparator();

            QList<QAction*> podcastPublication = UBPodcastController::instance()->podcastPublicationActions();

            foreach(QAction* publicationAction, podcastPublication)
            {
                menu->addAction(publicationAction);
            }

            tb->setMenu(menu);
        }
    }

    adjustSize();
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
}


void UBPodcastRecordingPalette::recordingProgressChanged(qint64 ms)
{
    int min = ms / 60000;
    int seconds = (ms / 1000) % 60;

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
