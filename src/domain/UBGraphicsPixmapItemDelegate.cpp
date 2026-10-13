#include "UBGraphicsPixmapItemDelegate.h"
#include "UBGraphicsPixmapItem.h"
#include "UBGraphicsDelegateFrame.h"
#include "UBGraphicsScene.h"
#include "UBImageTransform.h"
#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "board/UBDrawingController.h"
#include "core/UBApplication.h"
#include "gui/UBUiTheme.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsProxyWidget>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
// Separate from the legacy frame so text, video, widgets and group editing
// keep their established behavior. Everything here remains a Control layer.
class ImageFrame final : public UBGraphicsDelegateFrame
{
public:
    explicit ImageFrame(UBGraphicsPixmapItemDelegate* delegate)
        : UBGraphicsDelegateFrame(delegate, {}, 1), mImageDelegate(delegate)
    {
        for (auto* child : childItems()) child->hide();
        resetTransform();
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        setAcceptHoverEvents(true);
        setToolTip(QStringLiteral("拖动图片移动；四角等比缩放；按住 Alt 自由缩放；Shift 旋转以 15° 吸附"));
    }

    void setAntiScale(qreal scale) override { mUnit = scale; }

    void positionHandles() override
    {
        auto* item = mImageDelegate->delegated();
        if (!item) return;
        mOutline = item->mapToScene(QPolygonF(item->boundingRect()));
        const QRectF bounds = item->boundingRect();
        for (int i = 0; i < 8; ++i)
            mHandles[i] = item->mapToScene(UBImageTransform::point(bounds, i));
        const QPointF center = item->mapToScene(bounds.center());
        QPointF normal = mHandles[UBImageTransform::Top] - center;
        const qreal length = std::hypot(normal.x(), normal.y());
        mRotate = mHandles[UBImageTransform::Top]
                + (length > 0 ? normal / length : QPointF(0, -1)) * 28 * mUnit;
        QRectF area = mOutline.boundingRect();
        area |= QRectF(mRotate - QPointF(12*mUnit, 12*mUnit), QSizeF(24*mUnit, 24*mUnit));
        setRect(area.adjusted(-12*mUnit, -12*mUnit, 12*mUnit, 12*mUnit));
        setZValue(1010000.0); // Control layer only; never lift image content.
        update();
    }

    QPainterPath shape() const override
    {
        QPainterPath border;
        border.addPolygon(mOutline);
        border.closeSubpath();
        QPainterPathStroker stroker;
        stroker.setWidth(6*mUnit);
        QPainterPath result = stroker.createStroke(border);
        if (!mImageDelegate->isLocked()) {
            for (int i = 0; i < 8; ++i)
                if (handleVisible(i)) result.addRect(hitRect(mHandles[i]));
            result.addEllipse(hitRect(mRotate));
        }
        return result;
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen pen(UBUiTheme::blue(), 1.5*mUnit);
        if (mImageDelegate->isLocked()) pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(mOutline);
        if (!mImageDelegate->isLocked()) {
            painter->drawLine(mHandles[UBImageTransform::Top], mRotate);
            painter->setBrush(Qt::white);
            for (int i = 0; i < 8; ++i) {
                if (!handleVisible(i)) continue;
                const QPointF p = mHandles[i];
                painter->drawRoundedRect(QRectF(p.x()-4*mUnit, p.y()-4*mUnit, 8*mUnit, 8*mUnit), mUnit, mUnit);
            }
            painter->drawEllipse(mRotate, 7*mUnit, 7*mUnit);
            // A small rotation arrow, readable without relying on color.
            painter->drawArc(QRectF(mRotate.x()-4*mUnit, mRotate.y()-4*mUnit, 8*mUnit, 8*mUnit), 20*16, 260*16);
        }
        painter->restore();
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override
    {
        const int tool = toolAt(event->scenePos());
        if (tool == 8) setCursor(Qt::OpenHandCursor);
        else if (tool < 0) setCursor(mImageDelegate->isLocked() ? Qt::ArrowCursor : Qt::SizeAllCursor);
        else {
            const auto* item = mImageDelegate->delegated();
            const QRectF r = item->boundingRect();
            const QPointF vector = item->mapToScene(UBImageTransform::point(r, tool))
                    - item->mapToScene(UBImageTransform::point(r, (tool+4)%8));
            const int sector = (qRound(qRadiansToDegrees(std::atan2(vector.y(), vector.x()))/45) % 4 + 4) % 4;
            const Qt::CursorShape cursors[] = {Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor};
            setCursor(cursors[sector]);
        }
        event->accept();
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (mImageDelegate->isLocked()) { event->accept(); return; }
        auto* item = mImageDelegate->delegated();
        mTool = toolAt(event->scenePos());
        mInitial = item->transform();
        mInitialPos = item->pos();
        mInitialInverse = item->sceneTransform().inverted(&mInvertible);
        mPress = event->scenePos();
        mParentInverse = item->parentItem() ? item->parentItem()->sceneTransform().inverted() : QTransform();
        mCenter = mInitial.map(item->boundingRect().center()) + mInitialPos;
        mImageDelegate->startUndoStep();
        mImageDelegate->setInteractionActive(true);
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (mTool == -2 || !mInvertible || mImageDelegate->isLocked()) { event->accept(); return; }
        auto* item = mImageDelegate->delegated();
        const QRectF bounds = item->boundingRect();
        if (mTool == 8) {
            const QPointF start = mParentInverse.map(mPress) - mCenter;
            const QPointF end = mParentInverse.map(event->scenePos()) - mCenter;
            if (std::hypot(end.x(), end.y()) > mUnit) {
                qreal delta = UBImageTransform::normalizeAngle(qRadiansToDegrees(std::atan2(end.y(), end.x()) - std::atan2(start.y(), start.x())));
                if (event->modifiers().testFlag(Qt::ShiftModifier))
                    delta = qRound((UBImageTransform::angle(mInitial)+delta)/15)*15 - UBImageTransform::angle(mInitial);
                item->setTransform(UBImageTransform::rotated(bounds, mInitial, delta));
            }
        } else if (mTool >= 0) {
            const bool corner = mTool % 2 == 0;
            const bool ratio = (corner && !event->modifiers().testFlag(Qt::AltModifier))
                    || event->modifiers().testFlag(Qt::ShiftModifier);
            const QPointF delta = mInitialInverse.map(event->scenePos()) - mInitialInverse.map(mPress);
            const qreal width = QLineF(mInitialInverse.inverted().map(bounds.topLeft()), mInitialInverse.inverted().map(bounds.topRight())).length();
            const qreal height = QLineF(mInitialInverse.inverted().map(bounds.topLeft()), mInitialInverse.inverted().map(bounds.bottomLeft())).length();
            item->setTransform(UBImageTransform::resized(bounds, mInitial, mTool, delta, ratio,
                12*mUnit/std::max(width, 0.001), 12*mUnit/std::max(height, 0.001)));
        } else {
            item->setPos(mInitialPos + mParentInverse.map(event->scenePos()) - mParentInverse.map(mPress));
        }
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (mTool != -2) {
            mTool = -2;
            mImageDelegate->commitUndoStep();
            mImageDelegate->setInteractionActive(false);
        }
        event->accept();
    }

private:
    QRectF hitRect(const QPointF& point) const
    { return {point.x()-10*mUnit, point.y()-10*mUnit, 20*mUnit, 20*mUnit}; }
    bool handleVisible(int i) const
    {
        if (i % 2 == 0) return true;
        const int a = (i+7)%8, b = (i+1)%8;
        return QLineF(mHandles[a], mHandles[b]).length() > 42*mUnit;
    }
    int toolAt(const QPointF& scenePoint) const
    {
        if (mImageDelegate->isLocked()) return -2;
        if (hitRect(mRotate).contains(scenePoint)) return 8;
        for (int i : {0, 2, 4, 6, 1, 3, 5, 7})
            if (handleVisible(i) && hitRect(mHandles[i]).contains(scenePoint)) return i;
        return -1;
    }
    UBGraphicsPixmapItemDelegate* mImageDelegate;
    qreal mUnit = 1;
    QPolygonF mOutline;
    QPointF mHandles[8], mRotate, mPress, mInitialPos, mCenter;
    QTransform mInitial, mInitialInverse, mParentInverse;
    int mTool = -2;
    bool mInvertible = false;
};
}

UBGraphicsPixmapItemDelegate::UBGraphicsPixmapItemDelegate(UBGraphicsPixmapItem* item)
    : UBGraphicsItemDelegate(item, nullptr, GF_COMMON | GF_FLIPPABLE_ALL_AXIS | GF_REVOLVABLE | GF_RESPECT_RATIO)
{
}

UBGraphicsPixmapItemDelegate::~UBGraphicsPixmapItemDelegate()
{
    // Independent controls must not outlive their image or remain in a scene
    // during its destruction. QPointer also handles scene-owned deletion.
    if (mImageToolbar) delete mImageToolbar.data();
    delete mimeData(); // Only a pending Alt-press; completed drags clear it.
    delete mMenu;
    mMenu = nullptr;
}

void UBGraphicsPixmapItemDelegate::createActions()
{
    if (mGeometryAction) return;
    mGeometryAction = new QAction(UBUiTheme::icon("size"), QStringLiteral("大小与旋转…"), this);
    connect(mGeometryAction, &QAction::triggered, this, [this] { editGeometry(); });
    mRotateAction = new QAction(UBUiTheme::icon("rotate"), QStringLiteral("向右旋转 90°"), this);
    connect(mRotateAction, &QAction::triggered, this, [this] { rotate(90); });
    mCopyAction = new QAction(UBUiTheme::icon("copy"), QStringLiteral("复制图片"), this);
    connect(mCopyAction, &QAction::triggered, this, [this] { duplicate(); });
}

void UBGraphicsPixmapItemDelegate::createControls()
{
    if (!mFrame) {
        mFrame = new ImageFrame(this);
        mFrame->hide();
    }
    createActions();
    if (!mImageToolbar) {
        auto* panel = new QWidget;
        panel->setObjectName(QStringLiteral("imageContextToolbar"));
        panel->setStyleSheet(QStringLiteral(
            "QWidget#imageContextToolbar {background:#ffffff; color:#202936; border:1px solid #dbe2eb; border-radius:10px;}"
            "QLabel {border:0; color:#526174; background:transparent;}"
            "QToolButton {color:#202936; background:transparent; border:1px solid transparent; border-radius:7px; padding:0 7px;}"
            "QToolButton:hover, QToolButton:pressed {background:#eaf1ff; border-color:#b9cff6; color:#1d4ed8;}"
            "QToolButton:focus {border-color:#2563eb;}"
            "QToolButton:disabled {color:#a1adbd;}"));
        panel->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
        auto* row = new QHBoxLayout(panel);
        row->setContentsMargins(6, 4, 6, 4);
        row->setSpacing(4);
        auto* title = new QLabel(QStringLiteral("图片"), panel);
        row->addWidget(title);
        const auto addButton = [&](QAction* action, bool text) {
            auto* button = new QToolButton(panel);
            button->setDefaultAction(action);
            button->setIconSize(QSize(20, 20));
            button->setFixedHeight(44);
            button->setMinimumWidth(text ? 68 : 44);
            button->setToolButtonStyle(text ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
            button->setCursor(Qt::ArrowCursor);
            button->setAccessibleName(action->text());
            row->addWidget(button);
            return button;
        };
        auto* sizeButton = addButton(mGeometryAction, true);
        sizeButton->setText(QStringLiteral("尺寸"));
        addButton(mRotateAction, false);
        addButton(mCopyAction, false);
        auto* more = new QToolButton(panel);
        more->setText(QStringLiteral("更多"));
        more->setIcon(UBUiTheme::icon("more"));
        more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        more->setIconSize(QSize(20, 20));
        more->setFixedSize(74, 44);
        more->setCursor(Qt::ArrowCursor);
        more->setAccessibleName(QStringLiteral("图片更多操作"));
        connect(more, &QToolButton::clicked, this, [this, more] { openMenu(more->mapToGlobal(QPoint(0, more->height()))); });
        row->addWidget(more);
        panel->adjustSize();
        mImageToolbar = new QGraphicsProxyWidget;
        mImageToolbar->setWidget(panel);
        mImageToolbar->setData(UBGraphicsItemData::ItemLayerType, UBItemLayerType::Control);
        mImageToolbar->setData(UBGraphicsItemData::itemLayerType, itemLayerType::SelectionFrame);
        mImageToolbar->hide();
    }
}

void UBGraphicsPixmapItemDelegate::freeControls()
{
    if (mImageToolbar) {
        if (mImageToolbar->scene()) mImageToolbar->scene()->removeItem(mImageToolbar);
        mImageToolbar->hide();
        mImageToolbar->deleteLater();
        mImageToolbar = nullptr;
    }
    if (mFrame) {
        if (mFrame->scene()) mFrame->scene()->removeItem(mFrame);
        mFrame->hide();
        mFrame->deleteLater();
        mFrame = nullptr;
    }
    mInteracting = mDragging = false;
}

bool UBGraphicsPixmapItemDelegate::controlsExist() const
{
    return mFrame && mImageToolbar;
}

void UBGraphicsPixmapItemDelegate::positionHandles()
{
    if (!controlsExist() || !mDelegated->scene()) return;
    auto* scene = mDelegated->scene();
    const bool editable = mDelegated->isSelected() && delegated() == mDelegated
            && UBDrawingController::drawingController()->stylusTool() != UBStylusTool::Play;
    if (!mFrame->scene()) scene->addItem(mFrame);
    if (!mImageToolbar->scene()) scene->addItem(mImageToolbar);
    mFrame->setAntiScale(mAntiScaleRatio);
    mFrame->positionHandles();
    mFrame->QGraphicsRectItem::setVisible(editable);
    mImageToolbar->setVisible(editable && !mInteracting);
    mImageToolbar->setScale(mAntiScaleRatio);
    mImageToolbar->setZValue(1010001.0);
    mGeometryAction->setEnabled(!isLocked());
    mRotateAction->setEnabled(!isLocked());
    const QRectF image = mDelegated->sceneBoundingRect();
    const QSizeF toolbar = mImageToolbar->boundingRect().size() * mAntiScaleRatio;
    auto* view = UBApplication::boardController->controlView();
    const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
    qreal x = image.center().x()-toolbar.width()/2;
    qreal y = image.top()-toolbar.height()-42*mAntiScaleRatio;
    if (y < visible.top()+8*mAntiScaleRatio) y = image.bottom()+18*mAntiScaleRatio;
    x = qBound(visible.left()+8*mAntiScaleRatio, x, std::max(visible.left()+8*mAntiScaleRatio, visible.right()-toolbar.width()-8*mAntiScaleRatio));
    y = qBound(visible.top()+8*mAntiScaleRatio, y, std::max(visible.top()+8*mAntiScaleRatio, visible.bottom()-toolbar.height()-8*mAntiScaleRatio));
    mImageToolbar->setPos(x, y);
}

void UBGraphicsPixmapItemDelegate::setInteractionActive(bool active)
{
    mInteracting = active;
    positionHandles();
}

bool UBGraphicsPixmapItemDelegate::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) { event->accept(); return true; }
    UBGraphicsItemDelegate::mousePressEvent(event);
    mDragging = !isLocked() && delegated() == mDelegated;
    mPressScene = event->scenePos();
    mPressPosition = mDelegated->pos();
    mDelegated->setFlag(QGraphicsItem::ItemIsFocusable, true);
    mDelegated->setFocus();
    event->accept();
    return true;
}

bool UBGraphicsPixmapItemDelegate::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (mimeData() && event->modifiers().testFlag(Qt::AltModifier)) {
        const bool handled = UBGraphicsItemDelegate::mouseMoveEvent(event);
        setMimeData(nullptr); // QDrag owns it now; never reuse or delete it.
        mDragging = false;
        setInteractionActive(false);
        return handled;
    }
    if (mDragging) {
        setInteractionActive(true);
        auto* parent = mDelegated->parentItem();
        const QPointF delta = parent ? parent->mapFromScene(event->scenePos())-parent->mapFromScene(mPressScene)
                                     : event->scenePos()-mPressScene;
        mDelegated->setPos(mPressPosition+delta);
    }
    event->accept();
    return true;
}

bool UBGraphicsPixmapItemDelegate::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) { event->accept(); return true; }
    delete mimeData();
    setMimeData(nullptr);
    mDragging = false;
    setInteractionActive(false);
    return UBGraphicsItemDelegate::mouseReleaseEvent(event);
}

bool UBGraphicsPixmapItemDelegate::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)
            || event->modifiers().testFlag(Qt::AltModifier)) return false;
    QPointF delta;
    const qreal step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
    switch (event->key()) {
    case Qt::Key_Left: delta.setX(-step); break;
    case Qt::Key_Right: delta.setX(step); break;
    case Qt::Key_Up: delta.setY(-step); break;
    case Qt::Key_Down: delta.setY(step); break;
    default: return false;
    }
    if (!isLocked()) {
        startUndoStep();
        mDelegated->setPos(mDelegated->pos()+delta);
        commitUndoStep();
    }
    event->accept();
    return true;
}

void UBGraphicsPixmapItemDelegate::remove(bool canUndo)
{
    freeControls();
    UBGraphicsItemDelegate::remove(canUndo);
}

void UBGraphicsPixmapItemDelegate::applyTransform(const QTransform& transform)
{
    if (isLocked()) return;
    startUndoStep();
    mDelegated->setTransform(transform);
    commitUndoStep();
    positionHandles();
}

void UBGraphicsPixmapItemDelegate::rotate(qreal degrees)
{
    applyTransform(UBImageTransform::rotated(mDelegated->boundingRect(), mDelegated->transform(), degrees));
}

void UBGraphicsPixmapItemDelegate::createMenu()
{
    if (mMenu) return;
    createActions();
    mMenu = new QMenu(UBApplication::boardController->controlView());
    mMenu->setObjectName(QStringLiteral("imageOperationsMenu"));
    mMenu->setStyleSheet(QStringLiteral(
        "QMenu {background:#ffffff; color:#202936; border:1px solid #dbe2eb; border-radius:9px; padding:6px;}"
        "QMenu::item {background:transparent; color:#202936; padding:9px 30px 9px 28px; border-radius:6px;}"
        "QMenu::item:selected {background:#eaf1ff; color:#1d4ed8;}"
        "QMenu::item:disabled {color:#a1adbd;}"
        "QMenu::separator {height:1px; background:#e5eaf1; margin:4px 8px;}"));
    mMenu->addAction(mGeometryAction);
    auto* rotation = mMenu->addMenu(UBUiTheme::icon("rotate"), QStringLiteral("旋转与翻转"));
    rotation->addAction(mRotateAction);
    rotation->addAction(QStringLiteral("向左旋转 90°"), this, [this] { rotate(-90); });
    rotation->addSeparator();
    rotation->addAction(UBUiTheme::icon("flip"), QStringLiteral("水平翻转"), this, [this] {
        applyTransform(UBImageTransform::flipped(mDelegated->boundingRect(), mDelegated->transform(), true)); });
    rotation->addAction(UBUiTheme::icon("flip"), QStringLiteral("垂直翻转"), this, [this] {
        applyTransform(UBImageTransform::flipped(mDelegated->boundingRect(), mDelegated->transform(), false)); });
    rotation->addAction(QStringLiteral("恢复水平角度"), this, [this] { rotate(-UBImageTransform::angle(mDelegated->transform())); });
    auto* arrange = mMenu->addMenu(UBUiTheme::icon("layers"), QStringLiteral("排列图层"));
    arrange->addAction(QStringLiteral("放到笔迹下方"), this, [this] {
        if (auto scene = castUBGraphicsScene()) scene->arrangeImage(mDelegated, UBImageLayerPolicy::BelowInk); });
    arrange->addAction(QStringLiteral("覆盖已有笔迹"), this, [this] {
        if (auto scene = castUBGraphicsScene()) scene->arrangeImage(mDelegated, UBImageLayerPolicy::AboveInk); });
    arrange->addSeparator();
    arrange->addAction(QStringLiteral("上移一层"), this, [this] { increaseZLevelUp(); });
    arrange->addAction(QStringLiteral("下移一层"), this, [this] { increaseZLevelDown(); });
    arrange->addSeparator();
    arrange->addAction(QStringLiteral("置于顶层"), this, [this] { increaseZlevelTop(); });
    arrange->addAction(QStringLiteral("置于底层"), this, [this] { increaseZlevelBottom(); });
    mMenu->addSeparator();
    mMenu->addAction(mCopyAction);
    UBGraphicsItemDelegate::decorateMenu(mMenu);
    mLockAction->setIcon(UBUiTheme::icon("lock"));
    mShowOnDisplayAction->setIcon(UBUiTheme::icon("eye"));
    if (mSetAsBackgroundAction) mSetAsBackgroundAction->setIcon(UBUiTheme::icon("picture"));
    mMenu->addSeparator();
    mMenu->addAction(UBUiTheme::icon("trash"), QStringLiteral("删除图片"), this, [this] { remove(); });
}

void UBGraphicsPixmapItemDelegate::openMenu(const QPoint& globalPosition)
{
    createMenu();
    updateMenuActionState();
    mLockAction->setText(isLocked() ? QStringLiteral("解除锁定") : QStringLiteral("锁定图片"));
    mGeometryAction->setEnabled(!isLocked());
    mRotateAction->setEnabled(!isLocked());
    for (QAction* action : mMenu->actions())
        if (action->menu()) action->setEnabled(!isLocked());
    mMenu->exec(globalPosition);
}

void UBGraphicsPixmapItemDelegate::editGeometry()
{
    if (isLocked()) return;
    const QRectF bounds = mDelegated->boundingRect();
    if (bounds.isEmpty()) return;
    const QTransform initial = mDelegated->transform();
    const qreal oldWidth = QLineF(initial.map(bounds.topLeft()), initial.map(bounds.topRight())).length();
    const qreal oldHeight = QLineF(initial.map(bounds.topLeft()), initial.map(bounds.bottomLeft())).length();
    if (oldWidth <= 0 || oldHeight <= 0) return;
    QDialog dialog(UBApplication::boardController->controlView());
    dialog.setWindowTitle(QStringLiteral("图片大小与旋转"));
    dialog.setMinimumWidth(340);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog {background:#ffffff; color:#202936;}"
        "QLabel, QCheckBox {color:#202936; background:transparent;}"
        "QDoubleSpinBox {background:#ffffff; color:#202936; border:1px solid #cbd6e5; border-radius:6px; min-height:34px; padding:2px 8px;}"
        "QPushButton {background:#f8fafc; color:#202936; border:1px solid #cbd6e5; border-radius:7px; min-height:32px; padding:4px 16px;}"
        "QPushButton:hover {background:#eaf1ff; border-color:#9cbbfb;}"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 16);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    const auto spin = [&](qreal value, const QString& suffix) {
        auto* box = new QDoubleSpinBox(&dialog);
        box->setRange(0.1, 1000000);
        box->setDecimals(1);
        box->setSuffix(suffix);
        box->setValue(value);
        return box;
    };
    auto* width = spin(oldWidth, QStringLiteral(" px"));
    auto* height = spin(oldHeight, QStringLiteral(" px"));
    auto* angle = spin(0, QStringLiteral("°"));
    angle->setRange(-360, 360);
    angle->setValue(UBImageTransform::angle(initial));
    width->setAccessibleName(QStringLiteral("图片宽度"));
    height->setAccessibleName(QStringLiteral("图片高度"));
    angle->setAccessibleName(QStringLiteral("图片旋转角度"));
    form->addRow(QStringLiteral("宽度"), width);
    form->addRow(QStringLiteral("高度"), height);
    form->addRow(QStringLiteral("旋转角度"), angle);
    layout->addLayout(form);
    auto* keepRatio = new QCheckBox(QStringLiteral("锁定纵横比"), &dialog);
    keepRatio->setChecked(true);
    layout->addWidget(keepRatio);
    connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, [=](double value) {
        if (keepRatio->isChecked()) { QSignalBlocker guard(height); height->setValue(value*oldHeight/oldWidth); }
    });
    connect(height, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, [=](double value) {
        if (keepRatio->isChecked()) { QSignalBlocker guard(width); width->setValue(value*oldWidth/oldHeight); }
    });
    auto* hint = new QLabel(QStringLiteral("四角默认等比缩放，Alt 可自由拉伸。\n旋转时按住 Shift，可按 15° 吸附。"), &dialog);
    hint->setStyleSheet(QStringLiteral("color:#526174;"));
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    QTransform local;
    local.translate(bounds.center().x(), bounds.center().y());
    local.scale(width->value()/oldWidth, height->value()/oldHeight);
    local.translate(-bounds.center().x(), -bounds.center().y());
    QTransform resized = local * initial;
    applyTransform(UBImageTransform::rotated(bounds, resized, angle->value()-UBImageTransform::angle(initial)));
}
