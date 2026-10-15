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




#include "UBDesktopPalette.h"
#include "gui/UBUiTheme.h"

#include <QtGui>

#include "frameworks/UBPlatformUtils.h"

#include "core/UBSettings.h"
#include "core/UBSetting.h"
#include "core/UBApplication.h"
#include "core/UBApplicationController.h"

#include "board/UBDrawingController.h"

#include "gui/UBMainWindow.h"
#include "gui/UBResources.h"

#include "core/memcheck.h"

UBDesktopPalette::UBDesktopPalette(QWidget *parent, UBRightPalette* _rightPalette)
    : UBActionPalette(Qt::TopLeftCorner, parent)
    , mShowHideAction(NULL)
    , mDisplaySelectAction(NULL)
    , rightPalette(_rightPalette)
{
    QList<QAction*> actions;

    setObjectName(QStringLiteral("desktopToolsPalette"));
    setGrip(false);
    mActionUniboard = new QAction(UBUiTheme::icon("pages"), tr("Show OpenBoard"), this);
    connect(mActionUniboard, SIGNAL(triggered()), this, SIGNAL(uniboardClick()));
    actions << mActionUniboard;


    actions << UBApplication::mainWindow->actionPen;
    actions << UBApplication::mainWindow->actionMarker;
    actions << UBApplication::mainWindow->actionEraser;
    QAction *clearInk = UBApplication::mainWindow->actionEraseDesktopAnnotations;
    clearInk->setText(QStringLiteral("清屏"));
    clearInk->setToolTip(QStringLiteral("清除屏幕上的当前笔迹（不影响白板页面）"));
    clearInk->setIcon(UBUiTheme::icon(QStringLiteral("clear")));
    actions << clearInk;
    actions << UBApplication::mainWindow->actionSelector;
    actions << UBApplication::mainWindow->actionPointer;

    if (UBPlatformUtils::hasVirtualKeyboard())
        actions << UBApplication::mainWindow->actionVirtualKeyboard;

    mActionCustomSelect = new QAction(UBUiTheme::icon("capture"), tr("Capture Part of the Screen"), this);
    connect(mActionCustomSelect, SIGNAL(triggered()), this, SIGNAL(customClick()));
    actions << mActionCustomSelect;

    mDisplaySelectAction = new QAction(UBUiTheme::icon("monitor"), tr("Capture the Screen"), this);
    connect(mDisplaySelectAction, SIGNAL(triggered()), this, SIGNAL(screenClick()));
    actions << mDisplaySelectAction;

    QIcon showHideIcon;
    showHideIcon.addPixmap(UBUiTheme::icon("eye").pixmap(24, 24), QIcon::Normal, QIcon::On);
    showHideIcon.addPixmap(UBUiTheme::icon("eyeoff").pixmap(24, 24), QIcon::Normal, QIcon::Off);
    mShowHideAction = new QAction(showHideIcon, "", this);
    mShowHideAction->setCheckable(true);

    connect(mShowHideAction, SIGNAL(triggered(bool)), this, SLOT(showHideClick(bool)));
    actions << mShowHideAction;

    setActions(actions);
    setButtonIconSize(QSize(24, 24));
    styleButtons();

    adjustSizeAndPosition();

    //  This palette can be minimized
    QIcon maximizeIcon;
    maximizeIcon = UBUiTheme::icon("pen");
    mMaximizeAction = new QAction(maximizeIcon, tr("Show the stylus palette"), this);
    connect(mMaximizeAction, SIGNAL(triggered()), this, SLOT(maximizeMe()));
    connect(this, SIGNAL(maximizeStart()), this, SLOT(maximizeMe()));
    connect(this, SIGNAL(minimizeStart(eMinimizedLocation)), this, SLOT(minimizeMe(eMinimizedLocation)));
    setMinimizePermission(true);

    connect(rightPalette, SIGNAL(resized()), this, SLOT(parentResized()));
}


UBDesktopPalette::~UBDesktopPalette()
{

}


void UBDesktopPalette::adjustPosition()
{
    QPoint pos = this->pos();
    if(this->pos().y() < 30){
        pos.setY(30);
        moveInsideParent(pos);
    }
}

void UBDesktopPalette::disappearForCapture()
{
    setWindowOpacity(0.0);
    qApp->processEvents();
}


void UBDesktopPalette::appear()
{
    setWindowOpacity(1.0);
}


void UBDesktopPalette::showHideClick(bool checked)
{
    UBApplication::applicationController->mirroringEnabled(checked);
}


void UBDesktopPalette::updateShowHideState(bool pShowEnabled)
{
    mShowHideAction->setChecked(pShowEnabled);

    if (mShowHideAction->isChecked())
        mShowHideAction->setToolTip(tr("Show Board on Secondary Screen"));
    else
        mShowHideAction->setToolTip(tr("Show Desktop on Secondary Screen"));

    if (pShowEnabled)
        raise();
}


void UBDesktopPalette::setShowHideButtonVisible(bool visible)
{
    mShowHideAction->setVisible(visible);
}


void UBDesktopPalette::setDisplaySelectButtonVisible(bool visible)
{
    mDisplaySelectAction->setVisible(visible);
}

//  Called when the palette is near the border and must be minimized
void UBDesktopPalette::minimizeMe(eMinimizedLocation location)
{
    Q_UNUSED(location);
    QList<QAction*> actions;
    clearLayout();

    actions << mMaximizeAction;
    setActions(actions);
    styleButtons();

    adjustSizeAndPosition();

#ifdef UB_REQUIRES_MASK_UPDATE
        emit refreshMask();
#endif
}

//  Called when the user wants to maximize the palette
void UBDesktopPalette::maximizeMe()
{
    QList<QAction*> actions;
    clearLayout();

    actions << mActionUniboard;
    actions << UBApplication::mainWindow->actionPen;
    actions << UBApplication::mainWindow->actionMarker;
    actions << UBApplication::mainWindow->actionEraser;
    actions << UBApplication::mainWindow->actionEraseDesktopAnnotations;
    actions << UBApplication::mainWindow->actionSelector;
    actions << UBApplication::mainWindow->actionPointer;
    if (UBPlatformUtils::hasVirtualKeyboard())
        actions << UBApplication::mainWindow->actionVirtualKeyboard;

    actions << mActionCustomSelect;
    actions << mDisplaySelectAction;
    actions << mShowHideAction;

    setActions(actions);
    styleButtons();

    adjustSizeAndPosition();

    // Notify that the maximization has been done
    emit maximized();
#ifdef UB_REQUIRES_MASK_UPDATE
        emit refreshMask();
#endif
}

void UBDesktopPalette::showEvent(QShowEvent *event)
{
    Q_UNUSED(event);
    styleButtons();
    adjustSize();
    QIcon eraserIcon;
    UBApplication::mainWindow->actionPen->setIcon(
            UBResources::resources()->coloredPenIcon(true));
    UBApplication::mainWindow->actionMarker->setIcon(
            UBResources::resources()->coloredMarkerIcon(true));
    eraserIcon = UBUiTheme::icon("eraser");
    UBApplication::mainWindow->actionEraser->setIcon(eraserIcon);

    adjustPosition();
}

void UBDesktopPalette::styleButtons()
{
    layout()->setContentsMargins(9, 18, 9, 10);
    layout()->setSpacing(4);
    int visibleCount = 0;
    for (auto* button : mButtons)
        if (button->defaultAction()->isVisible()) ++visibleCount;
    const int availableHeight = parentWidget() ? parentWidget()->height() - 40 : 800;
    const int buttonHeight = visibleCount > 0
            ? qBound(32, (availableHeight - 28 - (visibleCount - 1) * 4) / visibleCount, 44)
            : 44;
    for (auto* button : mButtons)
    {
        button->setFixedSize(44, buttonHeight);
        button->setIconSize(QSize(24, 24));
        button->setCursor(Qt::ArrowCursor);
        button->setAccessibleName(button->defaultAction()->text());
        button->setStyleSheet(QStringLiteral(
            "QToolButton {background:transparent; border:1px solid transparent; border-radius:10px; padding:0;}"
            "QToolButton:hover {background:#f1f5fa; border-color:#dbe2eb;}"
            "QToolButton:checked, QToolButton:pressed {background:#eaf1ff; border-color:#a8c6ff;}"
            "QToolButton:focus {border-color:#2563eb;}"));
    }
}

void UBDesktopPalette::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(UBUiTheme::border(), 1));
    painter.setBrush(QColor(255, 255, 255, 248));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 14, 14);
    painter.setPen(QPen(QColor("#b8c4d4"), 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(width()/2.-7, 9), QPointF(width()/2.+7, 9));
}

void UBDesktopPalette::hideEvent(QHideEvent *event)
{
    Q_UNUSED(event);
    QIcon eraserIcon;
    UBApplication::mainWindow->actionPen->setIcon(
            UBResources::resources()->coloredPenIcon(false));
    UBApplication::mainWindow->actionMarker->setIcon(
            UBResources::resources()->coloredMarkerIcon(false));
    eraserIcon = UBUiTheme::icon(QStringLiteral("eraser"));
    UBApplication::mainWindow->actionEraser->setIcon(eraserIcon);
}

QPoint UBDesktopPalette::buttonPos(QAction *action)
{
    QPoint p;

    UBActionPaletteButton* pB = mMapActionToButton[action];
    if(NULL != pB)
    {
        p = pB->pos();
    }

    return p;
}


int UBDesktopPalette::getParentRightOffset()
{
    return rightPalette->width();
}

void UBDesktopPalette::parentResized()
{
    styleButtons();
    adjustSize();
    QPoint p = pos();
    if (minimizedLocation() == eMinimizedLocation_Right)
    {
        p.setX(parentWidget()->width() - getParentRightOffset() -width());
    }

    moveInsideParent(p);
}
