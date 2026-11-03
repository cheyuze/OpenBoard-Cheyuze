#include <QtWidgets>
#include <QBackingStore>
#include <iostream>
#include <memory>
#include <stdexcept>

// Undo/redo command and controller bodies are extracted from production. Qt handles
// scene ownership, item groups, undo indexing, painting and backing-store pixels.
// OpenBoard's unrelated widget, layer/delegate metadata use inert adapters.
struct UBGraphicsScene;
struct UBBoardController : QObject {
    std::shared_ptr<UBGraphicsScene> mActiveScene;
    void freezeW3CWidget(QGraphicsItem*, bool) {}
    void undo(); void redo(); void refreshUndoRedoViews();
};
struct UBApplication { static UBBoardController* boardController; static QPointer<QUndoStack> undoStack; };
QPointer<QUndoStack> UBApplication::undoStack;
UBBoardController controller;
UBBoardController* UBApplication::boardController = &controller;
namespace UBGraphicsItemData { enum { itemLayerType=100, ItemLayerType=101 }; }
namespace itemLayerType { enum { BackgroundItem=100 }; }
namespace UBItemLayerType { enum { FixedBackground=101 }; }
struct DelegateAdapter { void update() {} };
struct UBGraphicsItem { static DelegateAdapter* Delegate(QGraphicsItem*) { static DelegateAdapter d; return &d; } };
struct UBGraphicsGroupContainerItem : QGraphicsItemGroup {
    enum { Type=QGraphicsItem::UserType+2 };
    int type() const override { return Type; }
    void destroy(bool) {}
};
struct UBGraphicsPolygonItem : QGraphicsPolygonItem {
    enum { Type=QGraphicsItem::UserType+1 };
    int type() const override { return Type; }
    QGraphicsItemGroup* strokesGroup() const { return nullptr; }
};
using UBBoardView=QGraphicsView;
struct UBGraphicsScene : QGraphicsScene {
    UBBoardView* control=nullptr;
    UBBoardView* controlView() const { return control; }
    void repaintStrokePreview(const QRectF& sceneRect);
    void setAsBackgroundObject(QGraphicsItem* i) { if (i) addItem(i); }
    void removeItemFromDeletion(QGraphicsItem*) {}
    QGraphicsItem* itemForUuid(QUuid) { return nullptr; }
    void updateSelectionFrame() {}
};
struct UBGraphicsItemUndoCommand : QUndoCommand {
    using GroupDataTable=QMultiMap<UBGraphicsGroupContainerItem*,QUuid>;
    using GroupDataTableIterator=QMultiMapIterator<UBGraphicsGroupContainerItem*,QUuid>;
    std::shared_ptr<UBGraphicsScene> mScene;
    QSet<QGraphicsItem*> mAddedItems, mRemovedItems;
    GroupDataTable mExcludedFromGroup;
    bool mFirstRedo=true;
    UBGraphicsItemUndoCommand(std::shared_ptr<UBGraphicsScene> s, QGraphicsItem* i):mScene(s),mAddedItems{i} {}
    void undo() override;
    void redo() override;
};
#include "production_undo.inc"
#include "production_stroke_repaint.inc"
#include "production_undo_controller.inc"

class ActionHarness : public UBBoardController {
public:
    struct MainWindow { QAction* actionUndo; QAction* actionRedo; };
    MainWindow* mMainWindow;
    void connectActions() {
#include "production_undo_connections.inc"
    }
};
class ObservedView : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
    int paints=0;
protected:
    void paintEvent(QPaintEvent* e) override {++paints;QGraphicsView::paintEvent(e);}
};

static void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
static void settle() {
    for(int i=0;i<8;++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents(QEventLoop::AllEvents,5);
    }
}
static QColor visiblePixel(QGraphicsView& view) {
    auto* device=view.window()->backingStore()->paintDevice();
    require(device->devType()==QInternal::Image,"offscreen image backing store required");
    const auto image=static_cast<QImage*>(device)->copy();
    const QPoint sample=view.viewport()->mapTo(view.window(),view.mapFromScene(QPointF(0,0)));
    return image.pixelColor(sample*image.devicePixelRatio());
}
static bool white(QColor c) { return c.red()>250&&c.green()>250&&c.blue()>250; }

static void probe(bool suppressImplicit, qreal zoom, bool secondView, bool button) {
    auto scene=std::make_shared<UBGraphicsScene>();
    scene->setSceneRect(-500,-400,1000,800);
    scene->setBackgroundBrush(Qt::white);
    ObservedView view(scene.get()), display(scene.get());
    scene->control=&view;
    for(auto* v:{&view,&display}) {
        v->setOptimizationFlags(QGraphicsView::IndirectPainting|QGraphicsView::DontSavePainterState);
        v->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
        v->setCacheMode(QGraphicsView::CacheBackground);
        v->setFrameShape(QFrame::NoFrame);
        v->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        v->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        v->resize(600,400);v->scale(zoom,zoom);v->show();
    }
    settle();
    // A stroke-like group, rather than a standalone polygon, exercises the
    // completed-handwriting branch of the unchanged production command.
    auto* group=new QGraphicsItemGroup;
    auto* segment=new QGraphicsRectItem(-120,-6,240,12);
    segment->setBrush(Qt::red);segment->setPen(Qt::NoPen);
    group->addToGroup(segment);scene->addItem(group);
    QUndoStack stack;
    UBApplication::undoStack=&stack;
    ActionHarness board;
    board.mActiveScene=scene;
    QAction undoAction,redoAction;
    ActionHarness::MainWindow main{&undoAction,&redoAction};
    board.mMainWindow=&main;board.connectActions();
    QToolButton undoButton,redoButton;
    undoButton.setDefaultAction(&undoAction);redoButton.setDefaultAction(&redoAction);
    stack.push(new UBGraphicsItemUndoCommand(scene,group));settle();
    require(!white(visiblePixel(view)),"initial pen group did not paint");
    if(secondView)require(!white(visiblePixel(display)),"initial display group did not paint");
    if(suppressImplicit)for(auto* v:{&view,&display})v->setViewportUpdateMode(QGraphicsView::NoViewportUpdate);
    if(button)undoButton.click();else undoAction.trigger();
    // Inspect backing-store pixels before any later event, input or render.
    require(stack.index()==0&&group->scene()==nullptr,"undo failed to remove scene content");
    const bool undoStale=!white(visiblePixel(view));
    require(!undoStale,"completed undo left stale ink on control view");
    if(secondView)require(white(visiblePixel(display)),"completed undo left stale ink on display view");
    settle();
    // Later pen strokes repaint only their own local damage; the completed
    // undo must remain visible without relying on those later paints.
    for(int i=0;i<3;++i) {
        auto* next=scene->addRect(-100,70+i*35,200,8,QPen(Qt::NoPen),QBrush(Qt::blue));
        scene->repaintStrokePreview(next->sceneBoundingRect());settle();
        require(white(visiblePixel(view)),"new writing revived old undone ink");
    }
    if(button)redoButton.click();else redoAction.trigger();
    require(stack.index()==1&&group->scene()==scene.get(),"redo failed to restore content");
    require(!white(visiblePixel(view)),"completed redo did not immediately repaint restored ink");
    if(secondView)require(!white(visiblePixel(display)),"redo did not repaint display view");
    view.paints=0;display.paints=0;
    auto* extra=scene->addRect(-80,-100,160,10,QPen(Qt::NoPen),QBrush(Qt::green));
    stack.push(new UBGraphicsItemUndoCommand(scene,extra));
    require(view.paints==0&&display.paints==0,"pushing a new stroke forced full-view history repaint");
    for(int round=0;round<12;++round) {
        undoAction.trigger();undoAction.trigger();
        require(stack.index()==0&&group->scene()==nullptr&&extra->scene()==nullptr,
                "rapid undo skipped or duplicated a history step");
        require(white(visiblePixel(view))&&white(visiblePixel(display)),"rapid undo pixels stale");
        redoAction.trigger();redoAction.trigger();
        require(stack.index()==2&&group->scene()==scene.get()&&extra->scene()==scene.get(),
                "rapid redo history mismatch");
        require(!white(visiblePixel(view))&&!white(visiblePixel(display)),"rapid redo pixels stale");
    }
    view.paints=0;display.paints=0;
    redoAction.trigger();
    require(view.paints==0&&display.paints==0,"unavailable redo forced repaint");
    std::cout<<"PASS implicit="<<(suppressImplicit?"suppressed":"normal")<<" zoom="<<zoom
             <<" secondView="<<secondView<<" undo_state_correct=1 undo_stale="<<undoStale
             <<" button="<<button<<" immediate_pixels_correct=1 three_distant_strokes_checked=1 rapid_cycles=12"<<std::endl;
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        for(qreal zoom:{0.5,1.0,1.25,2.0})for(bool suppressed:{false,true})for(bool button:{false,true})probe(suppressed,zoom,true,button);
        UBApplication::undoStack=nullptr;
        ActionHarness empty;
        QAction undoAction,redoAction;
        ActionHarness::MainWindow main{&undoAction,&redoAction};
        empty.mMainWindow=&main;empty.connectActions();
        undoAction.trigger();redoAction.trigger();
        QUndoStack emptyStack;UBApplication::undoStack=&emptyStack;
        undoAction.trigger();redoAction.trigger();
        std::cout<<"PASS missing stack/scene and empty history are safe"<<std::endl;
        std::cout<<"17/17 scenarios passed. Suppressed damage is fault injection, not an exact hardware reproduction."<<std::endl;
        return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<std::endl;return 1;}
}
