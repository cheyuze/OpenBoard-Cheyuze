// Full-client icon-only regression. Never starts recording or changes tools.
#define main oldPasteMain
#define runTests oldPasteTests
#define runWhenReady oldPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "gui/UBUiTheme.h"

static int redPixels(const QImage& image)
{
    int count=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
        const auto color=image.pixelColor(x,y);
        if(color.alpha()>160&&color.red()>170&&color.red()>color.green()*1.6&&color.red()>color.blue()*1.4)++count;
    }
    return count;
}
static QImage iconImage(const QIcon& icon,int size,QIcon::Mode mode=QIcon::Normal,QIcon::State state=QIcon::Off,qreal dpr=1)
{return icon.pixmap(QSize(size,size),dpr,mode,state).toImage();}
static void tests()
{
    auto* board=UBApplication::boardController;auto* main=UBApplication::mainWindow;
    UBApplication::applicationController->showBoard();board->paletteManager()->tipsPalette()->hide();main->setAnimated(false);
    board->addScene();board->zoomRestore();main->resize(2325,900);settle();
    const QString out=qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT");
    const QIcon desktop=UBUiTheme::icon("monitor-record"),monitor=UBUiTheme::icon("monitor");
    check(!desktop.isNull()&&!monitor.isNull(),"desktop recording and generic monitor icons both resolve");
    for(auto mode:{QIcon::Normal,QIcon::Active,QIcon::Disabled})for(auto state:{QIcon::Off,QIcon::On})
        for(int size:{16,24,32,48})for(qreal dpr:{1.,2.}){
            const QImage image=iconImage(desktop,size,mode,state,dpr);
            const QPoint center(qRound(image.width()*17/24.),qRound(image.height()*8/24.));
            const auto color=image.pixelColor(center);
            check(!image.isNull()&&(mode==QIcon::Disabled
                ?redPixels(image)==0&&color.red()>150&&color.green()>150&&color.blue()>150
                :redPixels(image)>0&&color.red()>170&&color.red()>color.green()*1.6),
                "recording dot stays red when enabled and muted when disabled across icon size, mode, state and DPR");
        }
    check(redPixels(iconImage(monitor,24))==0&&redPixels(iconImage(monitor,24,QIcon::Active,QIcon::On))==0,
        "generic monitor retains its non-recording appearance");
    check(iconImage(main->actionDesktop->icon(),24)==iconImage(desktop,24),"actual Desktop QAction uses the new recording monitor");
    check(iconImage(main->actionPodcast->icon(),24)==iconImage(UBUiTheme::icon("record"),24),"main recording icon is unchanged");
    const QString label=main->actionDesktop->text(),tip=main->actionDesktop->toolTip();
    const bool checked=main->actionDesktop->isChecked(),enabled=main->actionDesktop->isEnabled();
    const int history=UBApplication::undoStack->index();
    UBUiTheme::applyActionIcons(main);
    check(main->actionDesktop->text()==label&&main->actionDesktop->toolTip()==tip
        &&main->actionDesktop->isChecked()==checked&&main->actionDesktop->isEnabled()==enabled
        &&UBApplication::undoStack->index()==history,"refreshing the icon changes no action semantics or document history");
    auto* button=qobject_cast<QToolButton*>(main->boardToolBar->widgetForAction(main->actionDesktop));
    check(button&&button->isVisible()&&redPixels(button->grab().toImage())>0,"actual visible toolbar button includes the red dot");
    check(button->grab().save(out+"/desktop-button.png"),"actual Desktop toolbar button captured");
    check(main->grab().save(out+"/desktop-toolbar-2325.png"),"full-client reference-width toolbar captured");
    main->resize(1440,900);settle();
    check(main->grab().save(out+"/desktop-toolbar-1440.png"),"full-client common-width toolbar captured");
    // Deterministic icon-state plate, not an OS screenshot or a new UI panel.
    QImage plate(480,120,QImage::Format_ARGB32_Premultiplied);plate.fill(Qt::white);
    {
        QPainter painter(&plate);painter.setFont(QFont("Microsoft YaHei UI",9));painter.setPen(UBUiTheme::ink());
        const QStringList labels{QStringLiteral("正常"),QStringLiteral("悬停"),QStringLiteral("选中"),QStringLiteral("禁用")};
        const QIcon::Mode modes[]{QIcon::Normal,QIcon::Active,QIcon::Normal,QIcon::Disabled};
        for(int i=0;i<4;++i){
            desktop.paint(&painter,QRect(i*120+36,16,48,48),Qt::AlignCenter,modes[i],i==2?QIcon::On:QIcon::Off);
            painter.drawText(QRect(i*120,78,120,24),Qt::AlignCenter,labels[i]);
        }
    }
    check(plate.save(out+"/desktop-icon-states.png"),"declared icon-state plate captured");
    std::cout<<"PASS "<<checks<<" desktop recording icon checks"<<std::endl;
}
static void ready()
{
    if(QThread::currentThread()->loopLevel()==0||!UBApplication::boardController||!UBApplication::boardController->activeScene()){
        QTimer::singleShot(100,qApp,ready);return;
    }
    try{tests();std::_Exit(0);}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;std::_Exit(1);}
}
int main(int argc,char** argv)
{
    if(qEnvironmentVariable("QT_QPA_PLATFORM")!="offscreen")return 2;
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-desktop-record-icon-tests",argc,argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QApplication::setFont(QFont("Microsoft YaHei UI",9));
    const auto profile=qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if(profile.isEmpty()||QDir::cleanPath(UBSettings::userDataDirectory())!=QDir::cleanPath(profile))return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);
    UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500,qApp,ready);QTimer::singleShot(120000,qApp,[]{std::_Exit(4);});return app.exec("");
}
