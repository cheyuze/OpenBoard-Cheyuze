#include <QtWidgets>
#include <memory>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// These adapters isolate the production event handlers from document I/O.
// Handler bodies are extracted unchanged by build-input-state.ps1.
using UBGraphicsScene = int;
class UBThumbnail : public QGraphicsRectItem
{
public:
    int sceneIndex() const { return 0; }
    static int heightForWidth(int width) { return width; }
    void adjustThumbnail() {}
    void updatePixmap(const QRectF&) {}
    void setPageScene(std::shared_ptr<int>) {}
};
class FakeThumbnailScene : public QGraphicsScene
{
public:
    int arrangeCalls = 0;
    void arrangeThumbnails() { ++arrangeCalls; }
    UBThumbnail* thumbnailAt(int) { return nullptr; }
    void hightlightItem(int, bool) {}
};
class UBDocument
{
public:
    FakeThumbnailScene thumbnails;
    FakeThumbnailScene* thumbnailScene() { return &thumbnails; }
    int proxy() const { return 0; }
};
class FakeBoardController
{
public:
    int activeSceneIndex() { return 0; }
    void persistViewPositionOnCurrentScene() {}
    void persistCurrentScene() {}
    void setActiveDocumentScene(int) {}
    void moveSceneToIndex(int, int, int) {}
};
struct UBApplication
{
    static inline FakeBoardController instance;
    static inline FakeBoardController* boardController = &instance;
};
struct UBPersistenceManager
{
    static UBPersistenceManager* persistenceManager() { static UBPersistenceManager p; return &p; }
    std::shared_ptr<int> getDocumentScene(int, int) { return {}; }
};
struct UBSettings { static constexpr double minScreenRatio = 1.0; };
class FakeArranger
{
public:
    QSize spacing() const { return {10, 10}; }
    double thumbnailWidth() const { return 80; }
};
class UBBoardThumbnailsView : public QGraphicsView
{
public:
    std::shared_ptr<UBDocument> mDocument;
    int mCurrentIndex = -1;
    bool mScrollbarVisible = false;
    int mThumbnailWidth = 100;
    UBThumbnail* mDropSource = nullptr;
    UBThumbnail* mDropTarget = nullptr;
    bool mDropIndicatorVisible = false;
    QRectF mDropIndicatorRect;
    QTimer mLongPressTimer;
    QPoint mLastPressedMousePos;
    FakeArranger arranger;
    FakeArranger* thumbnailArranger() { return &arranger; }
    void setDocument(std::shared_ptr<UBDocument>);
    void adjustThumbnail();
    void centerOnThumbnail(int);
    void ensureVisibleThumbnail(int);
    void updateActiveThumbnail(int);
    void resizeEvent(QResizeEvent*) override;
    void updateThumbnailPixmap(const QRectF);
    void mousePressEvent(QMouseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
};

namespace UBStylusTool { enum { Selector = 3, Play = 4, Pointer = 8 }; }
struct UBDrawingController
{
    int tool = 7;
    static UBDrawingController* drawingController() { static UBDrawingController c; return &c; }
    int stylusTool() const { return tool; }
};
struct FakePanController
{
    int panCalls = 0;
    void handScroll(double, double) { ++panCalls; }
};
class UBBoardView : public QGraphicsView
{
public:
    bool bIsControl = true;
    bool bIsDesktop = false;
    bool mMouseButtonIsPressed = false;
    bool mTabletStylusIsPressed = false;
    bool mMiddleButtonPanActive = true;
    QPointF mPreviousPoint;
    int lastCursor = -1;
    FakePanController controller;
    FakePanController* mController = &controller;
    void setToolCursor(int tool) { lastCursor = tool; }
    // Laser rendering is covered by the native laser suite. These adapters
    // keep the input/cursor suite focused on the unchanged extracted handlers.
    bool updateLaserPointer(const QPointF&, bool = false) { return false; }
    void hideLaserPointer() {}
    void refreshLaserPointer() {}
    void mouseMoveEvent(QMouseEvent*);
    bool viewportEvent(QEvent*) override;
};

#include "production_handlers.inc"

static bool check(bool ok, const char* message)
{
    std::cout << (ok ? "PASS " : "FAIL ") << message << '\n';
    return ok;
}

int main(int argc, char** argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QApplication app(argc, argv);
    if (argc < 2) return 2;
    const QString test = QString::fromLocal8Bit(argv[1]);
    UBBoardThumbnailsView view;
    bool ok = false;
    if (test == "null_resize")
    {
        QResizeEvent event(QSize(200, 500), QSize(0, 500));
        view.resizeEvent(&event);
        ok = check(!view.mDocument, "empty sidebar can expand from zero width");
    }
    else if (test == "null_callbacks")
    {
        view.adjustThumbnail();
        view.centerOnThumbnail(0);
        view.ensureVisibleThumbnail(0);
        view.updateActiveThumbnail(0);
        view.updateThumbnailPixmap(QRectF());
        QMouseEvent event(QEvent::MouseButtonPress, QPointF(10,10), QPointF(10,10), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        view.mousePressEvent(&event);
        ok = check(!event.isAccepted(), "canvas callbacks and click tolerate absent document");
    }
    else if (test == "document_reset")
    {
        UBThumbnail source;
        view.mDropSource = &source;
        view.mDropTarget = &source;
        view.mDropIndicatorVisible = true;
        view.mCurrentIndex = 9;
        view.mLongPressTimer.start(1000);
        view.setDocument(std::make_shared<UBDocument>());
        ok = check(!view.mDropSource && !view.mDropTarget && !view.mDropIndicatorVisible
                && view.mCurrentIndex == -1 && !view.mLongPressTimer.isActive(),
                "document change retires drag pointers and pending long press");
    }
    else if (test == "external_drop")
    {
        QMimeData data;
        QDropEvent event(QPointF(10,10), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        event.accept();
        view.dropEvent(&event);
        ok = check(!event.isAccepted(), "drop without an internal thumbnail drag is ignored");
    }
    else if (test == "external_enter")
    {
        QMimeData data;
        QDragEnterEvent event(QPoint(10,10), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        event.accept();
        view.dragEnterEvent(&event);
        ok = check(!event.isAccepted() && !view.mDropIndicatorVisible, "external drag cannot activate reorder feedback");
    }
    else if (test == "valid_resize")
    {
        view.setDocument(std::make_shared<UBDocument>());
        const int before = view.mDocument->thumbnails.arrangeCalls;
        QResizeEvent event(QSize(200, 500), QSize(100, 500));
        view.resizeEvent(&event);
        ok = check(view.mDocument->thumbnails.arrangeCalls > before, "nonempty sidebar still rearranges on resize");
    }
    else if (test == "lost_middle_release")
    {
        UBBoardView board;
        QMouseEvent event(QEvent::MouseMove, QPointF(30,40), QPointF(30,40), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        board.mouseMoveEvent(&event);
        ok = check(!board.mMiddleButtonPanActive && board.controller.panCalls == 0 && board.lastCursor == 7,
                "motion after lost middle release restores the tool without panning");
    }
    else if (test == "held_middle")
    {
        UBBoardView board;
        QMouseEvent event(QEvent::MouseMove, QPointF(30,40), QPointF(30,40), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
        board.mouseMoveEvent(&event);
        ok = check(board.mMiddleButtonPanActive && board.controller.panCalls == 1 && board.lastCursor == -1,
                "held middle button still pans");
    }
    else if (test == "cursor_enter")
    {
        UBBoardView board;
        board.mMiddleButtonPanActive = false;
        QEnterEvent event(QPointF(20,20), QPointF(20,20), QPointF(20,20));
        board.viewportEvent(&event);
        ok = check(board.lastCursor == 7, "canvas entry restores current tool cursor without a click");
    }
    else if (test == "cursor_pan")
    {
        UBBoardView board;
        QEnterEvent event(QPointF(20,20), QPointF(20,20), QPointF(20,20));
        board.viewportEvent(&event);
        ok = check(board.lastCursor == -1, "entry does not replace an active middle-pan cursor");
    }
    else if (test == "cursor_pressed")
    {
        UBBoardView board;
        board.mMiddleButtonPanActive = false;
        board.mMouseButtonIsPressed = true;
        QEnterEvent event(QPointF(20,20), QPointF(20,20), QPointF(20,20));
        board.viewportEvent(&event);
        ok = check(board.lastCursor == -1, "entry does not replace a live drag/drawing cursor");
    }
    else if (test == "cursor_display")
    {
        UBBoardView board;
        board.mMiddleButtonPanActive = false;
        board.bIsControl = false;
        QEnterEvent event(QPointF(20,20), QPointF(20,20), QPointF(20,20));
        board.viewportEvent(&event);
        ok = check(board.lastCursor == -1, "entry leaves a passive display view unchanged");
    }
    else if (test == "cursor_selection")
    {
        UBBoardView board;
        board.mMiddleButtonPanActive = false;
        auto* drawing = UBDrawingController::drawingController();
        QEnterEvent event(QPointF(20,20), QPointF(20,20), QPointF(20,20));
        drawing->tool = UBStylusTool::Selector;
        board.viewportEvent(&event);
        drawing->tool = UBStylusTool::Play;
        board.viewportEvent(&event);
        ok = check(board.lastCursor == -1, "selection and interaction item hover cursors remain untouched");
    }
    else return 2;
    return ok ? 0 : 1;
}
