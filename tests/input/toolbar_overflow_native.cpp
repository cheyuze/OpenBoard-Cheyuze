// Isolated complete-client regression for the actual toolbar disclosure.
#define main oldPasteMain
#define runTests oldPasteTests
#define runWhenReady oldPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "gui/UBUiTheme.h"
#include <QWindow>

static void windowMouse(QWidget* target,QEvent::Type type,QPointF point)
{
    auto* window=UBApplication::mainWindow->windowHandle();const QPointF global=target->mapToGlobal(point.toPoint());
    QMouseEvent event(type,window->mapFromGlobal(global),global,Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(window,&event);
}
static void tap(QWidget* target)
{windowMouse(target,QEvent::MouseButtonPress,target->rect().center());windowMouse(target,QEvent::MouseButtonRelease,target->rect().center());settle();}
static void sendKey(QWidget* target,int value)
{
    QKeyEvent press(QEvent::KeyPress,value,Qt::NoModifier);QApplication::sendEvent(target,&press);
    QKeyEvent release(QEvent::KeyRelease,value,Qt::NoModifier);QApplication::sendEvent(target,&release);settle();
}
static bool vertical(QMenu* menu)
{
    int x=-1,y=-1;
    for(auto* action:menu->actions()){
        if(!action->isVisible()||action->isSeparator())continue;
        const QRect row=menu->actionGeometry(action);if(row.isEmpty())continue;
        if(x>=0&&(row.x()!=x||row.y()<=y))return false;
        x=row.x();y=row.y();
    }
    return y>=0;
}
static QAction* sourceRow(QMenu* menu,QAction* original)
{
    for(auto* row:menu->actions())if(row==original||row->property("ubOverflowSourceAction").value<QObject*>()==original)return row;
    return nullptr;
}
static void capture(UBMainWindow* main,QToolButton* extension,QMenu* menu,const QString& path)
{
    // Offscreen Qt has no OS compositor and clamps popups to its synthetic
    // screen. Composite the real QMenu at the declared right/below anchor.
    const QPoint origin=main->mapToGlobal(QPoint());
    const QPoint anchor=extension->mapToGlobal(QPoint(extension->width()-menu->width(),extension->height()));
    const QPoint position=anchor-origin;
    QImage image=main->grab().toImage();{QPainter painter(&image);painter.translate(position);menu->render(&painter);}
    check(image.save(path),"full-client vertical overflow captured at the toolbar anchor");
}
static void tests()
{
    auto* board=UBApplication::boardController;auto* main=UBApplication::mainWindow;
    UBApplication::applicationController->showBoard();board->paletteManager()->tipsPalette()->hide();main->setAnimated(false);
    board->addScene();board->zoomRestore();main->resize(1440,900);settle();
    auto* toolbar=main->boardToolBar;auto* extension=toolbar->findChild<QToolButton*>("qt_toolbar_ext_button");
    auto* menu=toolbar->findChild<QMenu*>("ubBoardOverflowMenu");
    const QString out=qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT");
    check(extension&&menu,"existing More disclosure owns the vertical popup adapter");
    for(int width:{1440,1224,1024}){
        main->resize(width,900);settle();const int height=toolbar->height();
        const auto canvas=board->controlView()->geometry();
        check(extension->isVisible(),"More remains available when commands overflow");
        tap(extension);
        check(menu->isVisible()&&!menu->isEmpty(),"actual QWidgetWindow mouse click opens the overflow list");
        check(toolbar->height()==height&&board->controlView()->geometry()==canvas,"opening More never expands a second row or displaces the canvas");
        check(vertical(menu),"overflow entries form one vertical list");
        check(!menu->actions().first()->isSeparator()&&!menu->actions().last()->isSeparator(),"overflow has no leading or trailing separator");
        int expected=0,previous=-1;
        for(auto* action:toolbar->actions()){
            if(!action->isVisible()||action->isSeparator())continue;
            auto* button=qobject_cast<QToolButton*>(toolbar->widgetForAction(action));
            if(!button||button->isVisible())continue;
            if(button->menu()){
                bool found=false;for(auto* row:menu->actions())if(row->menu()==button->menu())found=true;
                check(found,"a hidden button retains its original submenu");
            }else{
                auto* row=sourceRow(menu,action);const int index=menu->actions().indexOf(row);
                check(row&&index>previous,"hidden plain actions retain source mapping and toolbar order");previous=index;
                check(row->shortcut().isEmpty(),"popup does not register a duplicate shortcut");
            }
            ++expected;
        }
        check(expected>0,"the list includes actual overflowed commands rather than a fixed catalog");
        capture(main,extension,menu,out+QStringLiteral("/overflow-%1.png").arg(width));
        sendKey(menu,Qt::Key_Escape);check(!menu->isVisible()&&toolbar->height()==height,"Escape dismisses without leaving toolbar expansion state");
        extension->setFocus();sendKey(extension,Qt::Key_Down);
        check(menu->isVisible()&&menu->activeAction(),"keyboard Down opens the list with an active row");
        sendKey(menu,Qt::Key_Escape);
        extension->setFocus();sendKey(extension,Qt::Key_Space);check(menu->isVisible(),"keyboard Space opens More");sendKey(menu,Qt::Key_Escape);
        extension->setFocus();sendKey(extension,Qt::Key_Return);check(menu->isVisible(),"keyboard Enter opens More");sendKey(menu,Qt::Key_Escape);
    }
    main->resize(1440,900);settle();
    const int originalHeight=toolbar->height();bool accessibleOpened=false;
    QTimer::singleShot(0,menu,[&]{accessibleOpened=menu->isVisible()&&!menu->isEmpty();menu->hide();});
    extension->click();settle();
    check(accessibleOpened&&toolbar->height()==originalHeight,"programmatic accessibility activation also opens a menu, never expansion");

    // Harmless synthetic actions verify real dispatch without recording,
    // opening external pages, or changing any installed-user document.
    int clicks=0;auto* probe=new QAction(UBUiTheme::icon("star"),QStringLiteral("测试操作"),toolbar);
    probe->setObjectName("overflowProbe");probe->setCheckable(true);
    probe->setProperty("ubToolbarFullText",QStringLiteral("测试操作的完整名称"));probe->setText(QStringLiteral("测试操..."));
    QObject::connect(probe,&QAction::triggered,toolbar,[&]{++clicks;});toolbar->addAction(probe);
    auto* unavailable=new QAction(QStringLiteral("不可用测试操作"),toolbar);unavailable->setEnabled(false);toolbar->addAction(unavailable);
    auto* hidden=new QAction(QStringLiteral("不应显示的测试操作"),toolbar);toolbar->addAction(hidden);hidden->setVisible(false);
    auto* options=new QToolButton;options->setText(QStringLiteral("测试子菜单"));options->setPopupMode(QToolButton::InstantPopup);
    auto* optionsMenu=new QMenu(options);auto* choice=optionsMenu->addAction(QStringLiteral("选择测试项"));
    int choices=0;QObject::connect(choice,&QAction::triggered,toolbar,[&]{++choices;});options->setMenu(optionsMenu);
    auto* optionsAction=toolbar->addWidget(options);
    auto* properties=new QWidget;auto* propertyLayout=new QHBoxLayout(properties);
    auto* propertySlider=new QSlider(Qt::Horizontal,properties);propertySlider->setRange(0,100);propertySlider->setValue(25);propertyLayout->addWidget(propertySlider);
    auto* propertiesAction=toolbar->addWidget(properties);
    // Direct-widget buttons must not be stolen from their QWidgetAction.
    auto* customButton=new QToolButton;customButton->setText(QStringLiteral("测试复合按钮"));int customClicks=0;
    QObject::connect(customButton,&QToolButton::clicked,toolbar,[&]{++customClicks;});auto* customAction=toolbar->addWidget(customButton);
    main->resize(1024,900);settle();
    tap(extension);check(sourceRow(menu,probe)&&sourceRow(menu,unavailable)&&!sourceRow(menu,hidden),"popup preserves enabled/disabled/hidden action states");
    check(!sourceRow(menu,unavailable)->isEnabled(),"disabled action is still disabled in the popup");
    check(sourceRow(menu,probe)->text()==QStringLiteral("测试操作的完整名称")&&probe->text()==QStringLiteral("测试操..."),"popup restores a complete label without mutating the toolbar abbreviation");
    const QPoint row=menu->actionGeometry(sourceRow(menu,probe)).center();
    QMouseEvent press(QEvent::MouseButtonPress,row,menu->mapToGlobal(row),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,row,menu->mapToGlobal(row),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(menu,&press);QApplication::sendEvent(menu,&release);settle();
    check(clicks==1&&probe->isChecked()&&!menu->isVisible(),"clicking a menu row triggers the original action exactly once and keeps its check state");
    tap(extension);check(probe->isChecked()&&sourceRow(menu,probe)->isChecked(),"reopening reflects the current checked state");
    QAction* optionsRow=nullptr;QAction* customRow=nullptr;
    for(auto* entry:menu->actions()){
        if(entry->menu()==optionsMenu)optionsRow=entry;
        if(entry->text()==customButton->text())customRow=entry;
    }
    check(optionsRow&&options->parentWidget()==toolbar,"submenu is retained without reparenting its toolbar button");
    check(customRow&&customButton->parentWidget()==toolbar,"custom QWidgetAction button is represented without being moved");
    auto* copy=menu->findChild<QSlider*>("ubOverflowSlider");
    check(copy&&copy->value()==25,"hidden continuous slider remains available in the list");
    copy->setValue(63);check(propertySlider->value()==63,"menu slider forwards to the original property control");
    propertySlider->setValue(41);check(copy->value()==41,"original property changes update the open menu slider");
    customRow->trigger();check(customClicks==1,"custom menu row retains the original button click signal path");
    menu->hide();optionsMenu->popup(QPoint(20,20));settle();
    check(optionsMenu->actions().contains(choice),"existing submenu contents stay intact");choice->trigger();optionsMenu->hide();check(choices==1,"submenu still invokes its original action");
    for(int i=0;i<10;++i){tap(extension);menu->hide();}
    check(toolbar->findChildren<QObject*>("ubBoardOverflowController",Qt::FindDirectChildrenOnly).size()==1,"reopening never installs duplicate controllers");
    UBUiTheme::normalizeBoardToolbar(toolbar,true);UBUiTheme::normalizeBoardToolbar(toolbar,true);settle();
    check(toolbar->findChildren<QObject*>("ubBoardOverflowController",Qt::FindDirectChildrenOnly).size()==1,"toolbar text normalization does not duplicate the adapter");
    // A tall catalog must scroll in one column rather than changing to a grid.
    QList<QAction*> extras;
    for(int i=0;i<35;++i)extras<<toolbar->addAction(QStringLiteral("测试条目 %1").arg(i+1));
    settle();tap(extension);check(vertical(menu),"large overflow remains a scrollable single column");
    check(menu->height()<=menu->screen()->availableGeometry().height(),"long menu stays within the available screen height");menu->hide();
    for(auto* entry:extras){toolbar->removeAction(entry);delete entry;}
    toolbar->removeAction(probe);toolbar->removeAction(unavailable);toolbar->removeAction(hidden);
    toolbar->removeAction(optionsAction);toolbar->removeAction(propertiesAction);toolbar->removeAction(customAction);
    main->resize(2325,900);settle();
    check(!extension->isVisible(),"wide toolbar hides More when all commands fit");
    check(main->grab().save(out+"/overflow-wide-2325.png"),"wide single-row toolbar captured");
    main->resize(1440,900);settle();tap(extension);
    check(!sourceRow(menu,probe)&&!sourceRow(menu,unavailable),"resizing and removing actions cannot leave stale menu entries");menu->hide();
    UBUiTheme::normalizeBoardToolbar(toolbar,false);settle();
    if(extension->isVisible()){tap(extension);check(menu->isVisible()&&vertical(menu),"icon-only toolbar still uses a vertical overflow list");}
    menu->hide();UBUiTheme::normalizeBoardToolbar(toolbar,true);settle();
    std::cout<<"PASS "<<checks<<" toolbar overflow checks"<<std::endl;
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
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-toolbar-overflow-tests",argc,argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QApplication::setFont(QFont("Microsoft YaHei UI",9));
    const auto profile=qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if(profile.isEmpty()||QDir::cleanPath(UBSettings::userDataDirectory())!=QDir::cleanPath(profile))return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500,qApp,ready);QTimer::singleShot(120000,qApp,[]{std::_Exit(4);});return app.exec("");
}
