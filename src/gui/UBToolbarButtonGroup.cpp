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




#include "UBToolbarButtonGroup.h"

#include <QtGui>
#include <QLayout>
#include <QStyleOption>
#include <QtMath>

#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "gui/UBMainWindow.h"

#include "board/UBDrawingController.h"

#include "core/memcheck.h"

UBToolbarButtonGroup::UBToolbarButtonGroup(QToolBar *toolBar, const QList<QAction*> &actions, QString objectNameprefix)
    : QWidget(toolBar)
    , mActions(actions)
    , mCurrentIndex(-1)
    , mDisplayLabel(true)
    , mActionGroup(0)
    , mPreviewType(ActionIconPreview)
    , mSlider(0)
    , mSliderValueLabel(0)
    , mCaptionLabel(0)
{
    Q_ASSERT(actions.size() > 0);

    if (UBApplication::mainWindow)
    {
        if (actions.first() == UBApplication::mainWindow->actionColor0)
            mPreviewType = ColorSwatchPreview;
        else if (actions.first() == UBApplication::mainWindow->actionLineSmall)
            mPreviewType = LineWidthPreview;
        else if (actions.first() == UBApplication::mainWindow->actionEraserSmall)
            mPreviewType = EraserSizePreview;
    }

    mToolButton = qobject_cast<QToolButton*>(toolBar->layout()->itemAt(0)->widget());
    Q_ASSERT(mToolButton);

    QVBoxLayout *verticalLayout = new QVBoxLayout(this);
    QHBoxLayout *horizontalLayout = new QHBoxLayout();
    // Keep the continuous controls compact enough for a single-row toolbar,
    // especially on Windows displays using 150% scaling.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    verticalLayout->setSizeConstraint(QLayout::SetFixedSize);
    verticalLayout->setContentsMargins(3, 2, 3, 0);
    verticalLayout->setSpacing(1);
    horizontalLayout->setContentsMargins(0, 0, 0, 0);
    horizontalLayout->setSpacing(mPreviewType == ColorSwatchPreview ? 2 : 5);
    horizontalLayout->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    verticalLayout->addLayout(horizontalLayout);

    mActionGroup = new QActionGroup(this);
    mActionGroup->setExclusive(true);

    foreach(QAction *action, actions)
        mActionGroup->addAction(action);

    mLabel = actions.first()->text();

    mCaptionLabel = new QLabel(mLabel, this);
    mCaptionLabel->setObjectName(QStringLiteral("ubToolbarGroupCaption"));
    mCaptionLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    mCaptionLabel->setFixedHeight(15);
    verticalLayout->addWidget(mCaptionLabel);

    if (usesContinuousSlider())
    {
        mSlider = new QSlider(Qt::Horizontal, this);
        mSlider->setObjectName(mPreviewType == LineWidthPreview
                ? QStringLiteral("ubLineWidthSlider")
                : QStringLiteral("ubEraserWidthSlider"));
        mSlider->setRange(0, 100);
        mSlider->setSingleStep(1);
        mSlider->setPageStep(10);
        mSlider->setFixedWidth(76);
        mSlider->setCursor(Qt::PointingHandCursor);
        mSlider->setToolTip(mPreviewType == LineWidthPreview
                ? QStringLiteral("拖动调整线条粗细")
                : QStringLiteral("拖动调整橡皮擦大小"));

        mSliderValueLabel = new QLabel(this);
        mSliderValueLabel->setObjectName(QStringLiteral("ubSliderValueLabel"));
        mSliderValueLabel->setAlignment(Qt::AlignCenter);
        mSliderValueLabel->setFixedWidth(28);

        horizontalLayout->addWidget(mSlider);
        horizontalLayout->addWidget(mSliderValueLabel);

        connect(mSlider, SIGNAL(valueChanged(int)),
                this, SLOT(sliderMoved(int)));

        if (mPreviewType == LineWidthPreview)
        {
            connect(UBDrawingController::drawingController(),
                    SIGNAL(lineWidthValueChanged(int)),
                    this, SLOT(setSliderValue(int)));
        }
        else
        {
            connect(UBDrawingController::drawingController(),
                    SIGNAL(eraserWidthValueChanged(int)),
                    this, SLOT(setSliderValue(int)));
        }

        setSliderValue(normalizedSliderValue());

        // The three legacy actions remain available to keyboard shortcuts.
        // Only the main toolbar owns this bridge; desktop property palettes
        // share the same QActions and must not apply the change repeatedly.
        if (objectNameprefix.isEmpty())
        {
            foreach(QAction *action, actions)
            {
                connect(action, &QAction::triggered, this,
                        [this, action]() { selected(action); });
            }
        }
    }
    else
    {
        int i = 0;

        foreach(QAction *action, actions)
        {
            QToolButton *button = new QToolButton(this);
            mButtons.append(button);
            button->setDefaultAction(action);
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            if (mPreviewType == ColorSwatchPreview)
            {
                button->setProperty("compactColorChoice", true);
                button->setIconSize(QSize(16, 16));
            }
            else
            {
                button->setIconSize(QSize(22, 22));
            }

            connect(button, SIGNAL(triggered(QAction*)),
                    this, SLOT(selected(QAction*)));

            if(i == 0)
            {
                objectNameprefix.length() > 0 ?
                    button->setObjectName(objectNameprefix + "-ubButtonGroupLeft")
                    : button->setObjectName("ubButtonGroupLeft");
            }
            else if (i == actions.size() - 1)
            {
                objectNameprefix.length() > 0 ?
                    button->setObjectName(objectNameprefix + "-ubButtonGroupRight")
                    : button->setObjectName("ubButtonGroupRight");

            }
            else
            {
                objectNameprefix.length() > 0 ?
                    button->setObjectName(objectNameprefix + "-ubButtonGroupCenter")
                    : button->setObjectName("ubButtonGroupCenter");
            }

            horizontalLayout->addWidget(button);
            i++;
        }
    }

    refreshPreviewIcons();
}

UBToolbarButtonGroup::~UBToolbarButtonGroup()
{
    // NOOP
}

void UBToolbarButtonGroup::setLabel(const QString& label)
{
    mLabel = label;
    if (mCaptionLabel)
        mCaptionLabel->setText(label);
}

void UBToolbarButtonGroup::setIcon(const QIcon &icon, int index)
{
    Q_ASSERT(index < mActions.size());

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    foreach(QObject *widget, mActions.at(index)->associatedObjects())
#else
    foreach(QWidget *widget, mActions.at(index)->associatedWidgets())
#endif
    {
        QToolButton *button = qobject_cast<QToolButton*>(widget);
        if (button)
        {
            // change icon at action, so that updates of action do not overwrite the icon
            for (QAction* action : button->actions())
            {
                action->setIcon(icon);
            }
        }
    }
}

void UBToolbarButtonGroup::setColor(const QColor &color, int index)
{
    setIcon(colorSwatchIcon(color), index);
    if (index >= 0 && index < mButtons.size())
    {
        mButtons.at(index)->setToolTip(QStringLiteral("当前颜色：%1")
                .arg(color.name(QColor::HexRgb).toUpper()));
    }
}

void UBToolbarButtonGroup::setPreviewType(PreviewType type)
{
    mPreviewType = type;
    refreshPreviewIcons();
}

QIcon UBToolbarButtonGroup::colorSwatchIcon(const QColor &color) const
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color.lightness() > 225
            ? QColor("#94A3B8") : QColor("#334155"), 1.0));
    painter.setBrush(color);
    painter.drawEllipse(QRectF(2.0, 2.0, 12.0, 12.0));
    return QIcon(pixmap);
}

QIcon UBToolbarButtonGroup::lineWidthIcon(const QColor &color, int index) const
{
    static const qreal previewWidths[] = {2.0, 5.0, 9.0};
    QPixmap pixmap(32, 28);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, previewWidths[qBound(0, index, 2)],
            Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(5, 14), QPointF(27, 14));
    return QIcon(pixmap);
}

QIcon UBToolbarButtonGroup::eraserSizeIcon(int index) const
{
    static const qreal previewDiameters[] = {7.0, 13.0, 20.0};
    const qreal diameter = previewDiameters[qBound(0, index, 2)];
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#64748B"), 1.5));
    painter.setBrush(QColor("#F8FAFC"));
    painter.drawEllipse(QRectF((28.0 - diameter) / 2.0,
            (28.0 - diameter) / 2.0, diameter, diameter));
    return QIcon(pixmap);
}

void UBToolbarButtonGroup::refreshPreviewIcons()
{
    if (usesContinuousSlider())
    {
        updateSliderAppearance();
        setSliderValue(normalizedSliderValue());
        return;
    }

    if (mPreviewType == ActionIconPreview || mButtons.isEmpty())
        return;

    if (mPreviewType == ColorSwatchPreview)
    {
        colorPaletteChanged();
        return;
    }

    const QColor previewColor = UBDrawingController::drawingController()
            ? UBDrawingController::drawingController()->currentToolColor()
            : QColor(Qt::black);

    for (int i = 0; i < mButtons.size(); ++i)
    {
        if (mPreviewType == LineWidthPreview)
        {
            setIcon(lineWidthIcon(previewColor, i), i);
            static const char *widthNames[] = {"细线", "中线", "粗线"};
            mButtons.at(i)->setToolTip(QString::fromUtf8(
                    widthNames[qBound(0, i, 2)]));
        }
        else if (mPreviewType == EraserSizePreview)
        {
            setIcon(eraserSizeIcon(i), i);
            static const char *eraserNames[] = {"小橡皮擦", "中橡皮擦", "大橡皮擦"};
            mButtons.at(i)->setToolTip(QString::fromUtf8(
                    eraserNames[qBound(0, i, 2)]));
        }
    }
}

void UBToolbarButtonGroup::selected(QAction *action)
{
    for (int i = 0; i < mActions.size(); ++i)
    {
        if (mActions.at(i) == action)
        {
            setCurrentIndex(i);
            emit activated(i);
            break;
        }
    }
}

int UBToolbarButtonGroup::currentIndex() const
{
    return mCurrentIndex;
}

void UBToolbarButtonGroup::setCurrentIndex(int index)
{
    Q_ASSERT(index >= 0 && index < mActions.size());

    if (index != mCurrentIndex)
    {
        for(int i = 0; i < mButtons.size(); i++)
        {
            mButtons.at(i)->setChecked(i == index);
        }
        mCurrentIndex = index;
        for (int i = 0; i < mButtons.size(); ++i)
            mButtons.at(i)->setProperty("currentChoice", i == index);
        emit currentIndexChanged(index);
    }

    if (usesContinuousSlider())
        setSliderValue(normalizedSliderValue());
}

bool UBToolbarButtonGroup::usesContinuousSlider() const
{
    return mPreviewType == LineWidthPreview
            || mPreviewType == EraserSizePreview;
}

int UBToolbarButtonGroup::normalizedSliderValue() const
{
    if (mPreviewType == EraserSizePreview)
    {
        return qBound(0, qRound(100.0
                * (UBSettings::settings()->currentEraserWidth() - 8.0)
                / 152.0), 100);
    }

    const bool marker = UBDrawingController::drawingController()->stylusTool()
            == UBStylusTool::Marker;
    const qreal minimum = marker ? 6.0 : 1.0;
    const qreal maximum = marker ? 60.0 : 16.0;
    return qBound(0, qRound(100.0
            * (UBDrawingController::drawingController()->currentToolWidth()
            - minimum) / (maximum - minimum)), 100);
}

void UBToolbarButtonGroup::setSliderValue(int value)
{
    if (!mSlider)
        return;

    const QSignalBlocker blocker(mSlider);
    mSlider->setValue(qBound(0, value, 100));
    updateSliderValueLabel();
    updateSliderAppearance();
}

void UBToolbarButtonGroup::sliderMoved(int value)
{
    if (mPreviewType == LineWidthPreview)
        UBDrawingController::drawingController()->setLineWidthValue(value);
    else if (mPreviewType == EraserSizePreview)
        UBDrawingController::drawingController()->setEraserWidthValue(value);

    updateSliderValueLabel();
    updateSliderAppearance();
}

void UBToolbarButtonGroup::updateSliderValueLabel()
{
    if (!mSliderValueLabel)
        return;

    if (mPreviewType == EraserSizePreview)
    {
        const int width = qRound(UBSettings::settings()->currentEraserWidth());
        mSliderValueLabel->setText(QStringLiteral("%1").arg(width));
        mSlider->setToolTip(QStringLiteral("橡皮擦大小：%1，拖动调整").arg(width));
    }
    else
    {
        const qreal width = UBDrawingController::drawingController()->currentToolWidth();
        mSliderValueLabel->setText(width < 10.0
                ? QString::number(width, 'f', 1)
                : QString::number(qRound(width)));
        mSlider->setToolTip(QStringLiteral("线条粗细：%1，拖动调整")
                .arg(QString::number(width, 'f', 1)));
    }
}

void UBToolbarButtonGroup::updateSliderAppearance()
{
    if (!mSlider)
        return;

    const QColor activeColor = mPreviewType == LineWidthPreview
            ? UBDrawingController::drawingController()->currentToolColor()
            : QColor(QStringLiteral("#64748B"));
    const QString colorName = activeColor.name(QColor::HexRgb);

    mSlider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { height: 8px; border-radius: 4px; background: #E2E8F0; }"
        "QSlider::sub-page:horizontal { border-radius: 4px; background: %1; }"
        "QSlider::add-page:horizontal { border-radius: 4px; background: #E2E8F0; }"
        "QSlider::handle:horizontal { width: 18px; height: 18px; margin: -6px 0;"
        " border-radius: 9px; border: 2px solid %1; background: white; }"
        "QSlider::handle:horizontal:hover { width: 20px; height: 20px; margin: -7px 0;"
        " border-radius: 10px; background: #F8FAFC; }").arg(colorName));
}

void UBToolbarButtonGroup::paintEvent(QPaintEvent *event)
{
    // The caption is a real layout item instead of being painted underneath
    // the controls.  This prevents colour swatches and sliders from covering
    // their labels on high-DPI Windows displays.
    QWidget::paintEvent(event);
}


void UBToolbarButtonGroup::colorPaletteChanged()
{
    bool isDarkBackground = UBSettings::settings()->isDarkBackground();

    QList<QColor> colors;

    if (UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Pen 
        || UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Line)
    {
        colors = UBSettings::settings()->penColors(isDarkBackground);
    }
    else if (UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Marker)
    {
        colors = UBSettings::settings()->markerColors(isDarkBackground);
    }

    if (mPreviewType == LineWidthPreview || mPreviewType == EraserSizePreview)
    {
        refreshPreviewIcons();
        return;
    }

    for (int i = 0; i < mButtons.size() && i < colors.size(); i++)
    {
        setColor(colors.at(i), i);
    }
}

void UBToolbarButtonGroup::displayText(QVariant display)
{
    mDisplayLabel = display.toBool();
    if (mCaptionLabel)
        mCaptionLabel->setVisible(mDisplayLabel);
    update();
}
