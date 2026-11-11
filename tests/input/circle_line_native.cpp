#define main oldPasteMain
#define runTests oldPasteTests
#define runWhenReady oldPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "domain/UBGraphicsDelegateFrame.h"
#include "domain/UBGraphicsPixmapItemDelegate.h"
#include "domain/UBGraphicsPolygonItem.h"
#include "domain/UBLineTransform.h"
#include "adaptors/UBSvgSubsetAdaptor.h"
#include <QWindow>

static QList<UBGraphicsStrokesGroup*> groups(const std::shared_ptr<UBGraphicsScene>& scene)
{QList<UBGraphicsStrokesGroup*> result;for(auto* item:scene->items())if(auto* group=dynamic_cast<UBGraphicsStrokesGroup*>(item))result<<group;return result;}
static UBGraphicsStrokesGroup* created(const std::shared_ptr<UBGraphicsScene>& scene,const QList<UBGraphicsStrokesGroup*>& before)
{for(auto* group:groups(scene))if(!before.contains(group))return group;return nullptr;}
static QLineF endpoints(UBGraphicsStrokesGroup* item)
{const auto line=item->editableLine();return QLineF(item->mapToScene(line.p1()),item->mapToScene(line.p2()));}
static QRectF centerlineBounds(const std::shared_ptr<UBGraphicsScene>& scene)
{
    QPolygonF points;
    for(auto* item:scene->items())if(auto* polygon=dynamic_cast<UBGraphicsPolygonItem*>(item))
        if(polygon->isNominalLine())points<<polygon->mapToScene(polygon->originalLine().p1())<<polygon->mapToScene(polygon->originalLine().p2());
    return points.boundingRect();
}
static void windowEvent(QWidget* target,QEvent::Type type,QPointF point,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    auto* window=UBApplication::mainWindow->windowHandle();const QPointF global=target->mapToGlobal(point.toPoint());
    QMouseEvent event(type,window->mapFromGlobal(global),global,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,modifiers);
    QApplication::sendEvent(window,&event);
}
static void drag(UBBoardView* view,QPointF first,QPointF last,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    windowEvent(view->viewport(),QEvent::MouseButtonPress,view->mapFromScene(first),modifiers);
    for(int i=1;i<=8;++i)windowEvent(view->viewport(),QEvent::MouseMove,view->mapFromScene(first+(last-first)*i/8),modifiers);
    windowEvent(view->viewport(),QEvent::MouseButtonRelease,view->mapFromScene(last),modifiers);settle();
}
static void frameDrag(const std::shared_ptr<UBGraphicsScene>& scene,QGraphicsItem* frame,QPointF first,QPointF last,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    for(auto type:{QEvent::GraphicsSceneMousePress,QEvent::GraphicsSceneMouseMove,QEvent::GraphicsSceneMouseRelease}){
        QGraphicsSceneMouseEvent event(type);event.setScenePos(type==QEvent::GraphicsSceneMousePress?first:last);
        event.setPos(frame->mapFromScene(event.scenePos()));event.setButton(Qt::LeftButton);
        event.setModifiers(modifiers);
        event.setButtons(type==QEvent::GraphicsSceneMouseRelease?Qt::NoButton:Qt::LeftButton);scene->sendEvent(frame,&event);
    }
    settle();
}
static QImage content(const std::shared_ptr<UBGraphicsScene>& scene,UBGraphicsScene::RenderingContext context)
{
    QImage image(960,540,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);
    const auto old=scene->renderingContext();scene->setRenderingContext(context);
    scene->render(&painter,QRectF(0,0,960,540),QRectF(-480,-270,960,540));scene->setRenderingContext(old);return image;
}
static void tests()
{
    auto* board=UBApplication::boardController;auto* main=UBApplication::mainWindow;auto* draw=UBDrawingController::drawingController();
    UBApplication::applicationController->showBoard();board->paletteManager()->tipsPalette()->hide();main->setAnimated(false);main->resize(1440,900);
    board->addScene();board->setBorderlessPageSize(true);board->zoomRestore();auto scene=board->activeScene();auto* view=board->controlView();settle();
    auto* stack=UBApplication::undoStack.data();const QString out=qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT");
    draw->setLinePattern(UBDrawingController::SolidLinePattern);draw->setShapeFillEnabled(false);
    const QPointF center(-80,-40);
    for(auto delta:{QPointF(120,0),QPointF(0,95),QPointF(-70,-80),QPointF(80,-60),QPointF(-80,60)}){
        draw->setLineGeometryMode(UBDrawingController::CircleGeometry);const auto before=groups(scene);const int history=stack->index();
        scene->inputDevicePress(center);
        for(int i=1;i<=4;++i){
            scene->inputDeviceMove(center+delta*i/4);const QRectF bounds=centerlineBounds(scene);
            check(QLineF(bounds.center(),center).length()<.001,"circle center stays at the press point throughout the preview");
        }
        scene->inputDeviceRelease(UBStylusTool::Line);auto* circle=created(scene,before);
        const QRectF bounds=centerlineBounds(scene);const qreal radius=QLineF(QPointF(),delta).length();
        check(circle&&!circle->hasEditableLine()&&stack->index()==history+1,"circle is one real shape and one undo step, never mistaken for a line");
        check(QLineF(bounds.center(),center).length()<.001&&qAbs(bounds.width()-2*radius)<.001&&qAbs(bounds.height()-2*radius)<.001,"finished circle has the fixed center and exact pointer-distance radius in every direction");
        board->undo();check(groups(scene)==before,"one undo removes the whole circle");board->redo();check(groups(scene).contains(circle),"redo restores the circle");board->undo();
    }
    draw->setLineGeometryMode(UBDrawingController::EllipseGeometry);scene->inputDevicePress({-180,-80});scene->inputDeviceMove({140,60});scene->inputDeviceRelease(UBStylusTool::Line);
    auto bounds=centerlineBounds(scene);check(qAbs(bounds.width()-320)<.001&&qAbs(bounds.height()-140)<.001,"separate ellipse retains bounding-box drawing rather than removing the old capability");board->undo();
    draw->setLineGeometryMode(UBDrawingController::CircleGeometry);
    const QPointF a=view->mapToScene(QPoint(360,260)),b=view->mapToScene(QPoint(460,320));
    drag(view,a,b);bounds=centerlineBounds(scene);
    check(QLineF(bounds.center(),a).length()<1&&qAbs(bounds.width()-2*QLineF(a,b).length())<2,"actual QWidgetWindow mouse drawing follows the same center-radius behavior");board->undo();

    UBGraphicsStrokesGroup* line=nullptr;UBGraphicsPixmapItemDelegate* delegate=nullptr;
    for(int pattern:{int(UBDrawingController::SolidLinePattern),int(UBDrawingController::DashedLinePattern)}){
        // The offscreen platform's synthetic screen is only 800x800 (smaller
        // in logical pixels at high DPI). Keep actual window input inside it;
        // the application's intentional absurd-point guard must remain on.
        view->centerOn(0,0);settle();
        draw->setLineGeometryMode(UBDrawingController::StraightLineGeometry);draw->setLinePattern(pattern);
        const QPointF start=view->mapToScene(QPoint(180,180)),finish=view->mapToScene(QPoint(310,240));
        const auto before=groups(scene);scene->inputDevicePress(start);scene->inputDeviceMove(finish);scene->inputDeviceRelease(UBStylusTool::Line);
        line=created(scene,before);check(line&&line->hasEditableLine(),"drawn solid or dashed line records precise local endpoints");
        draw->setStylusTool(UBStylusTool::Selector);line->setSelected(true);delegate=dynamic_cast<UBGraphicsPixmapItemDelegate*>(line->Delegate());
        delegate->createControls();delegate->showControls();settle();auto* frame=delegate->frame();
        check(delegate->isLine()&&frame->objectName()==QStringLiteral("lineEndpointFrame"),"straight line uses the endpoint editor instead of the image frame");
        auto original=endpoints(line);const QPointF midpoint=(original.p1()+original.p2())/2;
        const QPointF normal(-(original.dy()),original.dx());const QPointF side=midpoint+normal/original.length()*55;
        check(!frame->shape().contains(side)&&frame->shape().contains(original.p1())&&frame->shape().contains(original.p2()),"hit area consists of the line and its two endpoints, not an image rectangle");
        const int history=stack->index();const auto transform=line->transform();const QPointF target=view->mapToScene(QPoint(380,180));
        check(QGuiApplication::screenAt(view->viewport()->mapToGlobal(view->mapFromScene(original.p2())))
            &&QGuiApplication::screenAt(view->viewport()->mapToGlobal(view->mapFromScene(target))),"window-routed test points lie inside the real synthetic-screen bounds");
        drag(view,original.p2(),target);
        auto changed=endpoints(line);
        check(QLineF(changed.p1(),original.p1()).length()<1&&QLineF(changed.p2(),target).length()<2,"real window-routed drag changes only the chosen end and keeps the opposite end fixed");
        check(stack->index()==history+1,"endpoint drag is one undo step regardless of mouse move count");
        const QPointF u=(original.p2()-original.p1())/original.length(),v(-u.y(),u.x());
        const QLineF transverse(transform.map(midpoint),transform.map(midpoint+v*4));
        const QLineF transformed(line->transform().map(midpoint),line->transform().map(midpoint+v*4));
        check(qAbs(transverse.length()-transformed.length())<.001,"changing line length and angle does not scale the transverse stroke width");
        board->undo();check(line->transform()==transform&&QLineF(endpoints(line).p2(),original.p2()).length()<.001,"undo restores exact endpoint geometry");board->redo();check(QLineF(endpoints(line).p2(),target).length()<2,"redo restores endpoint change");
        const QPointF fixed=endpoints(line).p2();const int secondHistory=stack->index();
        frameDrag(scene,frame,endpoints(line).p1(),fixed+QPointF(0,-170));
        check(QLineF(endpoints(line).p2(),fixed).length()<.001&&qAbs(endpoints(line).dx())<.001,"opposite endpoint can be dragged into a vertical line without moving the fixed end");
        check(stack->index()==secondHistory+1,"opposite-end editing is also a single undo step");
        const auto locked=line->transform();delegate->lock(true);frameDrag(scene,frame,endpoints(line).p1(),fixed+QPointF(90,60));
        check(line->transform()==locked,"locked line refuses endpoint editing");delegate->lock(false);
        const auto saved=line->transform();const int noOp=stack->index();frameDrag(scene,frame,endpoints(line).p1(),endpoints(line).p1());
        check(stack->index()==noOp&&line->transform()==saved,"clicking an endpoint without moving creates no undo noise");
        const auto recorded=content(scene,UBGraphicsScene::Podcast),exported=content(scene,UBGraphicsScene::NonScreen);
        line->setSelected(false);delegate->freeControls();
        check(content(scene,UBGraphicsScene::Podcast)==recorded&&content(scene,UBGraphicsScene::NonScreen)==exported,"endpoint controls never enter recording or exported content");
        if(pattern==int(UBDrawingController::SolidLinePattern)){
            line->setSelected(true);delegate->createControls();delegate->showControls();
            check(main->grab().save(out+"/line-vertical-1440.png"),"vertical endpoint editor captured");
            line->setSelected(false);delegate->freeControls();
        }
    }
    line->setSelected(true);delegate->createControls();delegate->showControls();settle();
    for(int percentage:{50,100,200}){
        board->zoom((percentage/100.)/board->currentZoom(),QPointF());
        check(qAbs(board->currentZoom()-percentage/100.)<.001,"actual public zoom operation sets the tested percentage");
        view->centerOn((endpoints(line).p1()+endpoints(line).p2())/2);settle();
        auto* frame=delegate->frame();const QLineF original=endpoints(line);const auto initial=line->transform();
        const QPointF target=original.p1()+QPointF(230,-90);const int history=stack->index();
        frameDrag(scene,frame,original.p2(),target);
        check(QLineF(endpoints(line).p1(),original.p1()).length()<.001&&QLineF(endpoints(line).p2(),target).length()<.001,"endpoint editing stays exact at 50/100/200 percent zoom with a panned view");
        check(frame->shape().contains(endpoints(line).p1())&&frame->shape().contains(endpoints(line).p2()),"endpoint hit handles follow the transformed segment at every zoom");
        check(stack->index()==history+1,"zoomed endpoint edit remains one undo step");
        board->undo();check(line->transform()==initial,"zoomed endpoint undo restores the exact original matrix");board->redo();board->undo();
    }
    board->zoomRestore();view->centerOn(0,0);settle();
    auto* frame=delegate->frame();QLineF original=endpoints(line);const auto initial=line->transform();
    frameDrag(scene,frame,original.p1(),original.p1());
    const int keyHistory=stack->index();QKeyEvent key(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);scene->sendEvent(frame,&key);
    check(QLineF(endpoints(line).p2(),original.p2()).length()<.001&&endpoints(line).p1().x()>original.x1(),"a selected endpoint can be nudged with the keyboard without moving the other end");
    check(stack->index()==keyHistory+1,"endpoint keyboard nudge is undoable");board->undo();
    const int cancelHistory=stack->index();
    for(auto type:{QEvent::GraphicsSceneMousePress,QEvent::GraphicsSceneMouseMove}){
        QGraphicsSceneMouseEvent event(type);event.setScenePos(type==QEvent::GraphicsSceneMousePress?original.p1():original.p1()+QPointF(65,45));event.setPos(frame->mapFromScene(event.scenePos()));event.setButton(Qt::LeftButton);event.setButtons(Qt::LeftButton);scene->sendEvent(frame,&event);
    }
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);scene->sendEvent(frame,&escape);
    check(line->transform()==initial&&stack->index()==cancelHistory,"Escape cancels an unfinished endpoint drag without changing history");
    frameDrag(scene,frame,original.p1(),original.p2());check(line->transform()==initial,"coincident endpoints are refused rather than making an invisible singular line");
    frameDrag(scene,frame,original.p2(),original.p1()+QPointF(190,55),Qt::ShiftModifier);
    check(qAbs(std::remainder(endpoints(line).angle(),15))<.001,"Shift endpoint drag snaps direction in 15-degree steps");board->undo();
    bool dialogChecked=false;const int dialogHistory=stack->index();
    QTimer::singleShot(0,qApp,[&]{if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){
        auto boxes=dialog->findChildren<QDoubleSpinBox*>();dialogChecked=boxes.size()==4;
        if(dialogChecked){check(dialog->grab().save(out+"/line-endpoint-dialog.png"),"native endpoint coordinate dialog captured");boxes[2]->setValue(boxes[2]->value()+20);dialog->accept();}else dialog->reject();
    }});
    for(auto* action:delegate->findChildren<QAction*>())if(action->text()==QStringLiteral("端点位置…")){action->trigger();break;}
    check(dialogChecked&&stack->index()==dialogHistory+1,"coordinate editor exposes four endpoint values and commits once");board->undo();
    line->setSelected(false);delegate->freeControls();
    // Validate conjugation through a non-uniformly transformed parent.
    auto* parent=new QGraphicsRectItem;auto* nested=dynamic_cast<UBGraphicsStrokesGroup*>(line->deepCopy());
    scene->addItem(parent);nested->setParentItem(parent);QTransform parentTransform;parentTransform.translate(50,-35);parentTransform.rotate(27);parentTransform.scale(1.3,.7);parent->setTransform(parentTransform);
    nested->setSelected(true);auto* nestedDelegate=static_cast<UBGraphicsPixmapItemDelegate*>(nested->Delegate());nestedDelegate->createControls();nestedDelegate->showControls();settle();
    check(nested->isSelected()&&nestedDelegate->frame()->isVisible(),"transformed-parent fixture exposes an independently selectable child, not group-wide selection");
    const QLineF nestedBefore=endpoints(nested);const auto nestedTransform=nested->transform();
    const QPointF nestedTarget=nestedBefore.p2()+QPointF(90,-40);
    frameDrag(scene,nestedDelegate->frame(),nestedBefore.p2(),nestedTarget);
    check(QLineF(endpoints(nested).p1(),nestedBefore.p1()).length()<.001&&QLineF(endpoints(nested).p2(),nestedTarget).length()<.001,"parent rotation and non-uniform scale do not break endpoint anchoring");
    board->undo();check(nested->transform()==nestedTransform,"nested endpoint undo restores the exact child transform");board->redo();
    nested->setSelected(false);nestedDelegate->freeControls();
    // Persistence and copy retain explicit endpoint mode after arbitrary editing.
    line->setPos(17,-9);const QLineF savedEnds=endpoints(line);const QUuid uuid=line->uuid();
    auto* clone=dynamic_cast<UBGraphicsStrokesGroup*>(line->deepCopy());
    check(clone&&clone->hasEditableLine()&&QLineF(endpoints(clone).p1(),savedEnds.p1()).length()<.001,"copy retains endpoint geometry and its editing mode");delete clone;
    UBSvgSubsetAdaptor::persistScene(board->selectedDocument(),scene,board->activeSceneIndex());
    auto restored=UBSvgSubsetAdaptor::loadScene(board->selectedDocument(),board->activeSceneIndex());UBGraphicsStrokesGroup* restoredLine=nullptr;
    for(auto* item:groups(restored))if(item->uuid()==uuid)restoredLine=item;
    check(restoredLine&&restoredLine->hasEditableLine()&&QLineF(endpoints(restoredLine).p1(),savedEnds.p1()).length()<.01&&QLineF(endpoints(restoredLine).p2(),savedEnds.p2()).length()<.01,"save/reopen preserves edited endpoints and vector geometry");
    auto* oldLine=new UBGraphicsStrokesGroup;auto* oldPolygon=new UBGraphicsPolygonItem(QLineF(QPointF(-100,120),QPointF(200,170)),3);
    oldPolygon->setStrokesGroup(oldLine);oldLine->addToGroup(oldPolygon);scene->addItem(oldLine);
    check(oldLine->hasEditableLine(),"legacy untagged nominal straight line also acquires endpoint editing");
    auto* curve=new UBGraphicsStrokesGroup;
    for(auto segment:{QLineF(-120,0,0,-80),QLineF(0,-80,150,20)}){auto* polygon=new UBGraphicsPolygonItem(segment,3);polygon->setStrokesGroup(curve);curve->addToGroup(polygon);}
    check(!curve->hasEditableLine(),"non-collinear handwriting is not mistaken for a straight line");delete curve;
    draw->setLineGeometryMode(UBDrawingController::CircleGeometry);draw->setLinePattern(UBDrawingController::SolidLinePattern);draw->setShapeFillEnabled(true);
    draw->setShapeFillColor(QColor("#93c5fd"));draw->setShapeFillOpacity(25);scene->inputDevicePress({-230,-40});scene->inputDeviceMove({-130,-40});scene->inputDeviceRelease(UBStylusTool::Line);
    draw->setStylusTool(UBStylusTool::Selector);oldLine->setSelected(true);auto* oldDelegate=static_cast<UBGraphicsPixmapItemDelegate*>(oldLine->Delegate());oldDelegate->createControls();oldDelegate->showControls();settle();
    check(main->grab().save(out+"/circle-line-1440.png"),"circle and two-endpoint selection captured in context");
    bool checked=false;QTimer::singleShot(0,qApp,[&]{auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());bool endpointsEntry=false,rotationEntry=false;
        if(menu)for(auto* entry:menu->actions()){if(entry->text().contains(QStringLiteral("端点")))endpointsEntry=true;if(entry->text().contains(QStringLiteral("旋转")))rotationEntry=true;}
        checked=menu&&endpointsEntry&&!rotationEntry;if(menu)menu->hide();});
    oldDelegate->openMenu(QPoint(200,200));check(checked,"line context menu offers endpoint coordinates instead of image size and rotation");
    QToolButton* geometryButton=nullptr;
    for(auto* button:main->findChildren<QToolButton*>("ubLineOptionMenuButton"))if(button->text()==QStringLiteral("形状"))geometryButton=button;
    check(geometryButton,"shape menu remains available");auto* menu=geometryButton->menu();
    bool circleChoice=false,ellipseChoice=false;for(auto* entry:menu->actions()){if(entry->text()==QStringLiteral("圆"))circleChoice=true;if(entry->text()==QStringLiteral("椭圆"))ellipseChoice=true;}
    check(circleChoice&&ellipseChoice,"circle and ellipse are separately named in the shape menu");
    const auto anchor=geometryButton->mapToGlobal(QPoint(0,geometryButton->height()));menu->popup(anchor);settle();
    QImage capture=main->grab().toImage();{QPainter painter(&capture);painter.translate(anchor-main->mapToGlobal(QPoint()));menu->render(&painter);}
    check(capture.save(out+"/circle-menu-1440.png"),"updated circle and ellipse menu captured");menu->hide();
    std::cout<<"PASS "<<checks<<" circle and line endpoint checks"<<std::endl;
}
static void ready()
{
    if(QThread::currentThread()->loopLevel()==0||!UBApplication::boardController||!UBApplication::boardController->activeScene()){QTimer::singleShot(100,qApp,ready);return;}
    try{tests();std::_Exit(0);}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;std::_Exit(1);}
}
int main(int argc,char** argv)
{
    if(qEnvironmentVariable("QT_QPA_PLATFORM")!="offscreen")return 2;
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-circle-line-tests",argc,argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");QApplication::setFont(QFont("Microsoft YaHei UI",9));
    const auto profile=qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");if(profile.isEmpty()||QDir::cleanPath(UBSettings::userDataDirectory())!=QDir::cleanPath(profile))return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500,qApp,ready);QTimer::singleShot(120000,qApp,[]{std::_Exit(4);});return app.exec("");
}
