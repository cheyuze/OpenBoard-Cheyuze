#include "UBGraphicsPixmapItemDelegate.h"
#include "UBGraphicsPixmapItem.h"
#include "UBGraphicsDelegateFrame.h"
#include "UBGraphicsScene.h"
#include "UBImageTransform.h"
#include "UBGraphicsStrokesGroup.h"
#include "UBLineTransform.h"
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
QLineF localLine(QGraphicsItem* item)
{return static_cast<UBGraphicsStrokesGroup*>(item)->editableLine();}

QLineF sceneLine(QGraphicsItem* item)
{
    const QLineF line=localLine(item);
    return QLineF(item->mapToScene(line.p1()),item->mapToScene(line.p2()));
}

QTransform lineTransform(QGraphicsItem* item,const QLineF& target)
{
    const QTransform parentInverse=item->parentItem()?item->parentItem()->sceneTransform().inverted():QTransform();
    const QTransform mapping=item->sceneTransform()*parentInverse;
    const QLineF line=localLine(item);
    const QLineF from(mapping.map(line.p1()),mapping.map(line.p2()));
    const QLineF to(parentInverse.map(target.p1()),parentInverse.map(target.p2()));
    // Keep position, transform origin and any separate rotation/scale intact.
    const QTransform tail=item->transform().inverted()*mapping;
    return mapping*UBLineTransform::between(from,to)*tail.inverted();
}

class LineFrame final : public UBGraphicsDelegateFrame
{
public:
    explicit LineFrame(UBGraphicsPixmapItemDelegate* delegate)
        : UBGraphicsDelegateFrame(delegate,{},1),mLineDelegate(delegate)
    {
        setObjectName(QStringLiteral("lineEndpointFrame"));
        for(auto* child:childItems())child->hide();
        resetTransform();setFlag(ItemIsSelectable,false);setFlag(ItemIsFocusable,true);setAcceptHoverEvents(true);
        setToolTip(QStringLiteral("拖动两端圆点改变直线长短和方向；拖动线身移动。选中端点后可用方向键微调，Shift 为 10 像素。"));
    }
    void setAntiScale(qreal scale) override {mUnit=scale;}
    void positionHandles() override
    {
        mLine=sceneLine(mLineDelegate->contentItem());
        setRect(QRectF(mLine.p1(),mLine.p2()).normalized().adjusted(-12*mUnit,-12*mUnit,12*mUnit,12*mUnit));
        setZValue(1010000);update();
    }
    QPainterPath shape() const override
    {
        QPainterPath path;path.moveTo(mLine.p1());path.lineTo(mLine.p2());
        QPainterPathStroker stroker;stroker.setWidth(10*mUnit);auto result=stroker.createStroke(path);
        if(!mLineDelegate->isLocked())for(auto point:{mLine.p1(),mLine.p2()})result.addEllipse(point,10*mUnit,10*mUnit);
        return result;
    }
    void paint(QPainter* painter,const QStyleOptionGraphicsItem*,QWidget*) override
    {
        painter->save();painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(UBUiTheme::blue(),mUnit,Qt::DashLine));painter->drawLine(mLine);
        if(!mLineDelegate->isLocked()){
            painter->setPen(QPen(UBUiTheme::blue(),1.5*mUnit));
            for(int i=0;i<2;++i){painter->setBrush(mSelected==i?UBUiTheme::blue():QColor(Qt::white));painter->drawEllipse(i?mLine.p2():mLine.p1(),5*mUnit,5*mUnit);}
        }
        painter->restore();
    }
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override
    {setCursor(mLineDelegate->isLocked()?Qt::ArrowCursor:handle(event->scenePos())>=0?Qt::CrossCursor:Qt::PointingHandCursor);event->accept();}
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if(event->button()!=Qt::LeftButton){event->ignore();return;}
        if(mLineDelegate->isLocked()){event->accept();return;}
        auto* item=mLineDelegate->contentItem();mHandle=handle(event->scenePos());mSelected=mHandle;setFocus();
        mInitial=item->transform();mInitialPos=item->pos();mInitialLine=sceneLine(item);mPress=event->scenePos();
        mParentInverse=item->parentItem()?item->parentItem()->sceneTransform().inverted():QTransform();
        mInitialMapping=item->sceneTransform()*mParentInverse;
        mTailInverse=(mInitial.inverted()*mInitialMapping).inverted();
        mDragging=true;mLineDelegate->startUndoStep();mLineDelegate->setInteractionActive(true);update();event->accept();
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override
    {
        if(!mDragging||mLineDelegate->isLocked()){event->accept();return;}
        if(QLineF(mPress,event->scenePos()).length()<.0001){event->accept();return;}
        auto* item=mLineDelegate->contentItem();
        if(mHandle<0)item->setPos(mInitialPos+mParentInverse.map(event->scenePos())-mParentInverse.map(mPress));
        else{
            const QPointF fixed=mHandle?mInitialLine.p1():mInitialLine.p2();
            QLineF radius(fixed,event->scenePos());
            if(radius.length()<2*mUnit){event->accept();return;}
            if(event->modifiers().testFlag(Qt::ShiftModifier))radius.setAngle(qRound(radius.angle()/15)*15);
            const QLineF target=mHandle?QLineF(fixed,radius.p2()):QLineF(radius.p2(),fixed);
            const QLineF from(mParentInverse.map(mInitialLine.p1()),mParentInverse.map(mInitialLine.p2()));
            const QLineF to(mParentInverse.map(target.p1()),mParentInverse.map(target.p2()));
            item->setTransform(mInitialMapping*UBLineTransform::between(from,to)*mTailInverse);
        }
        mLineDelegate->positionHandles();event->accept();
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        if(mDragging){mDragging=false;mLineDelegate->commitUndoStep();mLineDelegate->setInteractionActive(false);}
        event->accept();
    }
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override
    {mLineDelegate->openMenu(event->screenPos());event->accept();}
    void keyPressEvent(QKeyEvent* event) override
    {
        auto* item=mLineDelegate->contentItem();
        if(event->key()==Qt::Key_Escape&&mDragging){
            item->setTransform(mInitial);item->setPos(mInitialPos);mDragging=false;
            mLineDelegate->commitUndoStep();mLineDelegate->setInteractionActive(false);event->accept();return;
        }
        if(mSelected>=0&&!mDragging&&!mLineDelegate->isLocked()&&!(event->modifiers()&(Qt::ControlModifier|Qt::AltModifier))){
            QPointF offset;const qreal step=(event->modifiers().testFlag(Qt::ShiftModifier)?10:1)*mUnit;
            if(event->key()==Qt::Key_Left)offset.setX(-step);else if(event->key()==Qt::Key_Right)offset.setX(step);
            else if(event->key()==Qt::Key_Up)offset.setY(-step);else if(event->key()==Qt::Key_Down)offset.setY(step);
            if(!offset.isNull()){
                auto target=sceneLine(item);if(mSelected)target.setP2(target.p2()+offset);else target.setP1(target.p1()+offset);
                if(target.length()>=2*mUnit){mLineDelegate->startUndoStep();item->setTransform(lineTransform(item,target));mLineDelegate->commitUndoStep();mLineDelegate->positionHandles();}
                event->accept();return;
            }
        }
        if(!mLineDelegate->keyPressEvent(event))QGraphicsRectItem::keyPressEvent(event);
    }
private:
    int handle(const QPointF& point) const
    {
        const qreal first=QLineF(point,mLine.p1()).length(),second=QLineF(point,mLine.p2()).length();
        return qMin(first,second)>10*mUnit?-1:first<=second?0:1;
    }
    UBGraphicsPixmapItemDelegate* mLineDelegate;
    qreal mUnit=1;QLineF mLine,mInitialLine;QTransform mInitial,mParentInverse,mInitialMapping,mTailInverse;
    QPointF mPress,mInitialPos;int mHandle=-1,mSelected=-1;bool mDragging=false;
};

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
        setToolTip(QStringLiteral("拖动%1移动；四角等比缩放；按住 Alt 自由缩放；Shift 旋转以 15° 吸附").arg(delegate->objectLabel()));
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
        else if (tool < 0) setCursor(mImageDelegate->isLocked() ? Qt::ArrowCursor
            : dynamic_cast<UBGraphicsStrokesGroup*>(mImageDelegate->contentItem()) ? Qt::PointingHandCursor : Qt::SizeAllCursor);
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

UBGraphicsPixmapItemDelegate::UBGraphicsPixmapItemDelegate(QGraphicsItem* item, bool isShape)
    : UBGraphicsItemDelegate(item, nullptr, GF_COMMON | GF_FLIPPABLE_ALL_AXIS | GF_REVOLVABLE | GF_RESPECT_RATIO)
    , mIsShape(isShape)
{
}

QString UBGraphicsPixmapItemDelegate::objectLabel() const
{
    return isLine()?QStringLiteral("直线"):mIsShape ? QStringLiteral("图形") : QStringLiteral("图片");
}

bool UBGraphicsPixmapItemDelegate::isLine() const
{auto* group=dynamic_cast<UBGraphicsStrokesGroup*>(mDelegated);return group&&group->hasEditableLine();}

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
    mGeometryAction = new QAction(UBUiTheme::icon("size"), isLine()?QStringLiteral("端点位置…"):QStringLiteral("大小与旋转…"), this);
    connect(mGeometryAction, &QAction::triggered, this, [this] { editGeometry(); });
    mRotateAction = new QAction(UBUiTheme::icon("rotate"), QStringLiteral("向右旋转 90°"), this);
    connect(mRotateAction, &QAction::triggered, this, [this] { rotate(90); });
    mCopyAction = new QAction(UBUiTheme::icon("copy"), QStringLiteral("复制%1").arg(objectLabel()), this);
    connect(mCopyAction, &QAction::triggered, this, [this] { duplicate(); });
}

void UBGraphicsPixmapItemDelegate::createControls()
{
    if (!mFrame) {
        mFrame = isLine()?static_cast<UBGraphicsDelegateFrame*>(new LineFrame(this)):new ImageFrame(this);
        mFrame->hide();
    }
    createActions();
    if (!mImageToolbar) {
        auto* panel = new QWidget;
        panel->setObjectName(mIsShape ? QStringLiteral("shapeContextToolbar") : QStringLiteral("imageContextToolbar"));
        panel->setStyleSheet(QStringLiteral(
            "QWidget#imageContextToolbar, QWidget#shapeContextToolbar {background:#ffffff; color:#202936; border:1px solid #dbe2eb; border-radius:10px;}"
            "QLabel {border:0; color:#526174; background:transparent;}"
            "QToolButton {color:#202936; background:transparent; border:1px solid transparent; border-radius:7px; padding:0 7px;}"
            "QToolButton:hover, QToolButton:pressed {background:#eaf1ff; border-color:#b9cff6; color:#1d4ed8;}"
            "QToolButton:focus {border-color:#2563eb;}"
            "QToolButton:disabled {color:#a1adbd;}"));
        panel->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
        auto* row = new QHBoxLayout(panel);
        row->setContentsMargins(6, 4, 6, 4);
        row->setSpacing(4);
        auto* title = new QLabel(objectLabel(), panel);
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
        sizeButton->setText(isLine()?QStringLiteral("端点"):QStringLiteral("尺寸"));
        if(!isLine())addButton(mRotateAction, false);
        addButton(mCopyAction, false);
        auto* more = new QToolButton(panel);
        more->setText(QStringLiteral("更多"));
        more->setIcon(UBUiTheme::icon("more"));
        more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        more->setIconSize(QSize(20, 20));
        more->setFixedSize(74, 44);
        more->setCursor(Qt::ArrowCursor);
        more->setAccessibleName(QStringLiteral("%1更多操作").arg(objectLabel()));
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

QVariant UBGraphicsPixmapItemDelegate::itemChange(QGraphicsItem::GraphicsItemChange change, const QVariant& value)
{
    if (mIsShape && change == QGraphicsItem::ItemSelectedHasChanged) {
        // As with pictures, only the controls rise above the canvas. Selecting
        // a shape must not lift its fill above neighboring handwriting.
        if (mDelegated->data(UBGraphicsItemData::ItemIsHiddenOnDisplay).toBool())
            showHide(!value.toBool());
        if (!value.toBool()) freeControls();
        return value;
    }
    return UBGraphicsItemDelegate::itemChange(change, value);
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
    // The view can be destroyed before the cached scene/delegate at exit.
    connect(mMenu, &QObject::destroyed, this, [this] { mMenu = nullptr; });
    mMenu->setObjectName(mIsShape ? QStringLiteral("shapeOperationsMenu") : QStringLiteral("imageOperationsMenu"));
    mMenu->setStyleSheet(QStringLiteral(
        "QMenu {background:#ffffff; color:#202936; border:1px solid #dbe2eb; border-radius:9px; padding:6px;}"
        "QMenu::item {background:transparent; color:#202936; padding:9px 30px 9px 28px; border-radius:6px;}"
        "QMenu::item:selected {background:#eaf1ff; color:#1d4ed8;}"
        "QMenu::item:disabled {color:#a1adbd;}"
        "QMenu::separator {height:1px; background:#e5eaf1; margin:4px 8px;}"));
    mMenu->addAction(mGeometryAction);
    if(!isLine()){
    auto* rotation = mMenu->addMenu(UBUiTheme::icon("rotate"), QStringLiteral("旋转与翻转"));
    rotation->addAction(mRotateAction);
    rotation->addAction(QStringLiteral("向左旋转 90°"), this, [this] { rotate(-90); });
    rotation->addSeparator();
    rotation->addAction(UBUiTheme::icon("flip"), QStringLiteral("水平翻转"), this, [this] {
        applyTransform(UBImageTransform::flipped(mDelegated->boundingRect(), mDelegated->transform(), true)); });
    rotation->addAction(UBUiTheme::icon("flip"), QStringLiteral("垂直翻转"), this, [this] {
        applyTransform(UBImageTransform::flipped(mDelegated->boundingRect(), mDelegated->transform(), false)); });
    rotation->addAction(QStringLiteral("恢复水平角度"), this, [this] { rotate(-UBImageTransform::angle(mDelegated->transform())); });
    }
    auto* arrange = mMenu->addMenu(UBUiTheme::icon("layers"), QStringLiteral("排列图层"));
    if (!mIsShape) {
        arrange->addAction(QStringLiteral("放到笔迹下方"), this, [this] {
            if (auto scene = castUBGraphicsScene()) scene->arrangeImage(mDelegated, UBImageLayerPolicy::BelowInk); });
        arrange->addAction(QStringLiteral("覆盖已有笔迹"), this, [this] {
            if (auto scene = castUBGraphicsScene()) scene->arrangeImage(mDelegated, UBImageLayerPolicy::AboveInk); });
        arrange->addSeparator();
    }
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
    mMenu->addAction(UBUiTheme::icon("trash"), QStringLiteral("删除%1").arg(objectLabel()), this, [this] { remove(); });
}

void UBGraphicsPixmapItemDelegate::openMenu(const QPoint& globalPosition)
{
    createMenu();
    updateMenuActionState();
    mLockAction->setText(isLocked() ? QStringLiteral("解除锁定") : QStringLiteral("锁定%1").arg(objectLabel()));
    mGeometryAction->setEnabled(!isLocked());
    mRotateAction->setEnabled(!isLocked());
    for (QAction* action : mMenu->actions())
        if (action->menu()) action->setEnabled(!isLocked());
    mMenu->exec(globalPosition);
}

void UBGraphicsPixmapItemDelegate::editGeometry()
{
    if (isLocked()) return;
    if(isLine()){
        const QLineF current=sceneLine(mDelegated);
        QDialog dialog(UBApplication::boardController->controlView());dialog.setWindowTitle(QStringLiteral("直线端点位置"));
        dialog.setStyleSheet(QStringLiteral("QDialog,QLabel{background:white;color:#202936;}QDoubleSpinBox,QPushButton{background:white;color:#202936;min-height:30px;}"));
        auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
        QDoubleSpinBox* values[4];const qreal coordinates[]{current.x1(),current.y1(),current.x2(),current.y2()};
        const QStringList labels{QStringLiteral("端点1 X"),QStringLiteral("端点1 Y"),QStringLiteral("端点2 X"),QStringLiteral("端点2 Y")};
        for(int i=0;i<4;++i){values[i]=new QDoubleSpinBox(&dialog);values[i]->setRange(-100000000,100000000);values[i]->setDecimals(2);values[i]->setValue(coordinates[i]);values[i]->setAccessibleName(labels[i]);form->addRow(labels[i],values[i]);}
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()==QDialog::Accepted){
            const QLineF target(values[0]->value(),values[1]->value(),values[2]->value(),values[3]->value());
            if(target.length()>.01)applyTransform(lineTransform(mDelegated,target));
            else UBApplication::showMessage(QStringLiteral("两个端点不能重合，请重新设置。"));
        }
        return;
    }
    const QRectF bounds = mDelegated->boundingRect();
    if (bounds.isEmpty()) return;
    const QTransform initial = mDelegated->transform();
    const qreal oldWidth = QLineF(initial.map(bounds.topLeft()), initial.map(bounds.topRight())).length();
    const qreal oldHeight = QLineF(initial.map(bounds.topLeft()), initial.map(bounds.bottomLeft())).length();
    if (oldWidth <= 0 || oldHeight <= 0) return;
    QDialog dialog(UBApplication::boardController->controlView());
    dialog.setWindowTitle(QStringLiteral("%1大小与旋转").arg(objectLabel()));
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
    width->setAccessibleName(QStringLiteral("%1宽度").arg(objectLabel()));
    height->setAccessibleName(QStringLiteral("%1高度").arg(objectLabel()));
    angle->setAccessibleName(QStringLiteral("%1旋转角度").arg(objectLabel()));
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
