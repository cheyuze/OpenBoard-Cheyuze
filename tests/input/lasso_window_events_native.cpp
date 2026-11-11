// Exercise the transport-window -> viewport route, not just viewport delivery.
#define main oldPasteMain
#define runTests oldPasteTests
#define runWhenReady oldPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "board/UBRegionFillController.h"
#include <QWindow>
#include <cmath>

static int svgCount(const std::shared_ptr<UBGraphicsScene>& scene)
{int n=0;for(auto* item:scene->items())if(dynamic_cast<UBGraphicsSvgItem*>(item))++n;return n;}
static QColor viewportColor(QWidget* viewport,QPointF logical)
{
    const QImage image=viewport->grab().toImage();
    const qreal dpr=image.devicePixelRatio();
    return image.pixelColor(qBound(0,qRound(logical.x()*dpr),image.width()-1),
        qBound(0,qRound(logical.y()*dpr),image.height()-1));
}
static void nativeMouse(QWindow* window,QEvent::Type type,QPointF global)
{
    const QPointF local=window->mapFromGlobal(global);
    QMouseEvent event(type,local,global,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(window,&event);
}
static void tests()
{
    auto* board=UBApplication::boardController;auto* main=UBApplication::mainWindow;
    UBApplication::applicationController->showBoard();board->paletteManager()->tipsPalette()->hide();main->resize(1200,900);
    board->addScene();board->setBorderlessPageSize(true);board->zoomRestore();settle();
    auto* view=board->controlView();auto* fill=board->regionFillController();auto scene=board->activeScene();
    auto* window=main->windowHandle();check(window,"real native QWidgetWindow exists");
    main->activateWindow();view->setFocus();settle();
    auto* stack=UBApplication::undoStack.data();const int before=stack->index();const int objects=svgCount(scene);
    const QColor beforeColor=viewportColor(view->viewport(),view->viewport()->rect().center());
    fill->start(UBRegionFillController::Lasso);
    const QPointF center=view->viewport()->rect().center();
    for(int i=0;i<60;++i){
        const qreal angle=i*2*3.141592653589793/60;
        const QPointF point=center+QPointF(100*std::cos(angle),70*std::sin(angle));
        nativeMouse(window,i==0?QEvent::MouseButtonPress:QEvent::MouseMove,view->viewport()->mapToGlobal(point.toPoint()));
    }
    check(fill->hasPreview(),"window-routed mouse movement produces the actual lasso preview");
    nativeMouse(window,QEvent::MouseButtonRelease,view->viewport()->mapToGlobal((center+QPointF(100,0)).toPoint()));
    settle();
    std::cout<<"after window release: preview="<<fill->hasPreview()<<" objects="<<svgCount(scene)-objects<<" undo="<<stack->index()-before<<std::endl;
    check(svgCount(scene)==objects+1&&stack->index()==before+1,"window-routed mouse release commits one fill instead of discarding it");
    const QColor afterColor=viewportColor(view->viewport(),center);
    check(afterColor!=beforeColor&&afterColor.red()<230,"committed fill remains visibly shaded at the circled center after release");
    const QString output=qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT");
    if(!output.isEmpty())check(main->grab().save(output+"/mouse-release-committed.png"),"post-release shading captured from the real client viewport");
    check(fill->isActive()&&!fill->hasPreview(),"committed mouse shading stays ready without a transient preview");
    board->undo();check(svgCount(scene)==objects,"one undo removes window-routed fill");
    std::cout<<"PASS "<<checks<<" native-window lasso checks"<<std::endl;
}
static void ready()
{
    if(QThread::currentThread()->loopLevel()==0||!UBApplication::boardController||!UBApplication::boardController->activeScene()){QTimer::singleShot(100,qApp,ready);return;}
    try{tests();std::_Exit(0);}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;std::_Exit(1);}
}
int main(int argc,char** argv)
{
    if(qEnvironmentVariable("QT_QPA_PLATFORM")!="offscreen")return 2;
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-lasso-window-tests",argc,argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QApplication::setFont(QFont("Microsoft YaHei UI",9));
    const auto profile=qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if(profile.isEmpty()||QDir::cleanPath(UBSettings::userDataDirectory())!=QDir::cleanPath(profile))return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500,qApp,ready);QTimer::singleShot(90000,qApp,[]{std::_Exit(4);});return app.exec("");
}
