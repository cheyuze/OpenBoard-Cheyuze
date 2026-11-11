#define main oldPasteMain
#define runTests oldPasteTests
#define runWhenReady oldPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include <QDesktopServices>

class ReleaseUrlReceiver : public QObject
{
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void record(const QUrl& url) { urls << url; }
};

static QVariant javascript(QWebEnginePage* page, const QString& script)
{
    QEventLoop loop;QVariant result;bool completed=false;
    QTimer timer;timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);
    page->runJavaScript(script,[&](const QVariant& value){result=value;completed=true;loop.quit();});
    timer.start(10000);loop.exec();
    check(completed,"embedded release page JavaScript completed");return result;
}
static void tests()
{
    check(QCoreApplication::applicationVersion()==QStringLiteral("2.0.0"),"actual rebuilt client reports application version 2.0.0");
    auto* board=UBApplication::boardController;auto* main=UBApplication::mainWindow;
    UBApplication::applicationController->showBoard();main->resize(1440,900);main->setAnimated(false);
    auto* palette=board->paletteManager()->tipsPalette();auto* settings=UBSettings::settings();
    palette->hide();settings->appStartupHintsEnabled->setBool(false);settings->setValue("App/LastStartupHintsVersion","1.9.7");
    palette->showAtStartup();settle();
    check(palette->isVisible(),"upgrade displays new release notes even with recurring hints disabled");
    check(settings->value("App/LastStartupHintsVersion").toString()=="2.0.0"
              && !settings->appStartupHintsEnabled->get().toBool(),"new version is marked seen without changing the recurring checkbox");
    auto* web=palette->findChild<UBWebEngineView*>();check(web,"real startup-hints web view exists");
    QElapsedTimer ready;ready.start();
    while(ready.elapsed()<20000){
        if(javascript(web->page(),"document.readyState==='complete' && !!document.querySelector('a.contact')").toBool())break;
        QEventLoop wait;QTimer::singleShot(100,&wait,&QEventLoop::quit);wait.exec();
    }
    check(javascript(web->page(),"document.querySelector('h1').textContent").toString()==QStringLiteral("体验优化"),"embedded page displays the exact requested title");
    check(javascript(web->page(),"document.querySelector('.version').textContent").toString().contains("2.0.0"),"embedded page displays the new version");
    const QString form="https://yach-doc-shimo.zhiyinlou.com/forms/L9kBM1E4D6spX5qK/fill";
    const QString yach="yach://yach.zhiyinlou.com/session/p2p?sessionid=285108440437788672";
    check(javascript(web->page(),"document.querySelectorAll('a.contact').length").toInt()==3,"Yach contact, feedback form and workbench coexist");
    ReleaseUrlReceiver receiver;QDesktopServices::setUrlHandler("https",&receiver,"record");QDesktopServices::setUrlHandler("yach",&receiver,"record");
    javascript(web->page(),"document.querySelector('a[href^=\"https://yach-doc-shimo\"]').click();true");settle();
    check(receiver.urls.size()==1&&receiver.urls.last()==QUrl(form),"feedback click reaches the external-browser handler with the exact form URL");
    check(web->url().isLocalFile(),"feedback is not loaded or submitted inside the whiteboard");
    // Custom-protocol navigation requires a trusted renderer input gesture;
    // a JavaScript .click() can be rejected by Chromium before navigation.
    const auto card=javascript(web->page(),"(()=>{const r=document.querySelector('a[href^=\"yach:\"]').getBoundingClientRect();return [r.x+r.width/2,r.y+r.height/2]})()").toList();
    check(card.size()==2,"existing contact has a real clickable web-view rectangle");
    QWidget* input=web->focusProxy()?web->focusProxy():web;
    const QPointF point(card[0].toDouble(),card[1].toDouble());
    QMouseEvent press(QEvent::MouseButtonPress,point,input->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,point,input->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(input,&press);QApplication::sendEvent(input,&release);
    QEventLoop navigation;QTimer::singleShot(250,&navigation,&QEventLoop::quit);navigation.exec();
    check(receiver.urls.size()==2&&receiver.urls.last()==QUrl(yach),"existing Yach business card still reaches its original URL");
    QDesktopServices::unsetUrlHandler("https");QDesktopServices::unsetUrlHandler("yach");
    QEventLoop paint;QTimer::singleShot(800,&paint,&QEventLoop::quit);paint.exec();
    check(palette->grab().save(qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT")+"/hints-native-800.png"),"actual Qt hints container captured");
    palette->hide();palette->showAtStartup();settle();
    check(!palette->isVisible(),"same version does not force another display when recurring hints are disabled");
    check(UBStartupHintsPalette::shouldShowAtStartup(false,"2.0.0","2.0.1"),"future version still forces one display");
    std::cout<<"PASS "<<checks<<" release hints native checks"<<std::endl;
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
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-release-hints-tests",argc,argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QApplication::setFont(QFont("Microsoft YaHei UI",9));
    const auto profile=qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if(profile.isEmpty()||QDir::cleanPath(UBSettings::userDataDirectory())!=QDir::cleanPath(profile))return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500,qApp,ready);QTimer::singleShot(120000,qApp,[]{std::_Exit(4);});return app.exec("");
}
#include "build/release200-hints/release_hints_native.moc"
