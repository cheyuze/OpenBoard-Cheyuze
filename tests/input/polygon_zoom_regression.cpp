#include <QtWidgets>
#include <memory>
#include <utility>
#include <iostream>
#include <stdexcept>

// Real Qt scenes/widgets and unchanged production method bodies; document,
// delegate and undo adapters keep these tests isolated from user documents.
class UBGraphicsScene;
class UBGraphicsItemDelegate {
public:
    explicit UBGraphicsItemDelegate(QGraphicsItem* item) : mItem(item) {}
    void remove(bool) { if (mItem->scene()) mItem->scene()->removeItem(mItem); }
    void update() {}
    QGraphicsItem* mItem;
};
class DelegatedItem : public QGraphicsRectItem {
public:
    DelegatedItem() : QGraphicsRectItem(0, 0, 10, 10), delegate(this) {}
    UBGraphicsItemDelegate delegate;
};
class UBGraphicsStrokesGroup : public QGraphicsItemGroup {
public:
    enum { Type = QGraphicsItem::UserType + 71 };
    UBGraphicsStrokesGroup() : delegate(this) {}
    int type() const override { return Type; }
    UBGraphicsItemDelegate delegate;
};
class UBGraphicsGroupContainerItem : public QGraphicsItemGroup {
public:
    enum { Type = QGraphicsItem::UserType + 72 };
    UBGraphicsGroupContainerItem() : delegate(this) {}
    int type() const override { return Type; }
    void destroy(bool) {}
    UBGraphicsItemDelegate* Delegate() { return &delegate; }
    UBGraphicsItemDelegate delegate;
};
class UBGraphicsPolygonItem : public QGraphicsPolygonItem {
public:
    UBGraphicsPolygonItem() : QGraphicsPolygonItem(QPolygonF{QPointF(0,0), QPointF(10,0), QPointF(10,10)}) {}
    void setStrokesGroup(UBGraphicsStrokesGroup*) {}
};
class UBGraphicsPDFItem : public QGraphicsRectItem {
public:
    enum { Type = QGraphicsItem::UserType + 73 };
    int type() const override { return Type; }
};
struct UBGraphicsItemData { enum { ItemLayerType }; };
struct UBItemLayerType { enum { FixedBackground = -2000, Tool = 1000 }; };
class UBGraphicsItem {
public:
    static UBGraphicsItemDelegate* Delegate(QGraphicsItem* item) {
        if (auto delegated = dynamic_cast<DelegatedItem*>(item)) return &delegated->delegate;
        if (auto strokes = dynamic_cast<UBGraphicsStrokesGroup*>(item)) return &strokes->delegate;
        if (auto group = dynamic_cast<UBGraphicsGroupContainerItem*>(item)) return group->Delegate();
        return nullptr;
    }
    static QUuid getOwnUuid(QGraphicsItem*) { return {}; }
};
class UBCoreGraphicsScene : public QGraphicsScene {
public:
    static void removeItemFromDeletion(QGraphicsItem*) {}
};
class UBGraphicsScene : public UBCoreGraphicsScene,
        public std::enable_shared_from_this<UBGraphicsScene> {
public:
    enum clearCase { clearItemsAndAnnotations, clearAnnotations, clearItems, clearBackground };
    void clearContent(clearCase pCase);
    bool polygonDrawingActive() const;
    bool isPolygonPreviewItem(const QGraphicsItem* item) const;
    void commitPolygonDrawing();
    QGraphicsItem* rootItem(QGraphicsItem* item) const;
    QList<QGraphicsItem*> savedPolygonItems() const;
    QList<QGraphicsItem*> copyEligiblePolygons() const;
    void drawItems(QPainter* painter, int numItems, QGraphicsItem* items[],
            const QStyleOptionGraphicsItem options[], QWidget* widget) override;
    void cancelPolygonDrawing();
    void clearPolygonPreview();
    void clearShapeFillPreview();
    void updatePolygonPreview(const QPointF& scenePos);
    void rebuildPolygonPreview(const QPointF&, bool) { ++rebuilds; }
    bool isBackgroundObject(QGraphicsItem* item) { return item == mBackgroundObject; }
    void setDocumentUpdated() { ++updated; }
    void seedPreview(bool withVertices = true) {
        if (withVertices) mPolygonVertices = {QPointF(0,0), QPointF(10,0)};
        mPolygonHoverPoint = QPointF(10,10);
        auto outline = new UBGraphicsPolygonItem;
        outline->setBrush(Qt::black);
        addItem(outline);
        mPolygonPreviewItems << outline;
        mShapeFillPreview = new UBGraphicsPolygonItem;
        mShapeFillPreview->setBrush(Qt::black);
        addItem(mShapeFillPreview);
    }
    QVector<QPointF> mPolygonVertices;
    QPointF mPolygonHoverPoint;
    QList<UBGraphicsPolygonItem*> mPolygonPreviewItems;
    UBGraphicsPolygonItem* mShapeFillPreview = nullptr;
    QGraphicsItem* mBackgroundObject = nullptr;
    bool mUndoRedoStackEnabled = true;
    int updated = 0;
    int rebuilds = 0;
    enum RenderingContext { Screen, NonScreen, PdfExport, Podcast };
    RenderingContext mRenderingContext = Screen;
    QList<QGraphicsItem*> mTools;
    QSet<QGraphicsItem*> mAddedItems;
    QSet<QGraphicsItem*> mRemovedItems;
};
class UBGraphicsItemUndoCommand : public QUndoCommand {
public:
    using GroupDataTable = QMultiMap<UBGraphicsGroupContainerItem*, QUuid>;
    UBGraphicsItemUndoCommand(std::shared_ptr<UBGraphicsScene> scene,
            const QSet<QGraphicsItem*>& removed, const QSet<QGraphicsItem*>&,
            const GroupDataTable&) : mScene(scene), removedItems(removed) {}
    UBGraphicsItemUndoCommand(std::shared_ptr<UBGraphicsScene> scene,
            const QSet<QGraphicsItem*>& removed, const QSet<QGraphicsItem*>& added)
        : mScene(scene), removedItems(removed), addedItems(added) {}
    void undo() override {
        for (auto item : addedItems) mScene->removeItem(item);
        for (auto item : removedItems) mScene->addItem(item);
    }
    void redo() override {
        if (first) { first = false; return; }
        for (auto item : removedItems) mScene->removeItem(item);
        for (auto item : addedItems) mScene->addItem(item);
    }
    std::shared_ptr<UBGraphicsScene> mScene;
    QSet<QGraphicsItem*> removedItems;
    QSet<QGraphicsItem*> addedItems;
    bool first = true;
};
class UBApplication {
public:
    static inline QUndoStack* undoStack = nullptr;
};
constexpr int UB_MAX_ZOOM = 5;
class UBBoardController {
public:
    UBBoardController() {
        mZoomSlider = &slider;
        slider.setRange(10,500);
        mZoomLabel = &label;
        mZoomOutButton = &out;
        mZoomInButton = &in;
        mControlView = &view;
        QObject::connect(&slider, &QSlider::valueChanged, &slider,
                [this](int value) { setZoomPercentage(value); });
        updateZoomControl(actualZoom);
    }
    void updateZoomControl(qreal zoomFactor);
    void setZoomPercentage(int percentage);
    void presetSelected(QAction* action);
    void doubleClickReset();
    qreal currentZoom() const { return actualZoom; }
    void zoom(qreal multiplier, const QPointF&) {
        actualZoom *= multiplier;
        ++zoomCalls;
        updateZoomControl(actualZoom);
    }
    QSlider slider;
    QLabel label;
    QToolButton out, in;
    QGraphicsView view;
    QSlider* mZoomSlider;
    QLabel* mZoomLabel;
    QToolButton* mZoomOutButton;
    QToolButton* mZoomInButton;
    QGraphicsView* mControlView;
    qreal actualZoom = 1.0;
    int zoomCalls = 0;
};

#include "production_polygon_handlers.inc"

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void testClear(UBGraphicsScene::clearCase mode, bool expectCancel) {
    auto scene = std::make_shared<UBGraphicsScene>();
    QUndoStack undo;
    UBApplication::undoStack = &undo;
    auto stroke = std::make_unique<UBGraphicsStrokesGroup>();
    auto object = std::make_unique<DelegatedItem>();
    auto background = std::make_unique<DelegatedItem>();
    scene->addItem(stroke.get());
    scene->addItem(object.get());
    scene->addItem(background.get());
    scene->mBackgroundObject = background.get();
    scene->seedPreview();
    scene->clearContent(mode);
    scene->updatePolygonPreview(QPointF(100,100));
    require(scene->polygonDrawingActive() != expectCancel, "polygon vertices retained after clear");
    require(scene->mPolygonPreviewItems.isEmpty() == expectCancel, "outline preview clear scope mismatch");
    require((scene->mShapeFillPreview == nullptr) == expectCancel, "fill preview clear scope mismatch");
    require(scene->rebuilds == (expectCancel ? 0 : 1), "pointer movement resurrected cleared polygon");
    require(undo.count() == 1, "preview cancellation changed undo command count");
    const auto command = dynamic_cast<const UBGraphicsItemUndoCommand*>(undo.command(0));
    const bool clearStroke = mode == UBGraphicsScene::clearAnnotations || mode == UBGraphicsScene::clearItemsAndAnnotations;
    const bool clearObject = mode == UBGraphicsScene::clearItems || mode == UBGraphicsScene::clearItemsAndAnnotations;
    const bool clearBackground = mode == UBGraphicsScene::clearBackground;
    require(command->removedItems.contains(stroke.get()) == clearStroke, "committed stroke clear behavior changed");
    require(command->removedItems.contains(object.get()) == clearObject, "object clear behavior changed");
    require(command->removedItems.contains(background.get()) == clearBackground, "background clear behavior changed");
    require(command->removedItems.size() == int(clearStroke) + int(clearObject) + int(clearBackground), "preview leaked into undo command");
    undo.undo();
    require(stroke->scene() == scene.get() && object->scene() == scene.get() && background->scene() == scene.get(), "committed objects not supplied intact to undo");
    if (expectCancel) require(!scene->polygonDrawingActive(), "undo revived unfinished polygon");
    // Explicitly detach owned fixtures before scene teardown.
    scene->removeItem(stroke.get());
    scene->removeItem(object.get());
    scene->removeItem(background.get());
    undo.clear();
}
static void testZoom(bool preset, qreal initial, int target) {
    UBBoardController board;
    board.actualZoom = initial;
    board.updateZoomControl(initial);
    if (preset) { QAction action; action.setData(target); board.presetSelected(&action); }
    else board.doubleClickReset();
    require(qAbs(board.actualZoom - target / 100.0) < 1e-12, "zoom handler retained rounded value instead of exact target");
    require(board.slider.value() == target, "zoom slider not synchronized");
}
static QColor renderPixel(UBGraphicsScene& scene, UBGraphicsScene::RenderingContext context) {
    scene.mRenderingContext = context;
    QImage image(100,100,QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene.render(&painter,QRectF(0,0,100,100),QRectF(0,0,100,100));
    painter.end();
    return image.pixelColor(5,2);
}
static void testPreviewPersistence() {
    auto scene = std::make_shared<UBGraphicsScene>();
    QUndoStack undo;
    UBApplication::undoStack = &undo;
    scene->seedPreview();
    auto outline = scene->mPolygonPreviewItems.first();
    auto fill = scene->mShapeFillPreview;
    require(renderPixel(*scene,UBGraphicsScene::Screen) == QColor(Qt::black), "screen should retain in-progress feedback");
    require(renderPixel(*scene,UBGraphicsScene::NonScreen) == QColor(Qt::white), "thumbnail included unfinished preview");
    require(renderPixel(*scene,UBGraphicsScene::PdfExport) == QColor(Qt::white), "PDF included unfinished preview");
    require(scene->savedPolygonItems().isEmpty(), "SVG item selection included unfinished preview");
    require(scene->polygonDrawingActive() && outline->isVisible() && fill->isVisible(), "saving destroyed or hid user's in-progress polygon");
    require(undo.count() == 0, "saving introduced undo command");
    scene->commitPolygonDrawing();
    require(!scene->polygonDrawingActive(), "commit did not end preview state");
    require(scene->savedPolygonItems().size() == 2, "committed polygon incorrectly excluded from SVG");
    require(renderPixel(*scene,UBGraphicsScene::NonScreen) == QColor(Qt::black), "committed polygon missing from thumbnail");
    require(renderPixel(*scene,UBGraphicsScene::PdfExport) == QColor(Qt::black), "committed polygon missing from PDF");
    require(undo.count() == 1, "commit should create exactly one undo command");
    undo.undo();
    require(scene->savedPolygonItems().isEmpty(), "undone polygon still saved");
    require(renderPixel(*scene,UBGraphicsScene::NonScreen) == QColor(Qt::white), "undone polygon still in thumbnail");
    undo.redo();
    require(scene->savedPolygonItems().size() == 2, "redone polygon excluded from save");
    require(renderPixel(*scene,UBGraphicsScene::NonScreen) == QColor(Qt::black), "redone polygon excluded from thumbnail");
    undo.clear();
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    int failed = 0;
    const auto run = [&](const char* name, auto operation) {
        try { operation(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    run("clear-all-cancels-unfinished-polygon", [] { testClear(UBGraphicsScene::clearItemsAndAnnotations, true); });
    run("clear-annotations-cancels-unfinished-polygon", [] { testClear(UBGraphicsScene::clearAnnotations, true); });
    run("clear-objects-preserves-unfinished-polygon", [] { testClear(UBGraphicsScene::clearItems, false); });
    run("clear-background-preserves-unfinished-polygon", [] { testClear(UBGraphicsScene::clearBackground, false); });
    run("double-click-exact-reset-from-100.05-percent", [] { testZoom(false,1.0005,100); });
    run("preset-exact-reset-from-29.99-percent", [] { testZoom(true,0.2999,30); });
    run("double-click-reset-from-150-percent", [] { testZoom(false,1.5,100); });
    run("preset-changed-integer", [] { testZoom(true,1.0,200); });
    run("already-exact-no-redundant-zoom", [] {
        UBBoardController board;
        board.doubleClickReset();
        require(board.actualZoom == 1.0 && board.zoomCalls == 0, "exact zoom should not redraw");
    });
    run("preview-save-render-commit-undo-redo", testPreviewPersistence);
    run("SVG-item-selection-excludes-only-preview", [] {
        auto scene = std::make_shared<UBGraphicsScene>();
        auto permanent = new UBGraphicsPolygonItem;
        scene->addItem(permanent);
        scene->seedPreview();
        const auto saved = scene->savedPolygonItems();
        require(saved.size() == 1 && saved.first() == permanent, "SVG did not exclude exactly the unfinished preview");
        require(scene->polygonDrawingActive(), "SVG selection canceled user's drawing");
    });
    run("copy-page-selection-excludes-only-preview", [] {
        auto scene = std::make_shared<UBGraphicsScene>();
        auto permanent = new UBGraphicsPolygonItem;
        scene->addItem(permanent);
        scene->seedPreview();
        const auto copied = scene->copyEligiblePolygons();
        require(copied.size() == 1 && copied.first() == permanent, "page copying included unfinished preview");
        require(scene->polygonDrawingActive(), "copy selection canceled user's drawing");
    });
    run("non-polygon-fill-not-filtered", [] {
        auto scene = std::make_shared<UBGraphicsScene>();
        scene->seedPreview(false);
        require(scene->savedPolygonItems().size() == 2, "filter affected non-polygon geometry fill");
        require(renderPixel(*scene,UBGraphicsScene::NonScreen) == QColor(Qt::black), "filter affected non-polygon rendering");
    });
    std::cout << "TOTAL " << 13 - failed << "/13 passed\n";
    return failed ? 1 : 0;
}
