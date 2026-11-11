// Full-client diagnostic of the user-visible shading workflow, with an isolated profile.
#define main previousPasteMain
#define runTests previousPasteTests
#define runWhenReady previousPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "board/UBRegionFillController.h"
#include <cmath>

static QList<UBGraphicsSvgItem*> fills(const std::shared_ptr<UBGraphicsScene>& scene)
{
    QList<UBGraphicsSvgItem*> result;
    for(auto* item:scene->items())if(auto* svg=dynamic_cast<UBGraphicsSvgItem*>(item))result<<svg;
    return result;
}
static void sendMouse(QWidget* widget,QEvent::Type type,QPointF point)
{
    QMouseEvent event(type,point,widget->mapToGlobal(point.toPoint()),
        type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(widget,&event);
}
static void stroke(UBBoardView* view,QPointF center)
{
    for(int i=0;i<=120;++i){
        const qreal angle=i*2*3.141592653589793/120;
        const QPointF point=center+QPointF(90*std::cos(angle),65*std::sin(angle));
        sendMouse(view->viewport(),i==0?QEvent::MouseButtonPress:i==120?QEvent::MouseButtonRelease:QEvent::MouseMove,point);
    }
}
static void interactionTests()
{
    auto* board=UBApplication::boardController;auto* main=UBApplication::mainWindow;
    UBApplication::applicationController->showBoard();board->paletteManager()->tipsPalette()->hide();main->setAnimated(false);main->resize(1440,900);
    board->addScene();board->setBorderlessPageSize(true);board->zoomRestore();settle();
    auto scene=board->activeScene();auto* view=board->controlView();auto* fill=board->regionFillController();
    auto* tool=main->findChild<QToolButton*>("regionFillToolButton");
    auto* lasso=tool->menu()->findChild<QAction*>("regionFillLassoMode");
    auto* click=tool->menu()->findChild<QAction*>("regionFillClickMode");
    const QString output=qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT");
    check(lasso&&click&&lasso->isChecked(),"first use defaults to the user's own lasso range");
    check(tool->text()==QStringLiteral("圈选阴影"),"button exposes the current fill mode");
    check(main->findChild<QAction*>("actionBoardNewDocument")->text()==QStringLiteral("新建文档")&&main->actionPages->text()==QStringLiteral("创建页面"),"document and page creation have distinct full labels");
    main->actionPen->trigger();
    sendMouse(tool,QEvent::MouseButtonPress,{tool->width()/2.,16});
    sendMouse(tool,QEvent::MouseButtonRelease,{tool->width()/2.,16});
    check(fill->isActive()&&tool->isChecked()&&!main->actionPen->isChecked(),"icon body activates lasso with a single exclusive selected tool");
    bool menuOpened=false;
    QTimer::singleShot(0,qApp,[&]{menuOpened=QApplication::activePopupWidget()==tool->menu();tool->menu()->hide();});
    sendMouse(tool,QEvent::MouseButtonPress,{tool->width()-9.,tool->height()-13.});
    sendMouse(tool,QEvent::MouseButtonRelease,{tool->width()-9.,tool->height()-13.});
    check(menuOpened&&fill->isActive(),"label-side arrow opens presets without leaving the active tool");
    tool->menu()->popup(tool->mapToGlobal(QPoint(0,tool->height())));settle();
    const QPoint row=tool->menu()->actionGeometry(click).center();
    sendMouse(tool->menu(),QEvent::MouseButtonPress,row);sendMouse(tool->menu(),QEvent::MouseButtonRelease,row);settle();
    check(fill->isActive()&&tool->text()==QStringLiteral("点击阴影")&&click->isChecked(),"actual menu release switches and names click fill");
    lasso->trigger();
    check(fill->isActive()&&tool->text()==QStringLiteral("圈选阴影")&&lasso->isChecked(),"switching back names and arms lasso");
    for(qreal zoom:{.5,1.,2.}){
        view->setTransform(QTransform::fromScale(zoom,zoom));view->centerOn(3000,-1800);settle();
        const QPointF center(view->viewport()->width()*.55,view->viewport()->height()*.55);
        const auto before=fills(scene);const int history=UBApplication::undoStack->index();
        stroke(view,center);
        UBGraphicsSvgItem* made=nullptr;for(auto* svg:fills(scene))if(!before.contains(svg))made=svg;
        check(made&&UBApplication::undoStack->index()==history+1,"dense loop commits exactly one object under pan/zoom");
        check(made->sceneBoundingRect().contains(view->viewportTransform().inverted().map(center)),"shading stays at the circled viewport location");
        check(made->fileData().contains("fill-opacity=\"0.250\""),"direct fill preserves preset 25 percent opacity");
        check(fill->isActive()&&tool->isChecked()&&!main->actionPen->isChecked(),"completed fill remains ready with no pen highlight");
        stroke(view,center+QPointF(-180,0));
        check(fills(scene).size()==before.size()+2,"a second loop works without reselecting the tool");
        auto* undoButton=main->findChild<QToolButton*>("ubBoardUndoButton");
        sendMouse(undoButton,QEvent::MouseButtonPress,undoButton->rect().center());
        sendMouse(undoButton,QEvent::MouseButtonRelease,undoButton->rect().center());
        check(fills(scene).size()==before.size()+1&&fill->isActive(),"one toolbar undo removes only the latest fill while staying ready");
        main->actionRedo->trigger();check(fills(scene).size()==before.size()+2,"redo restores only the latest fill");
        main->actionUndo->trigger();main->actionUndo->trigger();
        check(fills(scene)==before&&fill->isActive(),"two undo operations restore the starting page without exiting");
    }
    view->setTransform(QTransform());view->centerOn(0,0);settle();
    const QPointF center(view->viewport()->width()*.5,view->viewport()->height()*.5);
    for(int i=0;i<=96;++i){
        const qreal angle=i*2*3.141592653589793/120;
        sendMouse(view->viewport(),i==0?QEvent::MouseButtonPress:QEvent::MouseMove,
            center+QPointF(110*std::cos(angle),70*std::sin(angle)));
    }
    check(fill->hasPreview()&&fills(scene).isEmpty(),"drag feedback is transient until release");
    check(main->grab().save(output+"/lasso-drag-1440.png"),"continuous lasso and dashed closure captured in full client");
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&escape);
    check(!fill->isActive()&&!fill->hasPreview()&&main->actionPen->isChecked(),"Escape discards drag and restores the previous tool highlight");
    tool->click();main->actionPen->trigger();
    check(!fill->isActive()&&main->actionPen->isChecked(),"choosing the same underlying pen still exits shading");
    tool->click();main->actionSelector->trigger();
    check(!fill->isActive()&&main->actionSelector->isChecked()&&!main->actionPen->isChecked(),"another tool wins without rechecking the old tool");
    tool->click();tool->click();check(!fill->isActive()&&main->actionSelector->isChecked(),"clicking the selected shading body toggles off and restores selection");
    tool->click();
    const QPointingDevice pen("lasso pen",827,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
    const QList<QPointF> points={center,center+QPointF(50,0),center+QPointF(50,50),center+QPointF(0,50)};
    for(int i=0;i<points.size();++i){
        QTabletEvent event(i==0?QEvent::TabletPress:i==points.size()-1?QEvent::TabletRelease:QEvent::TabletMove,
            &pen,points[i],view->viewport()->mapToGlobal(points[i].toPoint()),i==points.size()-1?0:.5,0,0,0,0,0,
            Qt::NoModifier,Qt::LeftButton,i==points.size()-1?Qt::NoButton:Qt::LeftButton);
        QApplication::sendEvent(view->viewport(),&event);
    }
    check(fills(scene).size()==1&&fill->isActive(),"synthetic pen release commits once and stays in shading");
    main->activateWindow();view->setFocus();settle();
    QKeyEvent undo(QEvent::KeyPress,Qt::Key_Z,Qt::ControlModifier);QApplication::sendEvent(view->viewport(),&undo);
    QKeyEvent undoRelease(QEvent::KeyRelease,Qt::Key_Z,Qt::ControlModifier);QApplication::sendEvent(view->viewport(),&undoRelease);
    check(fill->isActive()&&fills(scene).isEmpty(),"Ctrl+Z removes the latest fill without unexpectedly leaving shading");
    QDialog modal(main);modal.setWindowModality(Qt::ApplicationModal);modal.show();settle();
    QKeyEvent dismiss(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&modal,&dismiss);
    check(!modal.isVisible()&&fill->isActive(),"Escape dismisses a settings dialog without being stolen by shading");
    fill->cancel();
    for(int width:{1440,1224,1024}){
        main->resize(width,width==1440?900:768);settle();
        auto* more=main->boardToolBar->findChild<QToolButton*>("qt_toolbar_ext_button");
        check(more&&more->text()==QStringLiteral("更\n多")&&more->accessibleName()==QStringLiteral("更多工具"),"native overflow is explicitly named More");
        if(more->isVisible())check(more->width()>=16 && more->mapTo(main,QPoint(more->width(),0)).x()<=main->width(),"vertical More label fits inside the visible client edge");
        check(main->grab().save(output+QStringLiteral("/toolbar-%1.png").arg(width)),"actual full toolbar state captured at supported window width");
        if(width==1024&&more->isVisible()){
            bool captured=false;bool exposed=false;
            QEventLoop finish;
            QTimer::singleShot(1000,qApp,[&]{
                QImage overflow=main->grab().toImage();
                if(auto* popup=QApplication::activePopupWidget()){
                    exposed=true;
                    const QPoint anchor=more->mapToGlobal(QPoint(more->width()-popup->width(),more->height()));
                    popup->move(anchor);QPainter p(&overflow);p.translate(anchor-main->mapToGlobal(QPoint()));popup->render(&p);
                    p.end();popup->hide();
                }else {
                    auto* command=main->boardToolBar->widgetForAction(main->actionDesktop);
                    exposed=command&&command->isVisible()&&main->boardToolBar->rect().contains(command->geometry());
                }
                captured=overflow.save(output+"/toolbar-overflow-1024.png");finish.quit();
            });
            sendMouse(more,QEvent::MouseButtonPress,more->rect().center());
            sendMouse(more,QEvent::MouseButtonRelease,more->rect().center());
            if(!captured)finish.exec();
            check(exposed&&captured,"actual More press exposes hidden commands, captured in full client");
            if(more->isChecked())more->click();settle();
        }
    }
    main->resize(1440,900);settle();
    auto* menu=tool->menu();const QPoint anchor=tool->mapToGlobal(QPoint(0,tool->height()));menu->popup(anchor);settle();menu->move(anchor);
    QImage capture=main->grab().toImage();{QPainter p(&capture);p.translate(menu->mapToGlobal(QPoint())-main->mapToGlobal(QPoint()));menu->render(&p);}
    check(capture.save(output+"/lasso-menu-1440.png"),"full-client mode and preset menu evidence captured");menu->hide();
    std::cout<<"PASS "<<checks<<" lasso interaction checks"<<std::endl;
}
static void ready()
{
    if(QThread::currentThread()->loopLevel()==0||!UBApplication::boardController||!UBApplication::boardController->activeScene()){QTimer::singleShot(100,qApp,ready);return;}
    try{interactionTests();std::_Exit(0);}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;std::_Exit(1);}
}
int main(int argc,char** argv)
{
    if(qEnvironmentVariable("QT_QPA_PLATFORM")!="offscreen")return 2;
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-lasso-interaction",argc,argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QApplication::setFont(QFont("Microsoft YaHei UI",9));
    const auto profile=qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if(profile.isEmpty()||QDir::cleanPath(UBSettings::userDataDirectory())!=QDir::cleanPath(profile))return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500,qApp,ready);QTimer::singleShot(120000,qApp,[]{std::_Exit(4);});return app.exec("");
}
