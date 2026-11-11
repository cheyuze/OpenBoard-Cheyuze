// Full-client integration test. Run only with the offscreen Qt platform and
// the generated, explicitly isolated paste-position-profile configuration.
#include <QtWidgets>
#include <QPointingDevice>
#include <iostream>
#include <stdexcept>
#include "core/UBApplication.h"
#include "core/UBApplicationController.h"
#include "core/UBSettings.h"
#include "core/UBPersistenceManager.h"
#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "board/UBBoardPaletteManager.h"
#include "board/UBDrawingController.h"
#include "document/UBDocument.h"
#include "document/UBDocumentProxy.h"
#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsPixmapItem.h"
#include "domain/UBGraphicsSvgItem.h"
#include "domain/UBGraphicsStrokesGroup.h"
#include "domain/UBImageTransform.h"
#include "gui/UBMainWindow.h"
#include "gui/UBStartupHintsPalette.h"

static int checks = 0;
static void check(bool condition, const char* message)
{
    ++checks;
    std::cout << (condition ? "PASS " : "FAIL ") << message << std::endl;
    if (!condition) throw std::runtime_error(message);
}
static bool near(QPointF a, QPointF b) { return QLineF(a, b).length() < .02; }
static void settle()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}
static QList<UBGraphicsPixmapItem*> pictures(const std::shared_ptr<UBGraphicsScene>& scene)
{
    QList<UBGraphicsPixmapItem*> result;
    for (auto* item : scene->items())
        if (auto* picture = dynamic_cast<UBGraphicsPixmapItem*>(item))
            if (!picture->parentItem() && picture != scene->backgroundObject()) result << picture;
    return result;
}
static void movePointer(QWidget* target, QPointF position)
{
    QMouseEvent event(QEvent::MouseMove, position, target->mapToGlobal(position.toPoint()),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}
static UBGraphicsPixmapItem* pasteImage(UBBoardController* board, const QImage& image)
{
    const auto before = pictures(board->activeScene());
    QApplication::clipboard()->setImage(image); // Offscreen clipboard is process-local.
    UBApplication::mainWindow->actionPaste->trigger();
    for (auto* item : pictures(board->activeScene())) if (!before.contains(item)) return item;
    throw std::runtime_error("paste did not create an image");
}
static void runTests()
{
    auto* board = UBApplication::boardController;
    auto* main = UBApplication::mainWindow;
    UBApplication::applicationController->showBoard();
    board->paletteManager()->tipsPalette()->hide();
    main->resize(1440, 900);
    auto document = UBPersistenceManager::persistenceManager()->createDocument(
        "Paste-position-test", QUuid::createUuid().toString(), false, QString(), 0, false);
    UBDocument::getDocument(document)->createPage(0);
    board->setActiveDocumentScene(document, 0, true, true);
    auto scene = board->activeScene();
    auto* view = board->controlView();
    auto* stack = UBApplication::undoStack.data();
    UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Pen);
    settle();
    check(view->isVisible(), "real control view is visible in the isolated client");

    // A never-entered view falls back to its visible center, not scene origin.
    {
        UBBoardView fresh(board, nullptr, true);
        fresh.resize(640, 400);
        fresh.setScene(scene.get());
        fresh.centerOn(2400, -1300);
        QCursor::setPos(fresh.viewport()->mapToGlobal(QPoint(-300, -300)));
        check(near(fresh.pastePosition(), fresh.mapToScene(fresh.viewport()->rect().center())),
              "no pointer history falls back to the visible viewport center");
    }
    QImage image(160, 90, QImage::Format_RGB32);
    image.fill(QColor("#60a5fa"));
    for (int mode = 0; mode < 3; ++mode) {
        if (mode == 0) board->setBorderlessPageSize(true);
        if (mode == 1) board->setWidePageSize(true);
        if (mode == 2) board->setRegularPageSize(true);
        for (qreal zoom : {.5, 1., 1.25, 2.}) {
            view->setTransform(QTransform::fromScale(zoom, zoom));
            view->centerOn(3200, -1900);
            settle();
            const QPointF pointer(430.25, 270.75);
            movePointer(view->viewport(), pointer);
            const QPointF expected = view->viewportTransform().inverted().map(pointer);
            check(near(view->pastePosition(), expected), "pointer maps through the actual pan/zoom transform");
            const int before = pictures(scene).size();
            auto* pasted = pasteImage(board, image);
            check(near(pasted->sceneBoundingRect().center(), expected), "image center lands exactly at the canvas pointer in every page mode");
            board->undo();
            check(pictures(scene).size() == before, "undo removes the pasted image");
            board->redo();
            check(pictures(scene).size() == before + 1 && near(pasted->sceneBoundingRect().center(), expected),
                  "redo restores the same pointer position, without random offset");
        }
    }

    // Enter restores a useful paste focus without a click, and menu events
    // must not replace the canvas anchor with toolbar coordinates.
    QPointF pointer(510.5, 230.5);
    QEnterEvent enter(pointer, pointer, view->viewport()->mapToGlobal(pointer.toPoint()));
    QApplication::sendEvent(view->viewport(), &enter);
    QPointF expected = view->viewportTransform().inverted().map(pointer);
    QToolButton control(main);
    movePointer(&control, {10, 10});
    auto* viaMenu = pasteImage(board, image);
    check(near(viaMenu->sceneBoundingRect().center(), expected), "menu paste retains the last canvas point even without a canvas click");

    auto shortcutBefore = pictures(scene);
    QApplication::clipboard()->setImage(image);
    main->activateWindow(); view->setFocus(); settle();
    QKeyEvent pasteKey(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(view->viewport(), &pasteKey);
    QKeyEvent pasteRelease(QEvent::KeyRelease, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(view->viewport(), &pasteRelease);
    UBGraphicsPixmapItem* shortcutImage = nullptr;
    for (auto* item : pictures(scene)) if (!shortcutBefore.contains(item)) shortcutImage = item;
    check(shortcutImage && near(shortcutImage->sceneBoundingRect().center(), expected), "actual Ctrl+V dispatch uses the same pointer anchor as menu paste");

    // Pen hover is recorded even when it does not move QCursor (synthetic or
    // tablet-specific drivers); no automatic tool switching is introduced.
    const QPointingDevice pen("test pen", 724,
        QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
    pointer = {690.75, 310.25};
    QTabletEvent hover(QEvent::TabletMove, &pen, pointer,
        view->viewport()->mapToGlobal(pointer.toPoint()), 0, 0, 0, 0, 0, 0,
        Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    QApplication::sendEvent(view->viewport(), &hover);
    expected = view->viewportTransform().inverted().map(pointer);
    check(near(pasteImage(board, image)->sceneBoundingRect().center(), expected), "tablet hover supplies an accurate subpixel paste focus");
    check(UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Pen,
          "pasting does not change the selected pen tool");

    // Remembered viewport focus is mapped at paste time, not cached in an old
    // scene coordinate system. Also keep it visible after a resize.
    view->centerOn(-4300, 2600);
    expected = view->viewportTransform().inverted().map(pointer);
    check(near(view->pastePosition(), expected), "menu focus follows the current view after a later pan");
    {
        UBBoardView resized(board, nullptr, true);
        resized.resize(800, 600);
        resized.setScene(scene.get());
        movePointer(resized.viewport(), {700, 500});
        resized.resize(250, 180);
        const QPoint mapped = resized.mapFromScene(resized.pastePosition());
        check(resized.viewport()->rect().contains(mapped), "resizing cannot leave a remembered paste anchor off canvas");
    }

    // Clipboard copied inside OpenBoard used to bypass pPos entirely.
    scene->deselectAllItems();
    viaMenu->setSelected(true);
    viaMenu->setTransform(UBImageTransform::rotated(viaMenu->boundingRect(), viaMenu->transform(), 27));
    board->copy();
    pointer = {450, 250};
    movePointer(view->viewport(), pointer);
    expected = view->viewportTransform().inverted().map(pointer);
    auto before = pictures(scene);
    board->paste();
    UBGraphicsPixmapItem* internal = nullptr;
    for (auto* item : pictures(scene)) if (!before.contains(item)) internal = item;
    check(internal && near(internal->sceneBoundingRect().center(), expected), "internally copied rotated image also pastes at the pointer");
    board->undo(); board->redo();
    check(internal && near(internal->sceneBoundingRect().center(), expected), "internal image undo/redo preserves its new position");

    scene->deselectAllItems();
    viaMenu->setSelected(true); internal->setSelected(true);
    const QPointF separation = internal->sceneBoundingRect().center() - viaMenu->sceneBoundingRect().center();
    board->copy();
    pointer = {760, 350};
    movePointer(view->viewport(), pointer);
    expected = view->viewportTransform().inverted().map(pointer);
    before = pictures(scene);
    board->paste();
    QList<QGraphicsItem*> copies;
    QRectF copiedBounds;
    for (auto* item : pictures(scene)) if (!before.contains(item)) { copies << item; copiedBounds |= item->sceneBoundingRect(); }
    std::cout << "multi copies=" << copies.size() << " expected=" << expected.x() << "," << expected.y()
              << " actual=" << copiedBounds.center().x() << "," << copiedBounds.center().y() << std::endl;
    check(copies.size() == 2 && near(copiedBounds.center(), expected), "multiple copied images are centered together at the pointer");
    const QPointF actualSeparation = copies[0]->sceneBoundingRect().center() - copies[1]->sceneBoundingRect().center();
    check(near(actualSeparation, separation) || near(actualSeparation, -separation), "multiple-image paste preserves relative placement");
    auto* duplicate = dynamic_cast<QGraphicsItem*>(board->duplicateItem(viaMenu));
    check(duplicate && !near(duplicate->sceneBoundingRect().center(), expected), "separate Duplicate command keeps its existing offset behavior");

    // File/URL paste previously re-centered large scaled images at scene 0,0.
    QImage large(3200, 1800, QImage::Format_RGB32);
    large.fill(Qt::yellow);
    const QString path = document->persistencePath() + "/large-source.png";
    check(large.save(path), "synthetic large image saved only in the test profile");
    auto* fileData = new QMimeData;
    fileData->setUrls({QUrl::fromLocalFile(path)});
    QApplication::clipboard()->setMimeData(fileData);
    before = pictures(scene);
    board->paste();
    QElapsedTimer wait; wait.start();
    while (pictures(scene).size() == before.size() && wait.elapsed() < 5000) settle();
    UBGraphicsPixmapItem* filePicture = nullptr;
    for (auto* item : pictures(scene)) if (!before.contains(item)) filePicture = item;
    check(filePicture && near(filePicture->sceneBoundingRect().center(), expected), "large file image remains at the paste anchor after fitting to the page");
    check(filePicture->sceneBoundingRect().width() < large.width(), "large file image still uses the existing size limit");

    QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" height=\"60\"><rect width=\"120\" height=\"60\" fill=\"blue\"/></svg>";
    auto* svgData = new QMimeData;
    svgData->setData("image/svg+xml", svg);
    svgData->setImageData(image);
    QApplication::clipboard()->setMimeData(svgData);
    board->paste();
    UBGraphicsSvgItem* vector = nullptr;
    for (auto* item : scene->items()) if (auto* candidate = dynamic_cast<UBGraphicsSvgItem*>(item)) vector = candidate;
    check(vector && near(vector->sceneBoundingRect().center(), expected), "SVG clipboard image is centered correctly at system scale");

    // Layering is unchanged: adding/selecting a picture must not hide ink.
    UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Pen);
    scene->inputDevicePress(expected - QPointF(20, 0));
    scene->inputDeviceMove(expected + QPointF(20, 0));
    scene->inputDeviceRelease(UBStylusTool::Pen);
    auto* underInk = pasteImage(board, image);
    QGraphicsItem* ink = nullptr;
    for (auto* item : scene->items()) if (item->type() == UBGraphicsStrokesGroup::Type) ink = item;
    check(ink && underInk->zValue() < ink->zValue(), "pointer placement retains the image-under-ink default");
    const qreal layer = underInk->zValue();
    underInk->setSelected(true);
    check(underInk->zValue() == layer, "selecting a pasted image does not change its layer");

    // Explicit drop positions do not consult the paste anchor.
    QMimeData drop; drop.setImageData(image);
    const QPointF dropPoint(-2500, 1600);
    before = pictures(scene);
    board->processMimeData(&drop, dropPoint);
    UBGraphicsPixmapItem* dropped = nullptr;
    for (auto* item : pictures(scene)) if (!before.contains(item)) dropped = item;
    check(dropped && near(dropped->sceneBoundingRect().center(), dropPoint), "drag/drop retains its explicit drop position");
    check(stack->canUndo(), "image operations retain undo history");
    std::cout << "PASS " << checks << " full-client paste-position checks" << std::endl;
}
static void runWhenReady()
{
    if (QThread::currentThread()->loopLevel() == 0
            || !UBApplication::boardController || !UBApplication::boardController->activeScene()) {
        QTimer::singleShot(100, qApp, runWhenReady); return;
    }
    try { runTests(); std::_Exit(0); }
    catch (const std::exception& error) { std::cerr << error.what() << std::endl; std::_Exit(1); }
}
int main(int argc, char** argv)
{
    if (qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen") return 2;
    Q_INIT_RESOURCE(OpenBoard);
    UBApplication app("OpenBoard-paste-position-tests", argc, argv);
    // The offscreen platform has no native font database. Load process-local
    // fonts before layout, as in the other full-client integration fixtures.
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    const QString expectedProfile = qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if (expectedProfile.isEmpty() || QDir::cleanPath(UBSettings::userDataDirectory()) != QDir::cleanPath(expectedProfile)) {
        std::cerr << "Refusing to run outside the exact isolated test profile" << std::endl; return 2;
    }
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);
    UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(300, qApp, runWhenReady);
    QTimer::singleShot(60000, qApp, [] { std::_Exit(4); });
    return app.exec("");
}
