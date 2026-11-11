#define main pastePositionMain
#define runTests pastePositionTests
#define runWhenReady pastePositionReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "board/UBRegionFillController.h"
#include "board/UBRegionFill.h"
#include "domain/UBGraphicsDelegateFrame.h"
#include "domain/UBGraphicsPixmapItemDelegate.h"
#include "adaptors/UBSvgSubsetAdaptor.h"
#include <QGraphicsSceneContextMenuEvent>

static void detectorTests()
{
    QImage image(300,240,QImage::Format_RGB32); image.fill(Qt::white);
    { QPainter p(&image); p.setPen(QPen(Qt::red,4));p.drawRect(QRect(40,40,210,150)); }
    auto result=UBRegionFill::detect(image,{100,100},4);
    check(result.error.isEmpty() && result.path.contains({100,100}),"closed hand-drawn region detected");
    check(!result.path.contains({10,10}),"fill never includes the outside of the outline");
    {QPainter p(&image);p.fillRect(140,36,5,9,Qt::white);}
    result=UBRegionFill::detect(image,{100,100},4);
    check(result.error.isEmpty() && result.path.contains({100,100}),"small open seam is automatically bridged");
    {QPainter p(&image);p.fillRect(125,35,40,12,Qt::white);}
    check(!UBRegionFill::detect(image,{100,100},4).error.isEmpty(),"large opening is refused instead of flooding the page");
    image.fill(Qt::white);
    check(!UBRegionFill::detect(image,{100,100},4).error.isEmpty(),"blank unbounded canvas cannot be filled");
    check(!UBRegionFill::detect(image,{-1,20},4).error.isEmpty(),"outside-canvas seed is rejected");
    {QPainter p(&image);p.setPen(QPen(Qt::red,4));p.drawRect(QRect(40,40,210,150));p.drawEllipse(QRect(115,80,60,60));}
    result=UBRegionFill::detect(image,{80,80},4);
    check(result.error.isEmpty() && !result.path.contains({145,110}),"nested closed region remains a hole");
    check(!UBRegionFill::detect(image,{40,40},4).error.isEmpty(),"seed on the border does not create an accidental tiny region");
    QImage huge(1025,10,QImage::Format_RGB32);huge.fill(Qt::white);
    check(!UBRegionFill::detect(huge,{10,5}).error.isEmpty(),"detector rejects unbounded input dimensions");
    QImage worst(1024,1024,QImage::Format_RGB32);worst.fill(Qt::white);
    {QPainter p(&worst);p.setPen(QPen(Qt::black,4));p.drawRect(QRect(200,200,620,620));}
    QElapsedTimer timer;timer.start();result=UBRegionFill::detect(worst,{500,500});
    std::cout<<"bounded detector ms="<<timer.elapsed()<<std::endl;
    check(result.error.isEmpty() && timer.elapsed()<2000,"maximum-resolution detector completes within a bounded time budget");
}
static QImage content(const std::shared_ptr<UBGraphicsScene>& scene,UBGraphicsScene::RenderingContext context)
{
    QImage image(960,540,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
    QPainter p(&image);auto old=scene->renderingContext();scene->setRenderingContext(context);
    scene->render(&p,QRectF(0,0,960,540),QRectF(-480,-270,960,540));scene->setRenderingContext(old);return image;
}
static QList<UBGraphicsSvgItem*> vectors(const std::shared_ptr<UBGraphicsScene>& scene)
{
    QList<UBGraphicsSvgItem*> list;for(auto* item:scene->items())if(auto* svg=dynamic_cast<UBGraphicsSvgItem*>(item))list<<svg;return list;
}
static void mouse(QWidget* target,QEvent::Type type,QPointF pos)
{
    QMouseEvent event(type,pos,target->mapToGlobal(pos.toPoint()),
        type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target,&event);
}
static void frameDrag(const std::shared_ptr<UBGraphicsScene>& scene,QGraphicsItem* frame,QPointF a,QPointF b,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    for(auto type:{QEvent::GraphicsSceneMousePress,QEvent::GraphicsSceneMouseMove,QEvent::GraphicsSceneMouseRelease}){
        QGraphicsSceneMouseEvent event(type);event.setScenePos(type==QEvent::GraphicsSceneMousePress?a:b);
        event.setPos(frame->mapFromScene(event.scenePos()));event.setButton(Qt::LeftButton);
        event.setButtons(type==QEvent::GraphicsSceneMouseRelease?Qt::NoButton:Qt::LeftButton);
        event.setModifiers(modifiers);
        scene->sendEvent(frame,&event);
    }
}
static void shapeAndFillTests()
{
    detectorTests();
    auto* board=UBApplication::boardController;auto* main=UBApplication::mainWindow;
    UBApplication::applicationController->showBoard();board->paletteManager()->tipsPalette()->hide();main->resize(1440,900);
    board->addScene();auto scene=board->activeScene();auto* view=board->controlView();
    board->setBorderlessPageSize(true);board->zoomRestore();view->centerOn(0,0);settle();
    auto* stack=UBApplication::undoStack.data();
    auto* draw=UBDrawingController::drawingController();
    draw->setStylusTool(UBStylusTool::Line);draw->setLineGeometryMode(UBDrawingController::TriangleGeometry);
    draw->setLinePattern(UBDrawingController::DashedLinePattern);draw->setShapeFillEnabled(true);
    draw->setShapeFillColor(QColor("#2563eb"));draw->setShapeFillOpacity(25);
    scene->inputDevicePress({-220,-160});scene->inputDeviceMove({120,160});scene->inputDeviceRelease(UBStylusTool::Line);
    UBGraphicsStrokesGroup* shape=nullptr;
    for(auto* item:scene->items())if(auto* group=dynamic_cast<UBGraphicsStrokesGroup*>(item))shape=group;
    check(shape && shape->childItems().size()>4,"real dashed filled triangle is created by the production drawing tool");
    const int parts=shape->childItems().size();
    auto* delegate=dynamic_cast<UBGraphicsPixmapItemDelegate*>(shape->Delegate());
    check(delegate,"inserted geometric shape shares the modern image editor");
    draw->setStylusTool(UBStylusTool::Selector);shape->setSelected(true);delegate->createControls();delegate->showControls();settle();
    QGraphicsItem* frame=nullptr;QWidget* toolbar=nullptr;
    for(auto* item:scene->items()){
        if(auto* candidate=dynamic_cast<UBGraphicsDelegateFrame*>(item))if(candidate->isVisible())frame=candidate;
        if(auto* proxy=dynamic_cast<QGraphicsProxyWidget*>(item))if(proxy->widget()&&proxy->widget()->objectName()=="shapeContextToolbar")toolbar=proxy->widget();
    }
    check(frame && toolbar,"shape has the same blue frame and compact toolbar as an image");
    int oldControls=0;for(auto* child:frame->childItems())if(dynamic_cast<DelegateButton*>(child)&&child->isVisible())++oldControls;
    check(oldControls==0,"legacy gray delete/copy/rotate chrome is not shown");
    const QString out=qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT");
    check(main->grab().save(out+"/shape-1440.png"),"shape desktop screenshot saved");
    const QTransform initial=shape->transform();const int history=stack->index();
    const QPointF corner=shape->mapToScene(shape->boundingRect().bottomRight());
    frameDrag(scene,frame,corner,corner+QPointF(90,50));
    check(shape->transform()!=initial && stack->index()==history+1,"corner drag scales the shape in one undo step");
    board->undo();check(shape->transform()==initial,"shape resize undo restores exact transform");
    board->redo();check(shape->transform()!=initial,"shape resize redo restores transform");
    check(shape->childItems().size()==parts,"shape outline dashes and fill remain intact after resize");
    const QTransform beforeRotation=shape->transform();
    const QPointF shapeCenter=shape->mapToScene(shape->boundingRect().center());
    const QPointF top=shape->mapToScene(UBImageTransform::point(shape->boundingRect(),UBImageTransform::Top));
    const QPointF normal=top-shapeCenter;
    const QPointF rotateGrip=top+normal/QLineF(top,shapeCenter).length()*28/(board->systemScaleFactor()*board->currentZoom());
    QTransform turn;turn.translate(shapeCenter.x(),shapeCenter.y());turn.rotate(31);turn.translate(-shapeCenter.x(),-shapeCenter.y());
    frameDrag(scene,frame,rotateGrip,turn.map(rotateGrip),Qt::ShiftModifier);
    check(std::abs(UBImageTransform::angle(shape->transform())-30)<.1,"rotation grip shares image-style 15-degree Shift snapping");
    board->undo();check(shape->transform()==beforeRotation,"rotation is undoable as one exact transform");
    delegate->lock(true);delegate->positionHandles();const auto locked=shape->transform();
    frameDrag(scene,frame,shape->mapToScene(shape->boundingRect().bottomRight()),corner+QPointF(200,200));
    check(shape->transform()==locked,"locked shape cannot be resized");delegate->lock(false);
    bool menuChecked=false;
    QTimer::singleShot(0,qApp,[&]{
        auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(menu){for(auto* action:menu->actions())if(action->text()==QStringLiteral("删除图形"))menuChecked=true;menu->close();}
    });
    QGraphicsSceneContextMenuEvent menuEvent(QEvent::GraphicsSceneContextMenu);menuEvent.setScenePos(shape->sceneBoundingRect().center());menuEvent.setScreenPos({300,300});
    scene->sendEvent(shape,&menuEvent);check(menuChecked,"shape right-click exposes the modern object operations menu");
    auto* copy=dynamic_cast<QGraphicsItem*>(board->duplicateItem(shape));
    check(copy && dynamic_cast<UBGraphicsPixmapItemDelegate*>(UBGraphicsItem::Delegate(copy)),"shape copy retains the shared editor");
    UBGraphicsItem::Delegate(copy)->remove();check(!copy->scene(),"deleting a shape removes its modern controls and content");
    board->undo();check(copy->scene()==scene.get(),"shape deletion is restored by one undo");
    const QPointF oldPosition=shape->pos();
    QKeyEvent nudge(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);scene->sendEvent(shape,&nudge);
    check(shape->pos()==oldPosition+QPointF(1,0),"shape supports image-style keyboard nudging");
    board->undo();check(shape->pos()==oldPosition,"keyboard nudge is undoable");
    scene->deselectAllItems();

    auto* fill=board->regionFillController();check(fill && main->findChild<QToolButton*>("regionFillToolButton"),"region fill is reachable from the main toolbar");
    auto* shadeButton=main->findChild<QToolButton*>("regionFillToolButton");QToolButton* textButton=nullptr;QToolButton* firstColor=nullptr;
    int normalized=0;bool sameMetrics=true;
    for(auto* button:main->boardToolBar->findChildren<QToolButton*>()){
        if(button->defaultAction()==main->actionText)textButton=button;
        if(button->property("compactColorChoice").toBool()&&!firstColor)firstColor=button;
        if(button->property("boardToolbarCommand").toBool()){
            ++normalized;sameMetrics &= button->iconSize()==QSize(24,24)&&button->height()==58&&button->font().pixelSize()==11;
        }
    }
    check(normalized>=15 && sameMetrics,"toolbar commands share one icon size, label font and button height");
    check(textButton && firstColor && shadeButton->mapTo(main->boardToolBar,QPoint()).x()>textButton->mapTo(main->boardToolBar,QPoint()).x()
        &&shadeButton->mapTo(main->boardToolBar,QPoint()).x()<firstColor->mapTo(main->boardToolBar,QPoint()).x(),"shadow tool sits immediately after text and before the color/property group");
    auto before=vectors(scene);const int index=stack->index();const QImage recordedBefore=content(scene,UBGraphicsScene::Podcast);
    fill->start(UBRegionFillController::Lasso);
    const QPointF center=view->mapFromScene(QPointF(240,0));
    mouse(view->viewport(),QEvent::MouseButtonPress,center+QPointF(-60,-50));
    mouse(view->viewport(),QEvent::MouseMove,center+QPointF(60,-50));
    mouse(view->viewport(),QEvent::MouseMove,center+QPointF(70,45));
    check(fill->hasPreview() && vectors(scene).size()==before.size() && stack->index()==index,"lasso shows transient feedback while dragging without adding history");
    check(content(scene,UBGraphicsScene::Podcast)==recordedBefore,"unconfirmed preview is excluded from the recording render");
    check(content(scene,UBGraphicsScene::NonScreen)==recordedBefore,"unconfirmed preview is excluded from exports/thumbnails");
    check(main->grab().save(out+"/fill-drag-1440.png"),"lasso drag desktop screenshot saved");
    mouse(view->viewport(),QEvent::MouseButtonRelease,center+QPointF(-55,50));
    check(vectors(scene).size()==before.size()+1 && stack->index()==index+1 && fill->isActive(),"releasing lasso commits one undoable object and keeps the tool active");
    check(!view->findChild<QWidget*>("regionFillPanel"),"no extra settings or confirmation panel interrupts filling");
    UBGraphicsSvgItem* shadow=nullptr;for(auto* item:vectors(scene))if(!before.contains(item))shadow=item;
    check(shadow && shadow->fileData().contains("fill-opacity=\"0.250\""),"shadow is a persistent semitransparent vector, not a destructive pixel edit");
    check(shadow->zValue()<shape->zValue(),"committed shading is below existing handwriting/geometry");
    const QImage committed=content(scene,UBGraphicsScene::Podcast);check(committed!=recordedBefore,"confirmed shading appears in the recording content");
    board->undo();check(vectors(scene).size()==before.size() && content(scene,UBGraphicsScene::Podcast)==recordedBefore,"one undo completely removes shading");
    board->redo();check(content(scene,UBGraphicsScene::Podcast)==committed,"redo restores exactly the same shading");
    fill->start(UBRegionFillController::Lasso);mouse(view->viewport(),QEvent::MouseButtonPress,center);
    mouse(view->viewport(),QEvent::MouseMove,center+QPointF(40,30));
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&escape);
    check(!fill->isActive() && content(scene,UBGraphicsScene::Podcast)==committed,"Escape cancels without changing the board");
    const int penHistory=stack->index();
    fill->start(UBRegionFillController::Lasso);
    const QPointingDevice pen("fill-test pen",827,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
    const QList<QPointF> points={center,center+QPointF(40,0),center+QPointF(40,40),center+QPointF(0,40)};
    for(int i=0;i<points.size();++i){
        QTabletEvent e(i==0?QEvent::TabletPress:i==points.size()-1?QEvent::TabletRelease:QEvent::TabletMove,
            &pen,points[i],view->viewport()->mapToGlobal(points[i].toPoint()),i==points.size()-1?0:.5,0,0,0,0,0,
            Qt::NoModifier,Qt::LeftButton,i==points.size()-1?Qt::NoButton:Qt::LeftButton);
        QApplication::sendEvent(view->viewport(),&e);
    }
    check(fill->isActive() && stack->index()==penHistory+1,"synthetic pen lasso commits once and remains ready without stray pen strokes");
    board->undo();check(content(scene,UBGraphicsScene::Podcast)==committed,"pen lasso is removed by a single undo");
    fill->start(UBRegionFillController::Click);mouse(view->viewport(),QEvent::MouseButtonPress,{25,25});
    mouse(view->viewport(),QEvent::MouseButtonRelease,{25,25});
    check(!fill->hasPreview(),"canvas-edge click is rejected instead of creating full-page shading");fill->cancel();
    // Detection works in view coordinates even when the board is far from its
    // original origin. Computing is asynchronous; cancellation invalidates it.
    auto* outline=scene->addSvg({},QPointF(3000,2000),QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" height=\"160\"><rect x=\"3\" y=\"3\" width=\"234\" height=\"154\" fill=\"none\" stroke=\"red\" stroke-width=\"4\"/></svg>"));
    outline->setTransform(QTransform());outline->setPos(2880,1920);view->centerOn(3000,2000);settle();
    QElapsedTimer wait;wait.start();while(fill->isBusy() && wait.elapsed()<5000)settle();
    fill->start(UBRegionFillController::Click);const QPoint seed=view->mapFromScene(QPointF(3000,2000));
    mouse(view->viewport(),QEvent::MouseButtonPress,seed);mouse(view->viewport(),QEvent::MouseButtonRelease,seed);
    wait.restart();while(fill->isBusy() && wait.elapsed()<5000)settle();
    check(fill->isActive() && vectors(scene).size()==before.size()+3,"background bucket fills the distant panned region and remains ready");
    fill->start(UBRegionFillController::Click);mouse(view->viewport(),QEvent::MouseButtonPress,seed);fill->cancel();
    wait.restart();while(fill->isBusy() && wait.elapsed()<5000)settle();
    check(!fill->isActive() && !fill->hasPreview(),"late background result cannot resurrect a cancelled preview");
    fill->start(UBRegionFillController::Lasso);board->addScene();check(!fill->isActive(),"switching pages cancels transient fill state safely");
    board->setActiveDocumentScene(board->selectedDocument(),board->activeSceneIndex()-1); // Back to the saved fixture page.
    auto doc=board->selectedDocument();
    UBSvgSubsetAdaptor::persistScene(doc,scene,board->activeSceneIndex());
    auto restored=UBSvgSubsetAdaptor::loadScene(doc,board->activeSceneIndex());
    bool found=false;for(auto* item:vectors(restored))if(item->fileData().contains("fill-opacity=\"0.250\""))found=true;
    check(found,"save/reopen retains the confirmed shading opacity and geometry");
    main->resize(1024,768);settle();
    view->centerOn(0,0);
    for(auto* item:scene->items())if(auto* group=dynamic_cast<UBGraphicsStrokesGroup*>(item)){group->setSelected(true);group->Delegate()->createControls();group->Delegate()->showControls();break;}
    check(main->grab().save(out+"/shape-1024.png"),"compact desktop screenshot saved");
    main->resize(1440,900);settle();
    check(view->isVisible(),"selected shape remains usable after compact-to-wide window resizing");
    auto* tool=main->findChild<QToolButton*>("regionFillToolButton");
    check(tool && tool->popupMode()==QToolButton::MenuButtonPopup,"main fill button works directly and exposes presets on its arrow");
    bool preset=false;for(auto* a:tool->menu()->actions())if(a->menu()&&a->text()==QStringLiteral("阴影浓度"))for(auto* option:a->menu()->actions())if(option->isChecked()&&option->text().contains("25%"))preset=true;
    check(preset,"25 percent shading is preset in the menu before use");
    const QImage buttonPixels=tool->grab().toImage();int blackPixels=0;
    for(int y=0;y<buttonPixels.height();++y)for(int x=qMax(0,buttonPixels.width()-qRound(16*buttonPixels.devicePixelRatio()));x<buttonPixels.width();++x){
        const QColor c=buttonPixels.pixelColor(x,y);if(c.alpha()>200&&c.red()<30&&c.green()<30&&c.blue()<30)++blackPixels;
    }
    check(blackPixels<120*buttonPixels.devicePixelRatio()*buttonPixels.devicePixelRatio(),"split-button arrow does not inherit a black system-theme background");
    auto* menu=tool->menu();const QPoint anchor=tool->mapToGlobal(QPoint(0,tool->height()));
    menu->popup(anchor);settle();menu->move(anchor);
    QMenu* opacityMenu=nullptr;
    for(auto* action:menu->actions())if(action->menu()&&action->text()==QStringLiteral("阴影浓度")){
        menu->setActiveAction(action);opacityMenu=action->menu();const QRect row=menu->actionGeometry(action);
        const QPoint submenuAnchor=menu->mapToGlobal(row.topRight());
        opacityMenu->popup(submenuAnchor);settle();opacityMenu->move(submenuAnchor);break;
    }
    settle();
    // Qt's offscreen platform has no OS compositor. Compose the actual popup
    // widgets at their real global offsets over the actual client render;
    // never present a detached menu crop as a full-client screenshot.
    // Its default 800px virtual screen also clamps popups too far left for
    // a 1440px test window; restore the requested, visible viewport anchors.
    QImage menuCapture=main->grab().toImage();
    {
        QPainter painter(&menuCapture);
        for(QWidget* popup:{static_cast<QWidget*>(menu),static_cast<QWidget*>(opacityMenu)})if(popup){
            const QPoint offset=popup->mapToGlobal(QPoint())-main->mapToGlobal(QPoint());
            painter.save();painter.translate(offset);popup->render(&painter);painter.restore();
        }
    }
    check(menuCapture.save(out+"/fill-menu-1440.png"),"full client with anchored presets menu and 25-percent submenu captured");
    if(opacityMenu)opacityMenu->hide();menu->hide();
    for(auto* a:tool->menu()->actions())if(a->menu()&&a->text()==QStringLiteral("阴影浓度"))for(auto* option:a->menu()->actions())if(option->text().contains("40%"))option->trigger();
    check(UBSettings::settings()->value("Board/RegionFillOpacity").toInt()==40,"menu opacity choice is saved in the user configuration");
    {
        UBRegionFillController recreated(board);QToolBar temporary;recreated.addToToolbar(&temporary,nullptr);
        auto* button=temporary.findChild<QToolButton*>("regionFillToolButton");bool restoredPreset=false;
        for(auto* a:button->menu()->actions())if(a->menu()&&a->text()==QStringLiteral("阴影浓度"))for(auto* option:a->menu()->actions())if(option->isChecked()&&option->text().contains("40%"))restoredPreset=true;
        check(restoredPreset,"a newly created fill controller restores the saved menu opacity");
    }
    std::cout<<"PASS "<<checks<<" shape/fill checks"<<std::endl;
}
static void ready()
{
    if(QThread::currentThread()->loopLevel()==0 || !UBApplication::boardController || !UBApplication::boardController->activeScene()){QTimer::singleShot(100,qApp,ready);return;}
    try{shapeAndFillTests();std::_Exit(0);}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;std::_Exit(1);}
}
int main(int argc,char** argv)
{
    if(qEnvironmentVariable("QT_QPA_PLATFORM")!="offscreen")return 2;
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-shape-fill-tests",argc,argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");QApplication::setFont(QFont("Microsoft YaHei",9));
    const auto profile=qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if(profile.isEmpty() || QDir::cleanPath(UBSettings::userDataDirectory())!=QDir::cleanPath(profile))return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500,qApp,ready);QTimer::singleShot(120000,qApp,[]{std::_Exit(4);});return app.exec("");
}
