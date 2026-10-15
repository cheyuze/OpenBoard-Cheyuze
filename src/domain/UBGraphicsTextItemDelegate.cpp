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




#include <QtGui>
#include <QtSvg>

#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "UBGraphicsTextItemDelegate.h"
#include "UBGraphicsTextItem.h"
#include "UBGraphicsGroupContainerItem.h"
#include "UBGraphicsDelegateFrame.h"
#include "UBGraphicsScene.h"
#include "UBImageTransform.h"
#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "board/UBDrawingController.h"
#include "gui/UBResources.h"
#include "gui/UBUiTheme.h"

#include <QDoubleSpinBox>
#include <QGraphicsProxyWidget>
#include <QGraphicsSceneHoverEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainterPathStroker>
#include <QSignalBlocker>
#include <QToolButton>

#include "core/memcheck.h"

namespace
{
// Only the editor adornments change. Text remains a QTextDocument, so input
// methods, rich text, serialization and text undo keep their existing paths.
class TextFrame final : public UBGraphicsDelegateFrame
{
public:
    explicit TextFrame(UBGraphicsTextItemDelegate* delegate)
        : UBGraphicsDelegateFrame(delegate, {}, 1), mTextDelegate(delegate)
    {
        for (auto* child : childItems()) child->hide();
        resetTransform();
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        setAcceptHoverEvents(true);
        setToolTip(QStringLiteral("拖动边框移动；左右调整宽度并自动换行；上方手柄旋转（Shift 按 15° 吸附）"));
    }

    void setAntiScale(qreal scale) override { mUnit = scale; }

    void positionHandles() override
    {
        auto* item = mTextDelegate->UBGraphicsItemDelegate::delegated();
        if (!item) return;
        const QRectF bounds = item->boundingRect();
        mOutline = item->mapToScene(QPolygonF(bounds));
        for (int i = 0; i < 8; ++i)
            mHandles[i] = item->mapToScene(UBImageTransform::point(bounds, i));
        QPointF normal = mHandles[1] - item->mapToScene(bounds.center());
        const qreal length = std::hypot(normal.x(), normal.y());
        mRotate = mHandles[1] + (length > 0 ? normal / length : QPointF(0, -1))*28*mUnit;
        QRectF area = mOutline.boundingRect();
        area |= hitRect(mRotate);
        setRect(area.adjusted(-12*mUnit, -12*mUnit, 12*mUnit, 12*mUnit));
        setZValue(1010000.0);
        update();
    }

    QPainterPath shape() const override
    {
        QPainterPath border;
        border.addPolygon(mOutline);
        border.closeSubpath();
        QPainterPathStroker stroker;
        stroker.setWidth(8*mUnit);
        QPainterPath result = stroker.createStroke(border);
        if (!mTextDelegate->isLocked()) {
            for (int i : {0, 2, 3, 4, 6, 7}) result.addRect(hitRect(mHandles[i]));
            result.addEllipse(hitRect(mRotate));
        }
        return result;
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen pen(UBUiTheme::blue(), 1.5*mUnit);
        if (mTextDelegate->isLocked()) pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(mOutline);
        if (!mTextDelegate->isLocked()) {
            painter->drawLine(mHandles[1], mRotate);
            painter->setBrush(Qt::white);
            for (int i : {0, 2, 3, 4, 6, 7}) {
                const QPointF p = mHandles[i];
                painter->drawRoundedRect(QRectF(p.x()-4*mUnit, p.y()-4*mUnit, 8*mUnit, 8*mUnit), mUnit, mUnit);
            }
            painter->drawEllipse(mRotate, 7*mUnit, 7*mUnit);
            painter->drawArc(QRectF(mRotate.x()-4*mUnit, mRotate.y()-4*mUnit, 8*mUnit, 8*mUnit), 20*16, 260*16);
        }
        painter->restore();
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override
    {
        const int tool = toolAt(event->scenePos());
        if (tool == 8) setCursor(Qt::OpenHandCursor);
        else if (tool < 0) setCursor(mTextDelegate->isLocked() ? Qt::ArrowCursor : Qt::SizeAllCursor);
        else {
            auto* item = mTextDelegate->UBGraphicsItemDelegate::delegated();
            const QPointF vector = item->mapToScene(item->boundingRect().topRight())
                    - item->mapToScene(item->boundingRect().topLeft());
            const int sector = (qRound(qRadiansToDegrees(std::atan2(vector.y(), vector.x()))/45) % 4 + 4) % 4;
            const Qt::CursorShape cursors[] = {Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor};
            setCursor(cursors[sector]);
        }
        event->accept();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || mTextDelegate->isLocked()) { event->accept(); return; }
        auto* item = mTextDelegate->UBGraphicsItemDelegate::delegated();
        mTool = toolAt(event->scenePos());
        mInitial = item->transform();
        mInitialPos = item->pos();
        mBounds = item->boundingRect();
        mInverse = item->sceneTransform().inverted(&mInvertible);
        mParentInverse = item->parentItem() ? item->parentItem()->sceneTransform().inverted() : QTransform();
        mPress = event->scenePos();
        mCenter = mInitial.map(mBounds.center()) + mInitialPos;
        mTextDelegate->startUndoStep();
        mTextDelegate->setInteractionActive(true);
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (mTool == -2 || !mInvertible || mTextDelegate->isLocked()) { event->accept(); return; }
        auto* item = mTextDelegate->UBGraphicsItemDelegate::delegated();
        if (mTool == 8) {
            const QPointF start = mParentInverse.map(mPress) - mCenter;
            const QPointF end = mParentInverse.map(event->scenePos()) - mCenter;
            if (std::hypot(end.x(), end.y()) > mUnit) {
                qreal delta = UBImageTransform::normalizeAngle(qRadiansToDegrees(std::atan2(end.y(), end.x())-std::atan2(start.y(), start.x())));
                if (event->modifiers().testFlag(Qt::ShiftModifier))
                    delta = qRound((UBImageTransform::angle(mInitial)+delta)/15)*15-UBImageTransform::angle(mInitial);
                item->setTransform(UBImageTransform::rotated(mBounds, mInitial, delta));
            }
        } else if (mTool >= 0) {
            // A text box reflows rather than stretching its glyphs.
            auto* text = dynamic_cast<UBGraphicsTextItem*>(item);
            if (text) {
                const bool left = mTool == 0 || mTool == 6 || mTool == 7;
                const qreal dx = mInverse.map(event->scenePos()).x()-mInverse.map(mPress).x();
                const qreal width = qMax(qreal(48), mBounds.width()+(left ? -dx : dx));
                text->resize(width, text->textHeight());
                const QPointF fixed = left ? mBounds.topRight() : mBounds.topLeft();
                const QPointF anchor = left ? text->boundingRect().topRight() : text->boundingRect().topLeft();
                text->setPos(mInitialPos+mInitial.map(fixed)-mInitial.map(anchor));
            }
        } else {
            item->setPos(mInitialPos+mParentInverse.map(event->scenePos())-mParentInverse.map(mPress));
        }
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (mTool != -2) {
            mTool = -2;
            mTextDelegate->commitUndoStep();
            mTextDelegate->setInteractionActive(false);
        }
        event->accept();
    }

private:
    QRectF hitRect(const QPointF& p) const
    { return {p.x()-10*mUnit, p.y()-10*mUnit, 20*mUnit, 20*mUnit}; }
    int toolAt(const QPointF& p) const
    {
        if (mTextDelegate->isLocked()) return -2;
        if (hitRect(mRotate).contains(p)) return 8;
        for (int i : {0, 2, 4, 6, 3, 7})
            if (hitRect(mHandles[i]).contains(p)) return i;
        return -1;
    }
    UBGraphicsTextItemDelegate* mTextDelegate;
    qreal mUnit = 1;
    QPolygonF mOutline;
    QPointF mHandles[8], mRotate, mPress, mInitialPos, mCenter;
    QRectF mBounds;
    QTransform mInitial, mInverse, mParentInverse;
    int mTool = -2;
    bool mInvertible = false;
};

QString textMenuStyle()
{
    return QStringLiteral(
        "QMenu {background:#ffffff; color:#202936; border:1px solid #dbe2eb; border-radius:9px; padding:6px;}"
        "QMenu::item {background:transparent; color:#202936; padding:9px 30px 9px 28px; border-radius:6px;}"
        "QMenu::item:selected {background:#eaf1ff; color:#1d4ed8;}"
        "QMenu::item:disabled {color:#a1adbd;}"
        "QMenu::separator {height:1px; background:#e5eaf1; margin:4px 8px;}");
}
}

UBGraphicsTextItemDelegate::UBGraphicsTextItemDelegate(UBGraphicsTextItem* item, QObject*)
    : UBGraphicsItemDelegate(item, nullptr, GF_SCALABLE_X_AXIS | GF_DUPLICATION_ENABLED
        | GF_MENU_SPECIFIED | GF_ZORDER_MANIPULATIONS_ALLOWED | GF_REVOLVABLE)
{
    item->setData(UBGraphicsItemData::ItemEditable, true);
    connect(item->document(), &QTextDocument::cursorPositionChanged, this, [this] { updateFormattingControls(); });
}

UBGraphicsTextItemDelegate::~UBGraphicsTextItemDelegate()
{
    if (mTextToolbar) delete mTextToolbar.data();
    delete mMenu;
    mMenu = nullptr;
}

void UBGraphicsTextItemDelegate::createControls()
{
    if (!mFrame) {
        mFrame = new TextFrame(this);
        mFrame->hide();
    }
    if (mTextToolbar) return;

    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("textContextToolbar"));
    panel->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    panel->setStyleSheet(QStringLiteral(
        "QWidget#textContextToolbar {background:#ffffff; color:#202936; border:1px solid #dbe2eb; border-radius:10px;}"
        "QLabel {border:0; color:#526174; background:transparent;}"
        "QToolButton {color:#202936; background:transparent; border:1px solid transparent; border-radius:7px; padding:0 5px;}"
        "QToolButton:hover, QToolButton:pressed, QToolButton:checked {background:#eaf1ff; border-color:#b9cff6; color:#1d4ed8;}"
        "QToolButton:focus {border-color:#2563eb;}"
        "QToolButton:disabled {color:#a1adbd;}"
        "QDoubleSpinBox {background:#f8fafc; color:#202936; border:1px solid #cbd6e5; border-radius:6px; padding:3px 5px; selection-background-color:#dbeafe; selection-color:#202936;}"
        "QDoubleSpinBox:focus {border-color:#2563eb;}"));
    auto* row = new QHBoxLayout(panel);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(3);
    auto* title = new QLabel(QStringLiteral("文本"), panel);
    row->addWidget(title);
    const auto button = [&](const QString& name, const QString& icon, const QString& tip) {
        auto* control = new QToolButton(panel);
        control->setObjectName(name);
        control->setIcon(UBUiTheme::icon(icon));
        control->setIconSize(QSize(20,20));
        control->setFixedSize(36,40);
        control->setFocusPolicy(Qt::NoFocus);
        control->setCursor(Qt::ArrowCursor);
        control->setToolTip(tip);
        control->setAccessibleName(tip);
        row->addWidget(control);
        return control;
    };
    mFontControl = button("textFont", "text", QStringLiteral("选择字体"));
    mFontControl->setToolButtonStyle(Qt::ToolButtonTextOnly);
    mFontControl->setFixedWidth(112);
    connect(mFontControl, &QToolButton::clicked, this, &UBGraphicsTextItemDelegate::pickFont);

    mSizeControl = new QDoubleSpinBox(panel);
    mSizeControl->setObjectName(QStringLiteral("textFontSize"));
    mSizeControl->setRange(1,9999);
    mSizeControl->setDecimals(1);
    mSizeControl->setSuffix(QStringLiteral(" pt"));
    mSizeControl->setSingleStep(1);
    mSizeControl->setKeyboardTracking(false);
    mSizeControl->setButtonSymbols(QAbstractSpinBox::NoButtons);
    mSizeControl->setFixedSize(76,36);
    mSizeControl->setToolTip(QStringLiteral("字号：直接输入数值，或用上下方向键微调"));
    mSizeControl->setAccessibleName(QStringLiteral("字号（磅）"));
    row->addWidget(mSizeControl);
    connect(mSizeControl, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double size) {
        QTextCharFormat format;
        format.setFontPointSize(size);
        applyFormat(format);
        UBSettings::settings()->setFontPointSize(qRound(size));
    });
    connect(mSizeControl, &QDoubleSpinBox::editingFinished, this, [this] {
        if (mDelegated->isSelected()) mDelegated->setFocus();
    });
    mBoldControl = button("textBold", "bold", QStringLiteral("加粗（Ctrl+B）"));
    mItalicControl = button("textItalic", "italic", QStringLiteral("斜体（Ctrl+I）"));
    mUnderlineControl = button("textUnderline", "underline", QStringLiteral("下划线（Ctrl+U）"));
    for (auto* control : {mBoldControl.data(), mItalicControl.data(), mUnderlineControl.data()})
        control->setCheckable(true);
    connect(mBoldControl, &QToolButton::clicked, this, [this](bool checked) {
        QTextCharFormat format; format.setFontWeight(checked ? QFont::Bold : QFont::Normal); applyFormat(format);
    });
    connect(mItalicControl, &QToolButton::clicked, this, [this](bool checked) {
        QTextCharFormat format; format.setFontItalic(checked); applyFormat(format);
    });
    connect(mUnderlineControl, &QToolButton::clicked, this, [this](bool checked) {
        QTextCharFormat format; format.setFontUnderline(checked); applyFormat(format);
    });
    mColorControl = button("textColor", "textcolor", QStringLiteral("文字颜色"));
    connect(mColorControl, &QToolButton::clicked, this, &UBGraphicsTextItemDelegate::pickColor);
    mAlignControl = button("textAlignment", "alignleft", QStringLiteral("段落对齐"));
    auto* alignment = new QMenu(panel);
    alignment->setStyleSheet(textMenuStyle());
    const QStringList names {QStringLiteral("左对齐"), QStringLiteral("居中对齐"), QStringLiteral("右对齐")};
    const QStringList icons {"alignleft", "aligncenter", "alignright"};
    const Qt::Alignment alignments[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
    for (int i=0; i<3; ++i) {
        auto* action = alignment->addAction(UBUiTheme::icon(icons[i]), names[i]);
        action->setObjectName(icons[i]);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, value=alignments[i]] { applyAlignment(value); });
    }
    mAlignControl->setMenu(alignment);
    mAlignControl->setPopupMode(QToolButton::InstantPopup);
    auto* more = button("textMore", "more", QStringLiteral("文本更多操作"));
    more->setText(QStringLiteral("更多"));
    more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    more->setFixedWidth(64);
    connect(more, &QToolButton::clicked, this, [this, more] {
        openMenu(more->mapToGlobal(QPoint(0, more->height())));
    });
    panel->adjustSize();
    mTextToolbar = new QGraphicsProxyWidget;
    mTextToolbar->setWidget(panel);
    mTextToolbar->setData(UBGraphicsItemData::ItemLayerType, UBItemLayerType::Control);
    mTextToolbar->setData(UBGraphicsItemData::itemLayerType, itemLayerType::SelectionFrame);
    mTextToolbar->hide();
    updateFormattingControls();
}

void UBGraphicsTextItemDelegate::freeControls()
{
    if (mTextToolbar) {
        if (mTextToolbar->scene()) mTextToolbar->scene()->removeItem(mTextToolbar);
        mTextToolbar->hide();
        mTextToolbar->deleteLater();
        mTextToolbar = nullptr;
    }
    if (mFrame) {
        if (mFrame->scene()) mFrame->scene()->removeItem(mFrame);
        mFrame->hide();
        mFrame->deleteLater();
        mFrame = nullptr;
    }
    mFontControl = nullptr;
    mSizeControl = nullptr;
    mColorControl = mBoldControl = mItalicControl = mUnderlineControl = mAlignControl = nullptr;
    mInteracting = false;
}

bool UBGraphicsTextItemDelegate::controlsExist() const
{
    return mFrame && mTextToolbar;
}

void UBGraphicsTextItemDelegate::positionHandles()
{
    if (!controlsExist() || !mDelegated->scene()) return;
    auto* scene = mDelegated->scene();
    const bool visible = mDelegated->isSelected() && mDelegated->isVisible()
            && UBGraphicsItemDelegate::delegated() == mDelegated
            && UBDrawingController::drawingController()->stylusTool() != UBStylusTool::Play;
    if (!mFrame->scene()) scene->addItem(mFrame);
    if (!mTextToolbar->scene()) scene->addItem(mTextToolbar);
    mFrame->setAntiScale(mAntiScaleRatio);
    mFrame->positionHandles();
    mFrame->QGraphicsRectItem::setVisible(visible);
    mTextToolbar->setVisible(visible && !mInteracting);
    mTextToolbar->setScale(mAntiScaleRatio);
    mTextToolbar->setZValue(1010001.0);
    const QRectF bounds = mDelegated->sceneBoundingRect();
    const QSizeF toolbar = mTextToolbar->boundingRect().size()*mAntiScaleRatio;
    auto* view = UBApplication::boardController->controlView();
    const QRectF viewport = view->mapToScene(view->viewport()->rect()).boundingRect();
    qreal x = bounds.center().x()-toolbar.width()/2;
    qreal y = bounds.top()-toolbar.height()-42*mAntiScaleRatio;
    if (y < viewport.top()+8*mAntiScaleRatio) y = bounds.bottom()+18*mAntiScaleRatio;
    x = qBound(viewport.left()+8*mAntiScaleRatio, x, std::max(viewport.left()+8*mAntiScaleRatio, viewport.right()-toolbar.width()-8*mAntiScaleRatio));
    y = qBound(viewport.top()+8*mAntiScaleRatio, y, std::max(viewport.top()+8*mAntiScaleRatio, viewport.bottom()-toolbar.height()-8*mAntiScaleRatio));
    mTextToolbar->setPos(x,y);
    updateFormattingControls();
}

qreal UBGraphicsTextItemDelegate::titleBarWidth()
{
    // The floating toolbar no longer imposes its width on the text itself.
    return 48;
}

void UBGraphicsTextItemDelegate::setInteractionActive(bool active)
{
    mInteracting = active;
    positionHandles();
}

void UBGraphicsTextItemDelegate::contentsChanged()
{
    delegated()->contentsChanged();
    positionHandles();
}

void UBGraphicsTextItemDelegate::customize(QFontDialog &fontDialog)
{
    if (UBSettings::settings()->isDarkBackground()) {
        fontDialog.setStyleSheet("background-color: white;");
    }

    QListView *fontNameListView = NULL;
    QList<QListView*> listViews = fontDialog.findChildren<QListView*>();
    if (listViews.count() > 0)
    {
        fontNameListView = listViews.at(0);
        foreach (QListView* listView, listViews)
        {
            if (listView->pos().x() < fontNameListView->pos().x())
                fontNameListView = listView;
        }
    }
    if (fontNameListView)
    {
        QStringListModel *stringListModel = dynamic_cast<QStringListModel*>(fontNameListView->model());
        if (stringListModel)
        {
            QStringList dialogFontNames = stringListModel->stringList();
            QStringList safeWebFontNames;
            safeWebFontNames.append("Arial");
            safeWebFontNames.append("Arial Black");
            safeWebFontNames.append("Comic Sans MS");
            safeWebFontNames.append("Courier New");
            safeWebFontNames.append("Georgia");
            safeWebFontNames.append("Impact");
            safeWebFontNames.append("Times New Roman");
            safeWebFontNames.append("Trebuchet MS");
            safeWebFontNames.append("Verdana");

            QStringList customFontList =  UBResources::resources()->customFontList();
            int index = 0;
            int remove = 0;

            for (const QString& dialogFontName : dialogFontNames)
            {
                if (UBStringUtils::containsPrefix(safeWebFontNames, dialogFontName, Qt::CaseInsensitive) ||
                        UBStringUtils::containsPrefix(customFontList, dialogFontName, Qt::CaseSensitive))
                {
                    stringListModel->removeRows(index, remove);
                    remove = 0;
                    index++;
                }
                else
                {
                    ++remove;
                }
            }

            stringListModel->removeRows(index, remove);
        }
    }
    QList<QComboBox*> comboBoxes = fontDialog.findChildren<QComboBox*>();
    if (comboBoxes.count() > 0)
        comboBoxes.at(0)->setEnabled(false);
}

void UBGraphicsTextItemDelegate::pickFont()
{
    if (!mDelegated->scene() || isLocked() || !isEditable()) return;
    const QTextCursor saved = delegated()->textCursor();
    QFontDialog dialog(UBApplication::boardController->controlView());
    dialog.setWindowTitle(QStringLiteral("字体"));
    dialog.setOption(QFontDialog::DontUseNativeDialog);
    dialog.setCurrentFont(saved.charFormat().font());
    customize(dialog);
    if (dialog.exec() != QDialog::Accepted) return;
    const QFont font = dialog.selectedFont();
    delegated()->setTextCursor(saved);
    QTextCharFormat format;
    format.setFont(font);
    applyFormat(format);
    auto* settings = UBSettings::settings();
    settings->setFontFamily(font.family());
    settings->setFontStyleName(font.styleName());
    settings->setBoldFont(font.bold());
    settings->setItalicFont(font.italic());
    settings->setFontPointSize(font.pointSize());
}

void UBGraphicsTextItemDelegate::pickColor()
{
    if (!mDelegated->scene() || isLocked() || !isEditable()) return;
    const QTextCursor saved = delegated()->textCursor();
    const QBrush brush = saved.charFormat().foreground();
    QColorDialog dialog(brush.style() != Qt::NoBrush ? brush.color() : delegated()->defaultTextColor(),
                        UBApplication::boardController->controlView());
    dialog.setWindowTitle(QStringLiteral("文字颜色"));
    dialog.setOption(QColorDialog::DontUseNativeDialog);
    if (dialog.exec() != QDialog::Accepted) return;
    delegated()->setTextCursor(saved);
    QTextCharFormat format;
    format.setForeground(dialog.selectedColor());
    applyFormat(format);
    if (!saved.hasSelection() || saved.selectionEnd()-saved.selectionStart() == delegated()->toPlainText().length()) {
        delegated()->setDefaultTextColor(dialog.selectedColor());
        UBGraphicsTextItem::lastUsedTextColor = dialog.selectedColor();
    }
}

void UBGraphicsTextItemDelegate::decreaseSize()
{
    changeTextSize(-delta, changeSize);
}

void UBGraphicsTextItemDelegate::increaseSize()
{
    changeTextSize(delta, changeSize);
}

void UBGraphicsTextItemDelegate::scaleTextSize(qreal multiplier)
{
    changeTextSize(multiplier, scaleSize);
}


void UBGraphicsTextItemDelegate::applyFormat(const QTextCharFormat& format)
{
    if (!isEditable() || isLocked()) return;
    QTextCursor cursor = delegated()->textCursor();
    const int anchor = cursor.anchor(), position = cursor.position();
    const bool selected = cursor.hasSelection();
    if (!selected) cursor.select(QTextCursor::Document);
    cursor.beginEditBlock();
    cursor.mergeCharFormat(format);
    cursor.endEditBlock();
    if (!selected) {
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(format); // Also set the insertion format for empty boxes.
    }
    delegated()->setTextCursor(cursor);
    if (!mSizeControl || !mSizeControl->hasFocus()) delegated()->setFocus();
    delegated()->contentsChanged();
    positionHandles();
}

void UBGraphicsTextItemDelegate::applyAlignment(Qt::Alignment alignment)
{
    if (!isEditable() || isLocked()) return;
    QTextCursor cursor = delegated()->textCursor();
    const int anchor = cursor.anchor(), position = cursor.position();
    const bool selected = cursor.hasSelection();
    if (!selected) cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setAlignment(alignment);
    cursor.beginEditBlock();
    cursor.mergeBlockFormat(format);
    cursor.endEditBlock();
    if (!selected) {
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
    }
    delegated()->setTextCursor(cursor);
    delegated()->setFocus();
    delegated()->contentsChanged();
    updateFormattingControls();
}

void UBGraphicsTextItemDelegate::updateFormattingControls()
{
    if (!mTextToolbar || !mFontControl || !mSizeControl) return;
    const QTextCharFormat format = delegated()->textCursor().charFormat();
    const QFont font = format.font();
    const bool enabled = isEditable() && !isLocked();
    const QList<QWidget*> controls {mFontControl, mSizeControl, mColorControl, mBoldControl, mItalicControl, mUnderlineControl, mAlignControl};
    for (auto* control : controls) control->setEnabled(enabled);
    const QString family = font.family().isEmpty() ? delegated()->font().family() : font.family();
    mFontControl->setText(QFontMetrics(mFontControl->font()).elidedText(family, Qt::ElideRight, 96));
    mFontControl->setToolTip(QStringLiteral("字体：%1").arg(family));
    if (!mSizeControl->hasFocus()) {
        const QSignalBlocker blocker(mSizeControl);
        const qreal size = font.pointSizeF() > 0 ? font.pointSizeF() : delegated()->font().pointSizeF();
        mSizeControl->setValue(size > 0 ? size : 12);
    }
    mBoldControl->setChecked(font.bold());
    mItalicControl->setChecked(font.italic());
    mUnderlineControl->setChecked(font.underline());
    QColor color = format.foreground().style() != Qt::NoBrush ? format.foreground().color() : delegated()->defaultTextColor();
    mColorControl->setIcon(UBUiTheme::icon("textcolor", color));
    const Qt::Alignment alignment = delegated()->textCursor().blockFormat().alignment();
    const QString icon = alignment.testFlag(Qt::AlignHCenter) ? "aligncenter"
        : alignment.testFlag(Qt::AlignRight) ? "alignright" : "alignleft";
    mAlignControl->setIcon(UBUiTheme::icon(icon));
    for (auto* action : mAlignControl->menu()->actions())
        action->setChecked(action->objectName() == icon);
}

void UBGraphicsTextItemDelegate::saveTextCursorFormats()
{
    // No HTML roundtrip: it would reset QTextDocument's local undo history.
    updateFormattingControls();
}

void UBGraphicsTextItemDelegate::openMenu(const QPoint& globalPosition)
{
    if (!mMenu) {
        mMenu = new QMenu(UBApplication::boardController->controlView());
        // The view can be destroyed before the cached scene/delegate at exit.
        connect(mMenu, &QObject::destroyed, this, [this] { mMenu = nullptr; });
        mMenu->setObjectName(QStringLiteral("textOperationsMenu"));
        mMenu->setStyleSheet(textMenuStyle());
        auto* edit = mMenu->addAction(UBUiTheme::icon("rename"), QStringLiteral("编辑文字"), this, [this] {
            if (isEditable() && !isLocked()) {
                delegated()->setTextInteractionFlags(Qt::TextEditorInteraction);
                delegated()->setFocus();
                QTextCursor cursor = delegated()->textCursor();
                cursor.select(QTextCursor::Document);
                delegated()->setTextCursor(cursor);
            }
        });
        edit->setObjectName(QStringLiteral("textEdit"));
        auto* size = mMenu->addMenu(UBUiTheme::icon("text"), QStringLiteral("调整字号"));
        size->menuAction()->setObjectName(QStringLiteral("textSizeMenu"));
        size->addAction(UBUiTheme::icon("plus"), QStringLiteral("增大字号"), this, &UBGraphicsTextItemDelegate::increaseSize);
        size->addAction(UBUiTheme::icon("minus"), QStringLiteral("减小字号"), this, &UBGraphicsTextItemDelegate::decreaseSize);
        mMenu->addAction(UBUiTheme::icon("copy"), QStringLiteral("复制文本框"), this, &UBGraphicsItemDelegate::duplicate);
        auto* arrange = mMenu->addMenu(UBUiTheme::icon("layers"), QStringLiteral("排列图层"));
        arrange->setObjectName(QStringLiteral("textArrange"));
        arrange->menuAction()->setObjectName(QStringLiteral("textArrange"));
        arrange->addAction(QStringLiteral("上移一层"), this, &UBGraphicsItemDelegate::increaseZLevelUp);
        arrange->addAction(QStringLiteral("下移一层"), this, &UBGraphicsItemDelegate::increaseZLevelDown);
        arrange->addSeparator();
        arrange->addAction(QStringLiteral("置于顶层"), this, &UBGraphicsItemDelegate::increaseZlevelTop);
        arrange->addAction(QStringLiteral("置于底层"), this, &UBGraphicsItemDelegate::increaseZlevelBottom);
        mMenu->addAction(UBUiTheme::icon("rotate"), QStringLiteral("恢复水平角度"), this, [this] {
            if (isLocked()) return;
            startUndoStep();
            mDelegated->setTransform(UBImageTransform::rotated(mDelegated->boundingRect(), mDelegated->transform(),
                    -UBImageTransform::angle(mDelegated->transform())));
            commitUndoStep();
            positionHandles();
        })->setObjectName(QStringLiteral("textResetRotation"));
        mMenu->addSeparator();
        decorateMenu(mMenu);
        mMenu->addSeparator();
        mMenu->addAction(UBUiTheme::icon("trash"), QStringLiteral("删除文本框"), this, [this] { remove(); });
    }
    updateMenuActionState();
    mMenu->exec(globalPosition);
}

UBGraphicsTextItem* UBGraphicsTextItemDelegate::delegated()
{
    return static_cast<UBGraphicsTextItem*>(mDelegated);
}

void UBGraphicsTextItemDelegate::setEditable(bool editable)
{
    mDelegated->setData(UBGraphicsItemData::ItemEditable, editable);
    if (!editable) {
        QTextCursor cursor = delegated()->textCursor();
        cursor.clearSelection();
        delegated()->setTextCursor(cursor);
    }
    delegated()->setTextInteractionFlags(editable && !isLocked() ? Qt::TextEditorInteraction : Qt::NoTextInteraction);
    updateFormattingControls();
}

void UBGraphicsTextItemDelegate::remove(bool canUndo)
{
    freeControls();
    UBGraphicsItemDelegate::remove(canUndo);
}

void UBGraphicsTextItemDelegate::decorateMenu(QMenu* menu)
{
    UBGraphicsItemDelegate::decorateMenu(menu);
    mLockAction->setIcon(UBUiTheme::icon("lock"));
    mShowOnDisplayAction->setIcon(UBUiTheme::icon("eye"));
    mEditableAction = menu->addAction(QStringLiteral("允许编辑文字"), this, &UBGraphicsTextItemDelegate::setEditable);
    mEditableAction->setCheckable(true);
}

void UBGraphicsTextItemDelegate::updateMenuActionState()
{
    UBGraphicsItemDelegate::updateMenuActionState();
    if (!mMenu) return;
    mLockAction->setText(isLocked() ? QStringLiteral("解除锁定") : QStringLiteral("锁定文本框"));
    if (mEditableAction) mEditableAction->setChecked(isEditable());
    for (auto* action : mMenu->actions()) {
        if (action->objectName() == "textSizeMenu") action->setEnabled(isEditable() && !isLocked());
        if (action->objectName() == "textArrange" || action->objectName() == "textResetRotation")
            action->setEnabled(!isLocked());
        if (action->objectName() == "textEdit")
            action->setEnabled(isEditable() && !isLocked());
    }
    updateFormattingControls();
}

bool UBGraphicsTextItemDelegate::isEditable()
{
    return mDelegated->data(UBGraphicsItemData::ItemEditable).toBool();
}

bool UBGraphicsTextItemDelegate::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    return UBGraphicsItemDelegate::mousePressEvent(event);
}

bool UBGraphicsTextItemDelegate::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    return UBGraphicsItemDelegate::mouseMoveEvent(event);
}

bool UBGraphicsTextItemDelegate::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    updateFormattingControls();
    return UBGraphicsItemDelegate::mouseReleaseEvent(event);
}

bool UBGraphicsTextItemDelegate::keyPressEvent(QKeyEvent* event)
{
    if (isLocked() || !isEditable()) {
        event->accept();
        return false;
    }
    return true;
}

bool UBGraphicsTextItemDelegate::keyReleaseEvent(QKeyEvent*)
{
    updateFormattingControls();
    return true;
}

QVariant UBGraphicsTextItemDelegate::itemChange(QGraphicsItem::GraphicsItemChange change, const QVariant& value)
{
    return UBGraphicsItemDelegate::itemChange(change, value);
}

void UBGraphicsTextItemDelegate::changeTextSize(qreal factor, textChangeMode mode)
{
    // Saved pixels-per-point ratios have finite decimal precision. Keep the
    // legacy hundredth tolerance so same-platform reloads do not resize text
    // merely because 1.333333... was stored as 1.33333.
    if (mode == scaleSize) factor = std::round(factor*100)/100;
    if (isLocked() || (mode == changeSize && !isEditable())
        || !std::isfinite(factor) || (mode == scaleSize ? factor <= 0 || factor == 1 : factor == 0)) return;
    QTextCursor cursor = delegated()->textCursor();
    const int anchor = cursor.anchor(), position = cursor.position();
    const bool selected = cursor.hasSelection();
    if (!selected) cursor.select(QTextCursor::Document);
    const int start = cursor.selectionStart(), end = cursor.selectionEnd();
    struct FontRange { int start, end; qreal size; };
    QList<FontRange> ranges;
    const auto pointSize = [this](const QTextCharFormat& format) {
        qreal size = format.font().pointSizeF();
        if (size <= 0) size = delegated()->font().pointSizeF();
        return size > 0 ? size : qreal(12);
    };
    // Collect first; editing invalidates document fragment iterators.
    for (QTextBlock block = delegated()->document()->findBlock(start);
         block.isValid() && block.position() < end; block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int first = qMax(start, fragment.position());
            const int last = qMin(end, fragment.position()+fragment.length());
            if (first < last) {
                const qreal size = pointSize(fragment.charFormat());
                ranges.append({first,last,qMax(qreal(1),mode == changeSize ? size+factor : size*factor)});
            }
        }
    }
    cursor.beginEditBlock();
    for (const auto& range : ranges) {
        cursor.setPosition(range.start);
        cursor.setPosition(range.end,QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setFontPointSize(range.size);
        cursor.mergeCharFormat(format);
    }
    cursor.endEditBlock();
    cursor.setPosition(anchor);
    cursor.setPosition(position,QTextCursor::KeepAnchor);
    if (ranges.isEmpty()) {
        const qreal size = pointSize(cursor.charFormat());
        QTextCharFormat format;
        format.setFontPointSize(qMax(qreal(1),mode == changeSize ? size+factor : size*factor));
        cursor.mergeCharFormat(format);
    }
    delegated()->setTextCursor(cursor);
    UBSettings::settings()->setFontPointSize(qRound(pointSize(cursor.charFormat())));
    delegated()->setFocus();
    saveTextCursorFormats();
}

void UBGraphicsTextItemDelegate::recolor()
{
    QTextCursor cursor = delegated()->textCursor();
    QTextCharFormat textFormat;

    int anchorPos = cursor.anchor();
    int cursorPos = cursor.position();

    if (0 == anchorPos-cursorPos)
    {
        // If nothing is selected, then we select all the text
        cursor.setPosition (0, QTextCursor::MoveAnchor);
        cursor.setPosition (cursor.document()->characterCount()-1, QTextCursor::KeepAnchor);
    }

    int startPos = qMin(cursor.anchor(), cursor.position());
    int endPos = qMax(cursor.anchor(), cursor.position());

    QFont curFont;
    QFont nextCharFont;
    bool bEndofTheSameBlock;
    int iBlockLen;
    int iCursorPos = startPos;
    QBrush curBrush;
    QBrush nextCharBrush;

    cursor.setPosition (startPos, QTextCursor::MoveAnchor);
    while(iCursorPos < endPos)
    {
        bEndofTheSameBlock = false;
        iBlockLen = 0;

        // Here we get the point size of the first character
        cursor.setPosition (iCursorPos+1, QTextCursor::KeepAnchor);
        curBrush = cursor.charFormat().foreground();

        // Then we position the end cursor to the start cursor position
        cursor.setPosition (iCursorPos, QTextCursor::KeepAnchor);

        do
        {
            cursor.setPosition (iCursorPos+iBlockLen+1, QTextCursor::KeepAnchor);
            nextCharBrush = cursor.charFormat().foreground();

            if (curBrush != nextCharBrush || (iCursorPos+iBlockLen >= endPos))
            {
                bEndofTheSameBlock = true;
                break;
            }

            iBlockLen++;

        }while(!bEndofTheSameBlock);


        //setting new parameters
        if (delegated()->scene()->isDarkBackground())
        {
            if (curBrush.color() == Qt::black)
            {
                curBrush = QBrush(Qt::white);
            }
        }
        else
        {
            if (curBrush.color() == Qt::white)
            {
                curBrush = QBrush(Qt::black);
            }
        }

        cursor.setPosition (iCursorPos+iBlockLen, QTextCursor::KeepAnchor);
        textFormat.setForeground(curBrush);
        cursor.mergeCharFormat(textFormat);

        iCursorPos += iBlockLen;
        cursor.setPosition (iCursorPos, QTextCursor::MoveAnchor);

        curFont = nextCharFont;
    }

    delegated()->setFont(curFont);
    //returning initial selection
    cursor.setPosition (anchorPos, QTextCursor::MoveAnchor);
    cursor.setPosition (cursorPos, QTextCursor::KeepAnchor);

    delegated()->setTextCursor(cursor);
    saveTextCursorFormats();
}
