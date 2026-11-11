#include "UBRegionFillController.h"
#include "UBRegionFill.h"
#include "UBBoardController.h"
#include "UBBoardView.h"
#include "UBDrawingController.h"
#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsSvgItem.h"
#include "gui/UBUiTheme.h"
#include "gui/UBMainWindow.h"
#include <QtWidgets>
#include <QtConcurrent>
#include <cmath>

class UBRegionFillPreview final : public QObject, public QGraphicsPathItem
{
public:
    void setVertices(const QPolygonF& vertices,qreal radius,bool closing) {
        prepareGeometryChange();mVertices=vertices;mRadius=radius;mClosing=closing;update();
    }
    QRectF boundingRect() const override {
        if(mVertices.isEmpty())return QGraphicsPathItem::boundingRect();
        return QGraphicsPathItem::boundingRect().united(mVertices.boundingRect()).adjusted(-mRadius*2,-mRadius*2,mRadius*2,mRadius*2);
    }
    void paint(QPainter* painter,const QStyleOptionGraphicsItem* option,QWidget* widget) override {
        QGraphicsPathItem::paint(painter,option,widget);
        if(mVertices.isEmpty())return;
        painter->save();painter->setRenderHint(QPainter::Antialiasing);
        QPen pen(UBUiTheme::blue(),1.2);pen.setCosmetic(true);painter->setPen(pen);
        for(int i=0;i<mVertices.size();++i){
            painter->setBrush(i==0&&mClosing?UBUiTheme::blue():QColor(Qt::white));
            const qreal r=mRadius*(i==0?1.5:1);
            painter->drawEllipse(mVertices.at(i),r,r);
        }
        painter->restore();
    }
private:
    QPolygonF mVertices;
    qreal mRadius=3;
    bool mClosing=false;
};

// A native split tool whose arrow belongs to its bottom label, not a tall
// side rail. QAction, QMenu, focus and keyboard handling remain QToolButton's.
class UBRegionFillToolButton final : public QToolButton
{
public:
    using QToolButton::QToolButton;
    QRect menuHitRect() const {
        return toolButtonStyle()==Qt::ToolButtonIconOnly ? QRect(width()-22,0,22,height())
            : QRect(width()-23,height()-25,23,25);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QStylePainter painter(this); QStyleOptionToolButton option; initStyleOption(&option);
        option.features &= ~(QStyleOptionToolButton::HasMenu | QStyleOptionToolButton::MenuButtonPopup);
        option.text.clear(); option.icon=QIcon(); option.arrowType=Qt::NoArrow;
        painter.drawComplexControl(QStyle::CC_ToolButton,option);
        const bool labels=toolButtonStyle()!=Qt::ToolButtonIconOnly;
        const auto mode=!isEnabled()?QIcon::Disabled : underMouse()?QIcon::Active:QIcon::Normal;
        const int iconTop=labels?7:(height()-iconSize().height())/2;
        icon().paint(&painter,QRect((width()-iconSize().width())/2,iconTop,iconSize().width(),iconSize().height()),
            Qt::AlignCenter,mode,isChecked()?QIcon::On:QIcon::Off);
        const QColor color=!isEnabled()?QColor("#94a3b8"):isChecked()||isDown()?UBUiTheme::blue():UBUiTheme::muted();
        painter.setPen(color); painter.setFont(font());
        if(labels) {
            const int textWidth=fontMetrics().horizontalAdvance(text());
            const int left=(width()-textWidth-12)/2;
            painter.drawText(QRect(left,height()-22,textWidth,16),Qt::AlignCenter,text());
            drawArrow(painter,QPointF(left+textWidth+8,height()-14));
        } else drawArrow(painter,QPointF(width()-9,height()/2.));
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton && menuHitRect().contains(event->position().toPoint()) && menu()) {
            event->accept(); showMenu(); return;
        }
        // Avoid the native full-height menu strip; the lower label arrow is
        // the only menu hit area. Main-body clicks still trigger the QAction.
        QAbstractButton::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override { QAbstractButton::mouseReleaseEvent(event); }
private:
    static void drawArrow(QPainter& painter,QPointF center) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(painter.pen().color(),1.3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.drawPolyline(QPolygonF{center+QPointF(-2.5,-1.5),center+QPointF(0,1),center+QPointF(2.5,-1.5)});
    }
};

UBRegionFillController::UBRegionFillController(UBBoardController* board)
    : QObject(board), mBoard(board)
{
    auto* settings=UBSettings::settings();
    const int opacity=settings->value("Board/RegionFillOpacity",25).toInt();
    mOpacity=QList<int>{15,25,40}.contains(opacity) ? opacity/100. : .25;
    const int gap=settings->value("Board/RegionFillGap",4).toInt();
    mGap=QList<int>{2,4,8}.contains(gap) ? gap : 4;
    mFollowPenColor=settings->value("Board/RegionFillFollowPen",true).toBool();
    const QColor custom(settings->value("Board/RegionFillColor",QStringLiteral("#2563eb")).toString());
    if(custom.isValid())mCustomColor=custom;
    // Migrate the confusing one-shot prototype once. Subsequent explicit
    // choices are remembered, including a user's preference for click fill.
    if(settings->value("Board/RegionFillInteractionVersion",0).toInt()<2) {
        settings->setValue("Board/RegionFillMode",int(Lasso));
        settings->setValue("Board/RegionFillInteractionVersion",2);
    }
    const int savedMode=settings->value("Board/RegionFillMode",int(Lasso)).toInt();
    mMode=savedMode>=int(Click)&&savedMode<=int(Polygon)?Mode(savedMode):Lasso;
    qApp->installEventFilter(this);
    connect(board,&UBBoardController::activeSceneChanged,this,[this]{cancel();});
    connect(UBDrawingController::drawingController(),&UBDrawingController::stylusToolChanged,
            this,[this](int){cancel();});
}
UBRegionFillController::~UBRegionFillController()
{
    mActive=false; // The view may already be in its parent's destruction.
    cancel();
    if (qApp) qApp->removeEventFilter(this);
}

void UBRegionFillController::addToToolbar(QToolBar* toolbar,QAction* before,QBoxLayout* inlineLayout)
{
    auto* button=new UBRegionFillToolButton(inlineLayout?inlineLayout->parentWidget():toolbar);
    mButton=button;
    button->setObjectName(QStringLiteral("regionFillToolButton"));
    button->setProperty("labelSplitMenu",true);
    button->setProperty("toolbarLabelReserve",QStringLiteral("多边形阴影"));
    mToolAction=new QAction(UBUiTheme::icon("fill"),QString(),button);
    mToolAction->setObjectName(QStringLiteral("actionRegionFill"));
    mToolAction->setCheckable(true);
    mToolAction->setProperty("id",-1);
    button->setDefaultAction(mToolAction);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(toolbar->iconSize());
    button->setPopupMode(QToolButton::MenuButtonPopup);
    connect(mToolAction,&QAction::triggered,this,[this]{if(mActive)cancel();else start(mMode);});
    if(auto* group=UBApplication::mainWindow->actionPen->actionGroup()) {
        group->addAction(mToolAction);
        connect(group,&QActionGroup::triggered,this,[this](QAction* action){if(action!=mToolAction)cancel();});
    }
    auto* menu=new QMenu(button);
    auto* modes=new QActionGroup(menu);
    auto* lasso=menu->addAction(QStringLiteral("圈选阴影 · 拖动后松开填充"),this,[this]{start(Lasso);});
    auto* click=menu->addAction(QStringLiteral("点击阴影 · 识别已有边界"),this,[this]{start(Click);});
    auto* polygon=menu->addAction(QStringLiteral("多边形阴影 · 逐点点击闭合"),this,[this]{start(Polygon);});
    click->setObjectName(QStringLiteral("regionFillClickMode"));lasso->setObjectName(QStringLiteral("regionFillLassoMode"));
    polygon->setObjectName(QStringLiteral("regionFillPolygonMode"));
    for(auto* action:{click,lasso,polygon}){action->setCheckable(true);modes->addAction(action);}
    (mMode==Click?click:mMode==Polygon?polygon:lasso)->setChecked(true);
    auto* opacity=menu->addMenu(QStringLiteral("阴影浓度"));
    auto* opacityChoices=new QActionGroup(opacity);
    for(int value:{15,25,40}){
        auto* action=opacity->addAction(QStringLiteral("%1%%2").arg(value).arg(value==25?QStringLiteral("（默认）"):QString()));
        action->setCheckable(true);action->setChecked(value==qRound(mOpacity*100));opacityChoices->addAction(action);
        connect(action,&QAction::triggered,this,[this,value]{mOpacity=value/100.;UBSettings::settings()->setValue("Board/RegionFillOpacity",value);});
    }
    auto* follow=menu->addAction(QStringLiteral("跟随当前笔色"));
    follow->setCheckable(true);follow->setChecked(mFollowPenColor);
    connect(follow,&QAction::toggled,this,[this](bool checked){mFollowPenColor=checked;UBSettings::settings()->setValue("Board/RegionFillFollowPen",checked);});
    menu->addAction(QStringLiteral("选择阴影颜色…"),this,[this,follow]{
        const auto color=QColorDialog::getColor(mCustomColor,mBoard->controlView(),QStringLiteral("阴影颜色"));
        if(color.isValid()){mCustomColor=color;follow->setChecked(false);UBSettings::settings()->setValue("Board/RegionFillColor",color.name());}
    });
    menu->addSeparator();
    auto* gaps=menu->addMenu(QStringLiteral("补缝范围"));
    auto* choices=new QActionGroup(gaps);
    for (int gap : {2,4,8}) {
        auto* action=gaps->addAction(gap==2 ? QStringLiteral("小") : gap==4 ? QStringLiteral("中（默认）") : QStringLiteral("大"));
        action->setCheckable(true); action->setChecked(gap==mGap); choices->addAction(action);
        connect(action,&QAction::triggered,this,[this,gap]{mGap=gap;UBSettings::settings()->setValue("Board/RegionFillGap",gap);});
    }
    menu->setStyleSheet(QStringLiteral(
        "QMenu{background:white;color:#202936;border:1px solid #dbe2eb;padding:6px;}"
        "QMenu::item{padding:9px 18px;}QMenu::item:selected{background:#eaf1ff;color:#1d4ed8;}"));
    button->setMenu(menu);
    refreshButton();
    if(inlineLayout)inlineLayout->addWidget(button);else toolbar->insertWidget(before,button);
}

void UBRegionFillController::showHint(const QString& text) { UBApplication::showMessage(text); }

void UBRegionFillController::refreshButton()
{
    if(!mButton || !mToolAction)return;
    const QString label=mMode==Polygon?QStringLiteral("多边形阴影"):mMode==Lasso?QStringLiteral("圈选阴影"):QStringLiteral("点击阴影");
    mToolAction->setText(label);
    mButton->setAccessibleName(label);
    mButton->setToolTip(mMode==Polygon
        ?QStringLiteral("逐点点击添加顶点；点击起点、双击、右键或 Enter 闭合填充。Backspace 退回一点，Esc 取消当前图形。")
        :mMode==Lasso
        ?QStringLiteral("按住拖动圈出范围，松开直接填充；可连续圈选，Esc 退出。文字旁箭头设置颜色和浓度。")
        :QStringLiteral("点击已有图形内部直接填充，可自动补小缺口；Esc 退出。文字旁箭头切换圈选方式。"));
}

void UBRegionFillController::start(Mode mode)
{
    if(mActive)clearGesture();
    if (!mBoard->activeScene() || !mBoard->controlView()->isVisible()) return;
    auto* currentGroup=UBApplication::mainWindow->actionPen->actionGroup();
    if(mToolAction && currentGroup && mToolAction->actionGroup()!=currentGroup) {
        // Changing palette orientation recreates the legacy tool action group.
        currentGroup->addAction(mToolAction);
        connect(currentGroup,&QActionGroup::triggered,this,[this](QAction* action){if(action!=mToolAction)cancel();});
    }
    if(!mActive && mToolAction && mToolAction->actionGroup()) {
        auto* selected=mToolAction->actionGroup()->checkedAction();
        if(selected!=mToolAction)mPreviousToolAction=selected;
        // Main-body triggering has already checked our QAction. Remember the
        // underlying tool as well so Esc can restore it without changing mode.
        if(!mPreviousToolAction) {
            // Do not use QAction's legacy "id": desktop palettes overwrite
            // that property with their own button index.
            auto* main=UBApplication::mainWindow;
            const QList<QAction*> tools{main->actionPen,main->actionEraser,main->actionMarker,
                main->actionSelector,main->actionPlay,main->actionHand,main->actionZoomIn,
                main->actionZoomOut,main->actionPointer,main->actionLine,main->actionText,main->actionCapture};
            const int tool=UBDrawingController::drawingController()->stylusTool();
            if(tool>=0 && tool<tools.size())mPreviousToolAction=tools.at(tool);
        }
    }
    mScene=mBoard->activeScene(); mMode=mode; mActive=true;
    UBSettings::settings()->setValue("Board/RegionFillMode",int(mode));
    mBoard->activeScene()->deselectAllItems();
    mBoard->controlView()->viewport()->setCursor(Qt::CrossCursor);
    if(mButton){
        mToolAction->setChecked(true);
        if(auto* action=mButton->menu()->findChild<QAction*>(mode==Click?"regionFillClickMode":mode==Polygon?"regionFillPolygonMode":"regionFillLassoMode"))action->setChecked(true);
    }
    refreshButton();
    showHint(mode==Polygon ? QStringLiteral("逐点点击；点击起点、双击、右键或 Enter 闭合，Backspace 退回，Esc 取消。")
                         :mode==Click ? QStringLiteral("点击区域直接填充；Ctrl+Z 撤销，Esc 退出。")
                         : QStringLiteral("按住拖动圈出范围，松开即填充；可连续圈选，Ctrl+Z 撤销，Esc 退出。"));
}

void UBRegionFillController::clearGesture()
{
    ++mDetectionSerial;
    if (auto scene=mScene.lock()) {
        if (mPreview) { scene->removeItem(mPreview); delete mPreview.data(); }
    }
    mPreview=nullptr; mPath=QPainterPath(); mPoints.clear(); mPressed=false;mPolygonClosing=false;
}

void UBRegionFillController::cancel()
{
    clearGesture();
    if(mToolAction) {
        auto* group=mToolAction->actionGroup();
        const bool ownedSelection=!group || !group->checkedAction() || group->checkedAction()==mToolAction;
        mToolAction->setChecked(false);
        if(mActive && mPreviousToolAction && ownedSelection)
            mPreviousToolAction->setChecked(true);
    }
    if (mActive && mBoard->controlView())
        mBoard->controlView()->setToolCursor(UBDrawingController::drawingController()->stylusTool());
    mActive=false; mScene.reset(); mPreviousToolAction=nullptr;
}

void UBRegionFillController::updatePreview()
{
    auto scene=mScene.lock();
    if (!scene || scene!=mBoard->activeScene()) { cancel(); return; }
    if (!mPreview) {
        mPreview=new UBRegionFillPreview;
        mPreview->setData(UBGraphicsItemData::ItemLayerType,UBItemLayerType::Control);
        mPreview->setAcceptedMouseButtons(Qt::NoButton);
        mPreview->setZValue(1010020);
        scene->addItem(mPreview);
    }
    QColor color=mColor; color.setAlphaF(mOpacity);
    QPen boundary(UBUiTheme::blue(),1,Qt::DashLine); boundary.setCosmetic(true);
    mPreview->setPen(mMode!=Click?boundary:QPen(Qt::NoPen)); mPreview->setBrush(color); mPreview->setPath(mPath);
    const auto transform=mBoard->controlView()->viewportTransform();
    const qreal scale=qMax(.001,std::hypot(transform.m11(),transform.m12()));
    mPreview->setVertices(mMode==Polygon?mPoints:QPolygonF(),3/scale,mPolygonClosing);
    mBoard->controlView()->viewport()->update();
}

void UBRegionFillController::detectAt(QPointF position)
{
    if (mBusy) { showHint(QStringLiteral("正在识别区域，请稍候；也可取消或圈选。")); return; }
    auto scene=mScene.lock(); auto* view=mBoard->controlView();
    if (!scene) { cancel(); return; }
    const QRectF visible=view->mapToScene(view->viewport()->rect()).boundingRect();
    const QSize size=view->viewport()->size().scaled(QSize(1024,1024),Qt::KeepAspectRatio);
    if (size.isEmpty()) return;
    QImage image(size,QImage::Format_RGB32); image.fill(Qt::white);
    {
        QPainter painter(&image);
        const auto old=scene->renderingContext();
        scene->setRenderingContext(UBGraphicsScene::NonScreen);
        scene->render(&painter,QRectF(QPointF(),size),visible,Qt::IgnoreAspectRatio);
        scene->setRenderingContext(old);
    }
    const QPoint seed(qFloor(position.x()*size.width()/view->viewport()->width()),
                      qFloor(position.y()*size.height()/view->viewport()->height()));
    mPath=QPainterPath();
    updatePreview(); showHint(QStringLiteral("正在识别区域… 可随时取消。"));
    QTransform transform; transform.translate(visible.left(),visible.top());
    transform.scale(visible.width()/size.width(),visible.height()/size.height());
    const quint64 serial=++mDetectionSerial;
    const int gap=mGap;
    mBusy=true;
    auto* watcher=new QFutureWatcher<UBRegionFill::Result>(this);
    connect(watcher,&QFutureWatcher<UBRegionFill::Result>::finished,this,[this,watcher,serial,transform]{
        mBusy=false;
        const auto result=watcher->result(); watcher->deleteLater();
        if (!mActive || serial!=mDetectionSerial || mScene.lock()!=mBoard->activeScene()) return;
        if (!result.error.isEmpty()) { updatePreview(); showHint(result.error); return; }
        mPath=transform.map(result.path);
        apply();
    });
    watcher->setFuture(QtConcurrent::run([image,seed,gap]{return UBRegionFill::detect(image,seed,gap);}));
}

void UBRegionFillController::pointerPress(QPointF position)
{
    if(mMode==Polygon){addPolygonPoint(position);return;}
    if(mMode==Click && mBusy) {showHint(QStringLiteral("正在识别区域，请稍候。"));return;}
    clearGesture();
    mColor=mFollowPenColor ? mBoard->penColorOnLightBackground() : mCustomColor;
    mColor.setAlphaF(1);
    if (mMode==Click) { detectAt(position); return; }
    mPressed=true; mPoints.clear(); mPath=QPainterPath();
    mPoints << mBoard->controlView()->viewportTransform().inverted().map(position);
    updatePreview();
}
void UBRegionFillController::pointerMove(QPointF position)
{
    if(mMode==Polygon){
        if(!mPoints.isEmpty())rebuildPolygonPreview(mBoard->controlView()->viewportTransform().inverted().map(position),true);
        return;
    }
    if (!mPressed || mMode!=Lasso || mPoints.size()>=4096) return;
    const QPointF point=mBoard->controlView()->viewportTransform().inverted().map(position);
    if (QLineF(mBoard->controlView()->viewportTransform().map(mPoints.last()),position).length()<2) return;
    mPoints << point; mPath=QPainterPath(); mPath.addPolygon(mPoints); mPath.closeSubpath(); updatePreview();
}
void UBRegionFillController::pointerRelease(QPointF position)
{
    if(mMode==Polygon)return;
    if (!mPressed) return;
    pointerMove(position); mPressed=false;
    if (mPoints.size()<3 || mPath.boundingRect().width()<2 || mPath.boundingRect().height()<2) {
        mPath=QPainterPath(); updatePreview(); showHint(QStringLiteral("请圈出一块区域，松开后自动闭合。")); return;
    }
    apply();
}

void UBRegionFillController::addPolygonPoint(QPointF position)
{
    const auto transform=mBoard->controlView()->viewportTransform();
    const QPointF point=transform.inverted().map(position);
    if(mPoints.isEmpty()) {
        clearGesture();mColor=mFollowPenColor?mBoard->penColorOnLightBackground():mCustomColor;mColor.setAlphaF(1);
    } else {
        if(QLineF(transform.map(mPoints.first()),position).length()<=9) {
            if(mPoints.size()>=3)finishPolygon();
            else showHint(QStringLiteral("至少需要三个顶点，再点击起点闭合。"));
            return;
        }
        if(QLineF(transform.map(mPoints.last()),position).length()<2)return;
    }
    if(mPoints.size()>=512){showHint(QStringLiteral("顶点已达上限，请双击、右键或按 Enter 闭合。"));return;}
    mPoints<<point;
    rebuildPolygonPreview(point,false);
}

void UBRegionFillController::rebuildPolygonPreview(QPointF hover,bool includeHover)
{
    QPolygonF points=mPoints;
    const auto transform=mBoard->controlView()->viewportTransform();
    mPolygonClosing=includeHover && points.size()>=3 && QLineF(transform.map(points.first()),transform.map(hover)).length()<=9;
    if(includeHover&&!points.isEmpty()) {
        if(mPolygonClosing)hover=points.first();
        if(QLineF(points.last(),hover).length()>.001)points<<hover;
    }
    mPath=QPainterPath();
    if(!points.isEmpty()) {
        mPath.addPolygon(points);
        if(points.size()>=3)mPath.closeSubpath();
    }
    updatePreview();
}

void UBRegionFillController::finishPolygon()
{
    if(mPoints.size()<3){showHint(QStringLiteral("至少点击三个顶点，才能闭合填充。"));return;}
    QPainterPath path;path.addPolygon(mPoints);path.closeSubpath();
    const auto simplified=path.simplified();
    if(simplified.isEmpty() || simplified.boundingRect().width()<.01 || simplified.boundingRect().height()<.01) {
        showHint(QStringLiteral("这些顶点没有围成区域，请退回一点或补充顶点。"));return;
    }
    mPath=path;apply();
}

void UBRegionFillController::removePolygonPoint()
{
    if(mPoints.isEmpty())return;
    mPoints.removeLast();
    if(mPoints.isEmpty())clearGesture();else rebuildPolygonPreview(mPoints.last(),false);
}

void UBRegionFillController::apply()
{
    auto scene=mScene.lock();
    if (!mActive || !scene || scene!=mBoard->activeScene() || mPath.isEmpty() || mPressed) return;
    const QRectF bounds=mPath.boundingRect();
    if (bounds.isEmpty()) return;
    QTransform local; local.translate(-bounds.left(),-bounds.top());
    const auto polygons=local.map(mPath).toSubpathPolygons();
    QString data;
    for (const auto& polygon:polygons) {
        if (polygon.size()<3) continue;
        data += QStringLiteral("M %1 %2 ").arg(polygon.first().x(),0,'f',3).arg(polygon.first().y(),0,'f',3);
        for (int i=1;i<polygon.size();++i) data+=QStringLiteral("L %1 %2 ").arg(polygon[i].x(),0,'f',3).arg(polygon[i].y(),0,'f',3);
        data+=QStringLiteral("Z ");
    }
    const QByteArray svg=QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\"><path d=\"%3\" fill=\"%4\" fill-opacity=\"%5\" fill-rule=\"evenodd\"/></svg>")
        .arg(bounds.width(),0,'f',3).arg(bounds.height(),0,'f',3).arg(data,mColor.name()).arg(mOpacity,0,'f',3).toUtf8();
    if (data.isEmpty()) return;
    auto* item=scene->addSvg({},bounds.center(),svg);
    if (!item) { showHint(QStringLiteral("阴影保存失败，请重试。")); return; }
    item->setTransform(QTransform()); item->setPos(bounds.topLeft());
    // addSvg uses the existing image-below-ink policy, persistence and a single
    // item undo command. No permanent changes to the original outline occur.
    clearGesture();
    mBoard->controlView()->viewport()->setCursor(Qt::CrossCursor);
    showHint(QStringLiteral("已填充，可继续操作；Ctrl+Z 撤销，Esc 退出阴影工具。"));
    scene->update(); mBoard->controlView()->viewport()->repaint();
}

bool UBRegionFillController::eventFilter(QObject* watched,QEvent* event)
{
    if (!mActive) return false;
    auto* view=mBoard->controlView();
    const bool canvas=watched==view || watched==view->viewport();
    if (event->type()==QEvent::ApplicationDeactivate || (watched==view && event->type()==QEvent::Hide)) {cancel();return false;}
    if (event->type()==QEvent::KeyPress || event->type()==QEvent::ShortcutOverride) {
        auto* key=static_cast<QKeyEvent*>(event);
        if (QApplication::activePopupWidget() || QApplication::activeModalWidget()) return false;
        const bool pendingUndo=mMode==Polygon&&!mPoints.isEmpty()&&key->matches(QKeySequence::Undo);
        if(event->type()==QEvent::ShortcutOverride){if(pendingUndo){event->accept();return true;}return false;}
        if(pendingUndo){removePolygonPoint();return true;}
        if(mMode==Polygon&&(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)){finishPolygon();return true;}
        if(mMode==Polygon&&key->key()==Qt::Key_Backspace){removePolygonPoint();return true;}
        if (key->key()==Qt::Key_Escape) {
            if(mMode==Polygon&&!mPoints.isEmpty()){clearGesture();showHint(QStringLiteral("已取消当前多边形，可重新点选；再按 Esc 退出。"));}
            else cancel();
            return true;
        }
        if (key->modifiers().testFlag(Qt::ControlModifier) && (key->key()==Qt::Key_Z || key->key()==Qt::Key_Y)) {clearGesture();return false;}
    }
    if (canvas && event->type()==QEvent::Wheel) {clearGesture();return false;}
    if (!canvas) {
        // Real input visits QWidgetWindow (a QWindow) before Qt forwards it to
        // the viewport. It is an envelope, not an outside-canvas interaction.
        // Clearing on its release loses the whole lasso before the viewport
        // can commit. Only an actual external QWidget may discard a drag.
        if(!qobject_cast<QWidget*>(watched))return false;
        if(mMode==Polygon&&!mPoints.isEmpty()&&event->type()==QEvent::MouseButtonPress
            && watched->objectName()==QStringLiteral("ubBoardUndoButton")
            && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){
            removePolygonPoint();event->accept();return true;
        }
        // Undo, colours and our presets are not tool changes. The exclusive
        // tool actions and page/window signals own exit; don't silently leave
        // shading merely because a control was clicked.
        if(mPressed && event->type()==QEvent::MouseButtonPress)clearGesture();
        if (mPressed && event->type()==QEvent::MouseButtonRelease) {mPressed=false;mPath=QPainterPath();updatePreview();}
        return false;
    }
    if (event->type()==QEvent::Enter) {
        QTimer::singleShot(0,this,[this]{if(mActive)mBoard->controlView()->viewport()->setCursor(Qt::CrossCursor);});
    }
    if(event->type()==QEvent::ContextMenu && mMode==Polygon){event->accept();return true;}
    if(event->type()==QEvent::MouseButtonDblClick){
        if(mMode==Polygon&&!mPoints.isEmpty())finishPolygon();event->accept();return true;
    }
    if (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseMove) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(mMode==Polygon&&mouse->button()==Qt::RightButton){
            if(event->type()==QEvent::MouseButtonPress)finishPolygon();event->accept();return true;
        }
        if (event->type()!=QEvent::MouseMove && mouse->button()!=Qt::LeftButton) {cancel();return false;}
        if (event->type()==QEvent::MouseButtonPress) pointerPress(mouse->position());
        if (event->type()==QEvent::MouseMove) pointerMove(mouse->position());
        if (event->type()==QEvent::MouseButtonRelease) pointerRelease(mouse->position());
        event->accept(); return true;
    }
    if (event->type()==QEvent::TabletPress || event->type()==QEvent::TabletMove || event->type()==QEvent::TabletRelease) {
        auto* pen=static_cast<QTabletEvent*>(event);
        if (UBApplication::app()) UBApplication::app()->setTabletCursorOverride(false);
        if(mMode==Polygon&&pen->button()==Qt::RightButton){
            if(event->type()==QEvent::TabletPress)finishPolygon();event->accept();return true;
        }
        if (event->type()==QEvent::TabletPress) pointerPress(pen->position());
        if (event->type()==QEvent::TabletMove) pointerMove(pen->position());
        if (event->type()==QEvent::TabletRelease) pointerRelease(pen->position());
        event->accept(); return true;
    }
    return false;
}
