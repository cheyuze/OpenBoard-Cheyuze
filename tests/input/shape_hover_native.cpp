// Isolated full-client selector hover and thin-shape picking regression.
#define main oldPasteMain
#define runTests oldPasteTests
#define runWhenReady oldPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "domain/UBGraphicsPixmapItemDelegate.h"
#include "adaptors/UBSvgSubsetAdaptor.h"
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static void mouse(QWidget* target, QEvent::Type type, QPointF point,
                  Qt::MouseButtons buttons = Qt::NoButton,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    auto* window = UBApplication::mainWindow->windowHandle();
    const QPointF global = target->mapToGlobal(point.toPoint());
    QMouseEvent event(type, window->mapFromGlobal(global), global,
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, modifiers);
    QApplication::sendEvent(window, &event);
    settle();
}
static void hover(UBBoardView* view, QPointF point) { mouse(view->viewport(), QEvent::MouseMove, point); }
static void click(UBBoardView* view, QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    mouse(view->viewport(), QEvent::MouseButtonPress, point, Qt::LeftButton, modifiers);
    mouse(view->viewport(), QEvent::MouseButtonRelease, point, Qt::NoButton, modifiers);
}
static UBGraphicsStrokesGroup* owner(QGraphicsItem* item)
{
    for (; item; item = item->parentItem())
        if (auto* group = dynamic_cast<UBGraphicsStrokesGroup*>(item)) return group;
    return nullptr;
}
static UBGraphicsStrokesGroup* drawShape(int geometry, QPointF start, QPointF end)
{
    auto scene = UBApplication::boardController->activeScene();
    QSet<UBGraphicsStrokesGroup*> old;
    for (auto* item : scene->items()) if (auto* group = dynamic_cast<UBGraphicsStrokesGroup*>(item)) old.insert(group);
    auto* draw = UBDrawingController::drawingController();
    draw->setLineGeometryMode(geometry);
    scene->inputDevicePress(start);
    scene->inputDeviceMove(end);
    scene->inputDeviceRelease(UBStylusTool::Line);
    draw->setStylusTool(UBStylusTool::Selector);
    settle();
    for (auto* item : scene->items()) if (auto* group = dynamic_cast<UBGraphicsStrokesGroup*>(item))
        if (!old.contains(group)) return group;
    throw std::runtime_error("shape creation failed");
}
static QImage renderContent(const std::shared_ptr<UBGraphicsScene>& scene, UBGraphicsScene::RenderingContext context)
{
    QImage image(800, 500, QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
    QPainter painter(&image);const auto previous = scene->renderingContext();scene->setRenderingContext(context);
    scene->render(&painter, QRectF(0, 0, 800, 500), QRectF(-400, -250, 800, 500));
    scene->setRenderingContext(previous);return image;
}
static void capture(UBBoardView* view, QPointF point, const QString& file)
{
    auto* main = UBApplication::mainWindow;
    QImage image = main->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // QWidget::grab omits the pointer. Place the actual Windows hand/cross
    // bitmap at the synthetic event position, after asserting Qt cursor state.
    // This is explicitly a cursor-composited Qt capture, not an OS screenshot.
#ifdef Q_OS_WIN
    const auto shape = view->viewport()->cursor().shape();
    HCURSOR cursor = LoadCursorW(nullptr, shape == Qt::PointingHandCursor ? IDC_HAND : IDC_CROSS);
    ICONINFO icon{};
    BITMAPINFO info{};info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width();info.bmiHeader.biHeight = -image.height();
    info.bmiHeader.biPlanes = 1;info.bmiHeader.biBitCount = 32;info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    check(cursor && dc && bitmap && bits && GetIconInfo(cursor, &icon), "native cursor evidence is available");
    const HGDIOBJ old = SelectObject(dc, bitmap);
    memcpy(bits, image.constBits(), image.sizeInBytes());
    const QPoint position = view->viewport()->mapTo(main, point.toPoint());
    check(DrawIconEx(dc, qRound(position.x() * image.devicePixelRatio()) - int(icon.xHotspot),
                     qRound(position.y() * image.devicePixelRatio()) - int(icon.yHotspot),
                     cursor, 0, 0, 0, nullptr, DI_NORMAL), "native cursor glyph composed at the tested hit location");
    memcpy(image.bits(), bits, image.sizeInBytes());
    for (int y = 0; y < image.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) row[x] |= 0xff000000;
    }
    SelectObject(dc, old);DeleteObject(bitmap);DeleteDC(dc);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);if (icon.hbmMask) DeleteObject(icon.hbmMask);
#endif
    check(image.save(qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT") + "/" + file), "full-client hover evidence captured");
}
static void tests()
{
    auto* board = UBApplication::boardController;auto* main = UBApplication::mainWindow;
    auto* draw = UBDrawingController::drawingController();
    UBApplication::applicationController->showBoard();board->paletteManager()->tipsPalette()->hide();
    main->setAnimated(false);main->resize(1440, 900);
    UBSettings::settings()->setPenWidth(.5);
    draw->setLinePattern(UBDrawingController::SolidLinePattern);draw->setShapeFillEnabled(false);
    for (int percent : {50, 100, 200}) {
        board->addScene();board->setBorderlessPageSize(true);
        board->zoom((percent / 100.) / board->currentZoom(), QPointF());
        auto* view = board->controlView();view->centerOn(2800, -1900);settle();
        auto scene = board->activeScene();auto* stack = UBApplication::undoStack.data();
        auto* line = drawShape(UBDrawingController::StraightLineGeometry,
                               view->mapToScene(QPoint(190, 150)), view->mapToScene(QPoint(380, 150)));
        const QPoint nearPoint(280, 154), farPoint(280, 164);
        check(QGuiApplication::screenAt(view->viewport()->mapToGlobal(nearPoint)), "native-window fixture remains inside the synthetic screen");
        check(owner(scene->itemAt(view->mapToScene(nearPoint), view->viewportTransform())) != line,
              "test point deliberately misses the unchanged precise thin-line silhouette");
        const auto before = renderContent(scene, UBGraphicsScene::NonScreen);
        const int history = stack->index();
        hover(view, nearPoint);
        check(view->viewport()->cursor().shape() == Qt::PointingHandCursor, "near a thin line the selector immediately displays a pointing hand");
        hover(view, nearPoint + QPoint(1, 0));
        check(view->viewport()->cursor().shape() == Qt::PointingHandCursor, "single-pixel hover motion is not swallowed by the drag threshold");
        check(!line->isSelected() && stack->index() == history, "hover never selects or creates undo entries");
        check(renderContent(scene, UBGraphicsScene::NonScreen) == before
                  && renderContent(scene, UBGraphicsScene::Podcast) == before,
              "hover tolerance changes neither exported nor recording content");
        if (percent == 100) capture(view, nearPoint, "shape-hover-line-1440.png");
        hover(view, farPoint);check(view->viewport()->cursor().shape() != Qt::PointingHandCursor, "outside the local tolerance restores the selector cursor");
        hover(view, nearPoint);click(view, nearPoint);
        check(line->isSelected() && stack->index() == history, "the hand's near-line target can actually be selected without undo noise");
        auto* delegate = static_cast<UBGraphicsPixmapItemDelegate*>(line->Delegate());
        auto* frame = delegate->frame();
        check(frame && frame->objectName() == "lineEndpointFrame", "tolerant selection preserves the two-endpoint editor");
        const QPoint endpoint = view->mapFromScene(line->mapToScene(line->editableLine().p1()));
        hover(view, endpoint);
        check(view->viewport()->cursor().shape() == Qt::CrossCursor, "endpoint handle keeps its precise cross cursor instead of the hand");
        if (percent == 100) capture(view, endpoint, "shape-hover-endpoint-1440.png");
        hover(view, QPoint(280, 150));check(view->viewport()->cursor().shape() == Qt::PointingHandCursor, "selected line body also uses a hand while handles stay distinct");

        line->setSelected(false);delegate->freeControls();settle();
        const QPointF original = line->pos();const QPoint delta(28, 18);
        hover(view, nearPoint);mouse(view->viewport(), QEvent::MouseButtonPress, nearPoint, Qt::LeftButton);
        mouse(view->viewport(), QEvent::MouseMove, nearPoint + delta, Qt::LeftButton);
        mouse(view->viewport(), QEvent::MouseButtonRelease, nearPoint + delta);
        check(QLineF(line->pos(), original + view->mapToScene(nearPoint + delta) - view->mapToScene(nearPoint)).length() < .01,
              "a near-line press can drag the real line rather than starting a rubber band");
        check(stack->index() == history + 1, "tolerant body drag commits exactly one undo step");
        board->undo();check(line->pos() == original, "undo restores tolerant drag exactly");
        board->redo();check(line->pos() != original, "redo restores tolerant drag");board->undo();
        line->setSelected(false);delegate->freeControls();
        delegate->lock(true);hover(view, nearPoint);click(view, nearPoint);
        check(line->isSelected(), "locked shape remains selectable for its context actions");
        mouse(view->viewport(), QEvent::MouseButtonPress, nearPoint, Qt::LeftButton);
        mouse(view->viewport(), QEvent::MouseMove, nearPoint + delta, Qt::LeftButton);
        mouse(view->viewport(), QEvent::MouseButtonRelease, nearPoint + delta);
        check(line->pos() == original, "locked shape does not move through tolerant selection");
        delegate->lock(false);scene->deselectAllItems();delegate->freeControls();

        auto* picture = new UBGraphicsPixmapItem;
        QPixmap pixels(24, 24);pixels.fill(QColor("#e2e8f0"));picture->setPixmap(pixels);
        picture->setScale(1. / view->viewportTransform().m11());
        picture->setPos(view->mapToScene(QPoint(268, 142)));scene->addItem(picture);
        picture->setZValue(line->zValue() - 10);hover(view, nearPoint);
        check(view->viewport()->cursor().shape() == Qt::PointingHandCursor,
              "thin shape above a picture retains its near-outline hand target");
        picture->setZValue(line->zValue() + 10);hover(view, nearPoint);
        check(view->viewport()->cursor().shape() != Qt::PointingHandCursor,
              "foreground picture occludes a hidden shape instead of offering a false hand");
        picture->hide();

        QToolButton* selector = nullptr;
        for (auto* button : main->findChildren<QToolButton*>())
            if (button->isVisible() && button->defaultAction() == main->actionSelector) selector = button;
        check(selector, "actual selection control is available for canvas-to-toolbar routing");
        const auto controlCursor = selector->cursor();
        hover(view, nearPoint);
        mouse(selector, QEvent::MouseMove, selector->rect().center());
        check(selector->cursor().shape() == controlCursor.shape() && selector->testAttribute(Qt::WA_SetCursor)
                  && view->viewport()->cursor().shape() != Qt::PointingHandCursor,
              "entering the toolbar releases the shape hand and retains the control cursor");
        hover(view, nearPoint);
        check(view->viewport()->cursor().shape() == Qt::PointingHandCursor,
              "returning from controls restores the hand without requiring a click");

        auto* circle = drawShape(UBDrawingController::CircleGeometry,
                                 view->mapToScene(QPoint(300, 260)), view->mapToScene(QPoint(350, 260)));
        hover(view, QPoint(246, 260));check(view->viewport()->cursor().shape() == Qt::PointingHandCursor, "thin circle outline has the same screen-space hand tolerance");
        if (percent == 100) capture(view, QPoint(246, 260), "shape-hover-circle-1440.png");
        hover(view, QPoint(300, 260));check(view->viewport()->cursor().shape() != Qt::PointingHandCursor, "hollow circle center is not treated as a solid selection rectangle");
        line->setSelected(true);
        click(view, QPoint(246, 260), Qt::ControlModifier);
        check(circle->isSelected() && line->isSelected(), "Ctrl near-outline selection retains the existing multi-selection path");
        scene->deselectAllItems();settle();
        auto* square = drawShape(UBDrawingController::SquareGeometry,
                                 view->mapToScene(QPoint(190, 320)), view->mapToScene(QPoint(370, 380)));
        hover(view, QPoint(280, 316));
        check(view->viewport()->cursor().shape() == Qt::PointingHandCursor,
              "thin non-line shape outline uses the same hand tolerance");
        hover(view, QPoint(280, 350));
        check(view->viewport()->cursor().shape() != Qt::PointingHandCursor,
              "unselected hollow non-line shape does not acquire a filled hit rectangle");
        square->hide();
        if (percent == 100) {
            const QRectF circleBounds = circle->boundingRect();
            const QPointF circleCenter = circle->mapToScene(circleBounds.center());
            const QPointF leftEdge = circle->mapToScene(QPointF(circleBounds.left(), circleBounds.center().y()));
            main->resize(1024, 800);settle();view->centerOn(circleCenter);settle();
            const QPoint narrowedProbe = view->mapFromScene(leftEdge) - QPoint(4, 0);
            check(view->viewport()->rect().contains(narrowedProbe)
                      && QGuiApplication::screenAt(view->viewport()->mapToGlobal(narrowedProbe)),
                  "narrow hover fixture remains inside both the viewport and the synthetic screen");
            hover(view, narrowedProbe);
            check(view->viewport()->cursor().shape() == Qt::PointingHandCursor, "narrow layout retains shape hover without altering the toolbar");
            capture(view, narrowedProbe, "shape-hover-narrow-1024.png");main->resize(1440, 900);settle();
        }
        // Tool ownership is unchanged; selector tolerance never modifies ink.
        for (int tool : {int(UBStylusTool::Pen), int(UBStylusTool::Marker), int(UBStylusTool::Eraser)}) {
            draw->setStylusTool(tool);hover(view, nearPoint);
            check(view->viewport()->cursor().shape() != Qt::PointingHandCursor, "drawing tools retain their own cursor over shape geometry");
        }
        draw->setStylusTool(UBStylusTool::Selector);
    }
    std::cout << "PASS " << checks << " shape hover checks" << std::endl;
}
static void ready()
{
    if (QThread::currentThread()->loopLevel() == 0 || !UBApplication::boardController || !UBApplication::boardController->activeScene()) {
        QTimer::singleShot(100, qApp, ready);return;
    }
    try { tests();std::_Exit(0); }
    catch (const std::exception& error) { std::cerr << error.what() << std::endl;std::_Exit(1); }
}
int main(int argc, char** argv)
{
    if (qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen") return 2;
    Q_INIT_RESOURCE(OpenBoard);UBApplication app("OpenBoard-shape-hover-tests", argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QApplication::setFont(QFont("Microsoft YaHei UI", 9));
    const auto profile = qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    if (profile.isEmpty() || QDir::cleanPath(UBSettings::userDataDirectory()) != QDir::cleanPath(profile)) return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500, qApp, ready);QTimer::singleShot(120000, qApp, [] {std::_Exit(4);});return app.exec("");
}
