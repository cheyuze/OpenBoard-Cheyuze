// Full-client integration: actual QWidgetWindow input routing, isolated data.
#define main oldPasteMain
#define runTests oldPasteTests
#define runWhenReady oldPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "board/UBRegionFillController.h"
#include "adaptors/UBSvgSubsetAdaptor.h"
#include <QWindow>

static QList<UBGraphicsSvgItem*> shades(const std::shared_ptr<UBGraphicsScene>& scene)
{
    QList<UBGraphicsSvgItem*> result;for(auto* item:scene->items())if(auto* svg=dynamic_cast<UBGraphicsSvgItem*>(item))result<<svg;return result;
}
static void sendMouse(QWidget* widget,QEvent::Type type,QPointF point,Qt::MouseButton button=Qt::LeftButton)
{
    QMouseEvent event(type,point,widget->mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:button,
        type==QEvent::MouseMove||type==QEvent::MouseButtonRelease?Qt::NoButton:button,Qt::NoModifier);
    QApplication::sendEvent(widget,&event);
}
static void windowMouse(QWidget* target,QEvent::Type type,QPointF point,Qt::MouseButton button=Qt::LeftButton)
{
    auto* window=UBApplication::mainWindow->windowHandle();
    const QPointF global=target->mapToGlobal(point.toPoint());
    QMouseEvent event(type,window->mapFromGlobal(global),global,type==QEvent::MouseMove?Qt::NoButton:button,
        type==QEvent::MouseMove||type==QEvent::MouseButtonRelease?Qt::NoButton:button,Qt::NoModifier);
    QApplication::sendEvent(window,&event);
}
static void tap(UBBoardView* view,QPointF point,Qt::MouseButton button=Qt::LeftButton)
{windowMouse(view->viewport(),QEvent::MouseButtonPress,point,button);windowMouse(view->viewport(),QEvent::MouseButtonRelease,point,button);}
static void key(UBBoardView* view,int value,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    UBApplication::mainWindow->activateWindow();view->setFocus();settle();
    QKeyEvent press(QEvent::KeyPress,value,modifiers);QApplication::sendEvent(view->viewport(),&press);
    QKeyEvent release(QEvent::KeyRelease,value,modifiers);QApplication::sendEvent(view->viewport(),&release);
}
static QColor pixel(QWidget* view,QPointF point)
{
    const QImage image=view->grab().toImage();const qreal dpr=image.devicePixelRatio();
    return image.pixelColor(qBound(0,qRound(point.x()*dpr),image.width()-1),qBound(0,qRound(point.y()*dpr),image.height()-1));
}
static QImage documentRender(const std::shared_ptr<UBGraphicsScene>& scene,UBBoardView* view,UBGraphicsScene::RenderingContext context)
{
    QImage image(640,360,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter p(&image);
    const auto old=scene->renderingContext();scene->setRenderingContext(context);
    scene->render(&p,QRectF(0,0,640,360),view->mapToScene(view->viewport()->rect()).boundingRect(),Qt::IgnoreAspectRatio);
    scene->setRenderingContext(old);return image;
}
static UBGraphicsSvgItem* added(const std::shared_ptr<UBGraphicsScene>& scene,const QList<UBGraphicsSvgItem*>& before)
{for(auto* item:shades(scene))if(!before.contains(item))return item;return nullptr;}
static void tests()
{
    auto* board=UBApplication::boardController;auto* main=UBApplication::mainWindow;
    UBApplication::applicationController->showBoard();board->paletteManager()->tipsPalette()->hide();main->setAnimated(false);main->resize(1440,900);
    board->addScene();board->setBorderlessPageSize(true);board->zoomRestore();settle();
    auto scene=board->activeScene();auto* view=board->controlView();auto* stack=UBApplication::undoStack.data();
    auto* fill=board->regionFillController();auto* tool=main->findChild<QToolButton*>("regionFillToolButton");
    auto* action=tool->menu()->findChild<QAction*>("regionFillPolygonMode");
    const QString out=qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT");
    check(action&&tool->menu()->findChild<QAction*>("regionFillClickMode")&&tool->menu()->findChild<QAction*>("regionFillLassoMode"),"all three shading modes are exposed in the existing menu");
    check(tool->text()==QStringLiteral("圈选阴影"),"adding polygon mode does not change default lasso preference");
    const int buttonWidth=tool->width();
    tool->menu()->popup(tool->mapToGlobal(QPoint(0,tool->height())));settle();
    const QPoint row=tool->menu()->actionGeometry(action).center();sendMouse(tool->menu(),QEvent::MouseButtonPress,row);sendMouse(tool->menu(),QEvent::MouseButtonRelease,row);settle();
    check(fill->isActive()&&action->isChecked()&&tool->text()==QStringLiteral("多边形阴影"),"actual menu selection activates and names polygon shading");
    check(tool->width()==buttonWidth&&tool->width()>tool->fontMetrics().horizontalAdvance(tool->text())+12,"five-character label fits without resizing on mode change");
    const QPointF a(360,240),b(680,240),c(620,480),far(920,610),hover(300,520);
    for(qreal zoom:{.5,1.,2.}){
        view->setTransform(QTransform::fromScale(zoom,zoom));view->centerOn(3000,-1800);settle();
        const auto before=shades(scene);const int history=stack->index();
        tap(view,a);check(shades(scene)==before&&stack->index()==history,"first native-window click only anchors a vertex");
        tap(view,b);check(shades(scene)==before&&stack->index()==history,"second release does not prematurely fill");
        tap(view,c);check(fill->hasPreview()&&shades(scene)==before&&stack->index()==history,"third release still leaves an editable transient polygon");
        windowMouse(view->viewport(),QEvent::MouseMove,hover);
        tap(view,a+QPointF(5,2));settle();
        auto* item=added(scene,before);
        check(item&&stack->index()==history+1,"clicking near the start closes exactly one polygon under zoom/pan");
        check(pixel(view->viewport(),(a+b+c)/3).red()<230,"closed polygon remains visibly shaded after the native release");
        check(item->fileData().contains("fill-opacity=\"0.250\"")&&!item->sceneBoundingRect().contains(view->viewportTransform().inverted().map(hover)),"fill uses preset opacity and fixed vertices, not the hovered next point");
        check(fill->isActive()&&!fill->hasPreview(),"completed polygon leaves the tool ready with no transient border");
        const QByteArray data=item->fileData();board->undo();check(shades(scene)==before,"one undo removes the entire polygon");
        board->redo();check(shades(scene).contains(item)&&item->fileData()==data,"redo restores the exact same polygon vector");board->undo();
    }
    view->setTransform(QTransform());view->centerOn(0,0);settle();
    auto before=shades(scene);int history=stack->index();
    tap(view,a);tap(view,b);tap(view,c);
    windowMouse(view->viewport(),QEvent::MouseButtonDblClick,c);windowMouse(view->viewport(),QEvent::MouseButtonRelease,c);
    check(shades(scene).size()==before.size()+1&&stack->index()==history+1,"native double-click closes once without a duplicate vertex or pen stroke");board->undo();
    tap(view,a);tap(view,b);tap(view,c);tap(view,hover,Qt::RightButton);
    check(shades(scene).size()==before.size()+1&&fill->isActive(),"native right-click matches existing polygon completion behavior");board->undo();
    tap(view,a);tap(view,b);key(view,Qt::Key_Return);
    check(shades(scene)==before&&fill->hasPreview(),"two vertices cannot accidentally create a filled object");
    tap(view,far);key(view,Qt::Key_Backspace);tap(view,c);key(view,Qt::Key_Return);
    auto* existing=added(scene,before);
    check(existing&&!existing->sceneBoundingRect().contains(view->viewportTransform().inverted().map(far)),"Backspace removes the last draft vertex before Enter closes");
    const int existingHistory=stack->index();
    tap(view,a);tap(view,b);tap(view,far);key(view,Qt::Key_Z,Qt::ControlModifier);
    key(view,Qt::Key_Return);
    check(shades(scene).size()==before.size()+1&&stack->index()==existingHistory,"draft Ctrl+Z removes a vertex without undoing existing content");
    tap(view,c);key(view,Qt::Key_Return);check(shades(scene).size()==before.size()+2,"draft can continue and close after correcting a point");
    key(view,Qt::Key_Z,Qt::ControlModifier);check(shades(scene).size()==before.size()+1,"idle Ctrl+Z retains ordinary one-fill undo behavior");
    tap(view,a);tap(view,b);tap(view,far);
    auto* undo=main->findChild<QToolButton*>("ubBoardUndoButton");
    windowMouse(undo,QEvent::MouseButtonPress,undo->rect().center());windowMouse(undo,QEvent::MouseButtonRelease,undo->rect().center());
    key(view,Qt::Key_Return);check(shades(scene).size()==before.size()+1,"actual Undo control also corrects the draft rather than deleting existing shading");
    key(view,Qt::Key_Escape);check(fill->isActive()&&!fill->hasPreview()&&shades(scene).size()==before.size()+1,"Escape cancels only the unfinished polygon and preserves existing content");
    key(view,Qt::Key_Escape);check(!fill->isActive(),"idle Escape exits the polygon tool");
    action->trigger();tap(view,a);tap(view,(a+b)/2);tap(view,b);key(view,Qt::Key_Return);
    check(shades(scene).size()==before.size()+1,"collinear vertices are refused instead of saving invisible content");key(view,Qt::Key_Escape);
    const QImage recorded=documentRender(scene,view,UBGraphicsScene::Podcast);
    const QImage exported=documentRender(scene,view,UBGraphicsScene::NonScreen);
    tap(view,a);tap(view,b);tap(view,c);windowMouse(view->viewport(),QEvent::MouseMove,hover);
    check(documentRender(scene,view,UBGraphicsScene::Podcast)==recorded,"polygon vertices and closure feedback are excluded from recording content");
    check(documentRender(scene,view,UBGraphicsScene::NonScreen)==exported,"polygon vertices and closure feedback are excluded from export content");
    check(main->grab().save(out+"/polygon-building-1440.png"),"full-client polygon vertex and prospective-edge state captured");
    windowMouse(view->viewport(),QEvent::MouseMove,a+QPointF(3,2));
    check(main->grab().save(out+"/polygon-close-hint-1440.png"),"start-point closure highlight captured");key(view,Qt::Key_Escape);
    // Tablet taps add vertices, not continuous freehand samples.
    const QPointingDevice pen("polygon pen",834,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
    for(auto point:{a,b,c,a})for(auto type:{QEvent::TabletPress,QEvent::TabletRelease}){
        QTabletEvent event(type,&pen,point,view->viewport()->mapToGlobal(point.toPoint()),type==QEvent::TabletRelease?0:.5,
            0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton);
        QApplication::sendEvent(view->viewport(),&event);
    }
    check(shades(scene).size()==before.size()+2&&fill->isActive(),"synthetic pen taps close one polygon without freehand strokes");
    int strokes=0;for(auto* item:scene->items())if(dynamic_cast<UBGraphicsStrokesGroup*>(item))++strokes;
    check(strokes==0,"no underlying pen strokes leak through polygon mouse or pen events");
    check(main->grab().save(out+"/polygon-committed-1440.png"),"committed polygon remains visible without preview controls");
    UBSvgSubsetAdaptor::persistScene(board->selectedDocument(),scene,board->activeSceneIndex());
    auto restored=UBSvgSubsetAdaptor::loadScene(board->selectedDocument(),board->activeSceneIndex());
    check(shades(restored).size()==shades(scene).size(),"polygon shading survives normal document save/reopen");
    {UBRegionFillController fresh(board);QToolBar temporary;fresh.addToToolbar(&temporary,nullptr);
        auto* button=temporary.findChild<QToolButton*>("regionFillToolButton");
        check(button->text()==QStringLiteral("多边形阴影")&&button->menu()->findChild<QAction*>("regionFillPolygonMode")->isChecked(),"a fresh controller restores explicitly chosen polygon mode");}
    for(int width:{1224,1024}){main->resize(width,768);settle();
        check(tool->width()>tool->fontMetrics().horizontalAdvance(tool->text())+12,"polygon label remains uncut in narrow native overflow layout");
        check(main->grab().save(out+QStringLiteral("/polygon-toolbar-%1.png").arg(width)),"narrow polygon toolbar captured");}
    main->resize(1440,900);settle();
    auto* menu=tool->menu();const QPoint anchor=tool->mapToGlobal(QPoint(0,tool->height()));menu->popup(anchor);settle();menu->move(anchor);
    QImage capture=main->grab().toImage();{QPainter p(&capture);p.translate(menu->mapToGlobal(QPoint())-main->mapToGlobal(QPoint()));menu->render(&p);}
    check(capture.save(out+"/polygon-menu-1440.png"),"full-client three-mode menu captured");menu->hide();
    tap(view,a);tap(view,b);main->actionPen->trigger();
    check(!fill->isActive()&&!fill->hasPreview(),"selecting the underlying pen discards an unfinished polygon safely");
    action->trigger();tap(view,a);tap(view,b);board->addScene();check(!fill->isActive(),"page change cancels pending polygon state");
    std::cout<<"PASS "<<checks<<" polygon shading checks"<<std::endl;
}
static void ready()
{
    if(QThread::currentThread()->loopLevel()==0||!UBApplication::boardController||!UBApplication::boardController->activeScene()){QTimer::singleShot(100,qApp,ready);return;}
    try{tests();std::_Exit(0);}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;std::_Exit(1);}
}
int main(int argc,char** argv)
{
    if(qEnvironmentVariable("QT_QPA_PLATFORM")!="offscreen")return 2;
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-polygon-shade-tests",argc,argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");QApplication::setFont(QFont("Microsoft YaHei UI",9));
    const auto profile=qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if(profile.isEmpty()||QDir::cleanPath(UBSettings::userDataDirectory())!=QDir::cleanPath(profile))return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500,qApp,ready);QTimer::singleShot(120000,qApp,[]{std::_Exit(4);});return app.exec("");
}
