#include <QtWidgets>
#include <QBackingStore>
#include <functional>
#include <iostream>
#include <stdexcept>

// Compile unchanged current production preview methods against real Qt
// scenes, views and backing-store pixels. Only application/settings access is
// adapted; no OpenBoard installation or user document is opened.
class UBBoardView : public QGraphicsView {
public:
    QRegion paintedRegion;
protected:
    void paintEvent(QPaintEvent* event) override {
        paintedRegion += event->region();
        QGraphicsView::paintEvent(event);
    }
};
class UBBoardController {
public:
    qreal zoom = 1;
    qreal systemScaleFactor() const { return 1; }
    qreal currentZoom() const { return zoom; }
};
struct UBApplication { static UBBoardController* boardController; };
UBBoardController* UBApplication::boardController = nullptr;
class UBSettings {
public:
    qreal width = 160;
    static UBSettings* settings() { static UBSettings value; return &value; }
    qreal currentEraserWidth() const { return width; }
};
class UBGraphicsScene : public QGraphicsScene {
public:
    QGraphicsEllipseItem* mEraser = nullptr;
    UBBoardView* view = nullptr;
    UBBoardView* controlView() const { return view; }
    void drawEraser(const QPointF& pPoint, bool pressed = true);
    void redrawEraser(bool pressed);
    void hideEraser();
};
#include "production_eraser_handlers.inc"

static void require(bool passed, const char* message)
{
    if (!passed) throw std::runtime_error(message);
}

static void settle()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 3);
    }
}

struct Fixture {
    UBBoardController board;
    UBGraphicsScene scene;
    UBBoardView view;
    Fixture(qreal zoom = 1) {
        UBApplication::boardController = &board;
        board.zoom = zoom;
        UBSettings::settings()->width = 160;
        scene.setSceneRect(-5000, -5000, 10000, 10000);
        scene.setBackgroundBrush(Qt::white);
        scene.mEraser = scene.addEllipse({}, QPen(QColor(0, 0, 0, 127)),
                                       QBrush(QColor(127, 127, 127, 80)));
        scene.mEraser->hide();
        scene.view = &view;
        view.setScene(&scene);
        view.setOptimizationFlags(QGraphicsView::IndirectPainting | QGraphicsView::DontSavePainterState);
        view.setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
        view.setCacheMode(QGraphicsView::CacheBackground);
        view.setRenderHints(QPainter::Antialiasing);
        view.setFrameShape(QFrame::NoFrame);
        view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view.resize(800, 600);
        view.scale(zoom, zoom);
        view.centerOn(0, 0);
        view.show();
        settle();
        // Fault injection: suppress only Qt's inferred scene dirty regions.
        // Explicit viewport damage must still clean old circles. This makes
        // the otherwise intermittent stale-region failure deterministic.
        view.setViewportUpdateMode(QGraphicsView::NoViewportUpdate);
    }
    ~Fixture() {
        scene.view = nullptr;
        view.setScene(nullptr);
        UBApplication::boardController = nullptr;
    }
    QImage pixels() {
        auto* store = view.window()->backingStore();
        auto* device = store ? store->paintDevice() : nullptr;
        require(device && device->devType() == QInternal::Image, "offscreen image backing store is required");
        auto image = static_cast<QImage*>(device)->copy();
        const qreal dpr = image.devicePixelRatio();
        return image.copy(QRect(view.viewport()->mapTo(view.window(), QPoint()) * dpr,
                                view.viewport()->size() * dpr));
    }
    QColor color(const QPointF& point) {
        const auto image = pixels();
        const QPoint pixel = view.mapFromScene(point) * image.devicePixelRatio();
        return image.pixelColor(pixel);
    }
    bool white(const QPointF& point) {
        const auto value = color(point);
        return value.red() >= 250 && value.green() >= 250 && value.blue() >= 250;
    }
    bool gray(const QPointF& point) {
        const auto value = color(point);
        return value.red() > 195 && value.red() < 235
                && value.green() == value.red() && value.blue() == value.red();
    }
    void seed(const QPointF& point) {
        scene.drawEraser(point, true);
        view.viewport()->repaint();
        settle();
        require(gray(point), "fixture must contain a real painted preview before testing cleanup");
        view.paintedRegion = {};
    }
};

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    int cases = 0, failures = 0;
    auto run = [&](const char* name, const std::function<void()>& test) {
        ++cases;
        try { test(); std::cout << "PASS " << name << std::endl; }
        catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL " << name << ": " << error.what() << std::endl;
        }
    };
    run("move_refreshes_old_and_new_positions", [] {
        Fixture f; f.seed({-180, 0});
        f.scene.drawEraser({180, 0}, false); settle();
        require(f.white({-180, 0}), "old preview remains on screen");
        require(f.gray({180, 0}), "new preview was not displayed");
        require(f.scene.mEraser->pen().style() == Qt::DotLine, "hover outline changed");
        require(f.scene.items().size() == 1, "preview movement must not create document objects");
    });
    run("hide_refreshes_last_position", [] {
        Fixture f; f.seed({0, 0}); f.scene.hideEraser(); settle();
        require(!f.scene.mEraser->isVisible() && f.white({0, 0}), "hidden preview leaves gray pixels");
    });
    run("shrink_cleans_old_outer_ring", [] {
        Fixture f; f.seed({0, 0});
        UBSettings::settings()->width = 16;
        f.scene.drawEraser({0, 0}, true); settle();
        require(f.gray({0, 0}) && f.white({50, 0}), "shrinking preview leaves previous filled circle");
    });
    run("moving_outside_view_cleans_previous_location", [] {
        Fixture f; f.seed({0, 0});
        f.scene.drawEraser({4000, 4000}); settle();
        require(f.white({0, 0}), "offscreen movement leaves old preview");
    });
    run("queued_fast_moves_do_not_leave_trails", [] {
        Fixture f; f.seed({-240, -160});
        const QList<QPointF> points{{0, -160}, {240, -160}, {-240, 120}, {0, 120}, {240, 120}};
        for (const auto& point : points) f.scene.drawEraser(point);
        settle();
        require(f.white({-240, -160}), "initial preview remains after batched input");
        for (int i = 0; i < points.size()-1; ++i)
            require(f.white(points[i]), "intermediate preview remains after batched input");
        require(f.gray(points.last()), "latest preview is missing after batched input");
    });
    for (qreal zoom : {0.5, 0.67, 1.25, 2.5}) {
        const auto name = QString("zoom_%1_cleans_mapped_viewport_area").arg(zoom).toStdString();
        run(name.c_str(), [zoom] {
            Fixture f(zoom); f.seed({-180/zoom, 0});
            f.scene.drawEraser({180/zoom, 0}); settle();
            require(f.white({-180/zoom, 0}) && f.gray({180/zoom, 0}), "zoomed preview damage is mapped incorrectly");
        });
    }
    run("damage_remains_local_not_full_screen", [] {
        Fixture f; f.seed({-180, 0});
        f.scene.drawEraser({180, 0}); settle();
        require(f.view.paintedRegion.contains(f.view.mapFromScene(QPointF(-180, 0)))
                && f.view.paintedRegion.contains(f.view.mapFromScene(QPointF(180, 0))), "both dirty regions must be painted");
        qint64 paintedArea = 0;
        for (const QRect rect : f.view.paintedRegion) paintedArea += qint64(rect.width()) * rect.height();
        require(paintedArea < f.view.viewport()->width()*f.view.viewport()->height()/2,
                "preview updates must not force a full-board repaint");
    });
    run("high_frequency_input_does_not_reenter_painting", [] {
        Fixture f; f.seed({-180, 0});
        QElapsedTimer timer; timer.start();
        for (int i = 0; i < 10000; ++i)
            f.scene.drawEraser({qreal(i%400 - 200), qreal(i%240 - 120)});
        require(f.view.paintedRegion.isEmpty(), "input events must queue updates instead of painting synchronously");
        std::cout << "TIMING 10000 queued preview moves=" << timer.elapsed() << "ms" << std::endl;
    });
    run("missing_view_and_disabled_preview_are_safe", [] {
        Fixture f; f.scene.view = nullptr;
        f.scene.drawEraser({0, 0}); f.scene.hideEraser();
        auto* item = f.scene.mEraser;
        f.scene.mEraser = nullptr;
        f.scene.drawEraser({0, 0}); f.scene.hideEraser();
        f.scene.mEraser = item;
    });
    run("already_hidden_preview_does_not_schedule_paint", [] {
        Fixture f; f.view.paintedRegion = {};
        f.scene.hideEraser(); settle();
        require(f.view.paintedRegion.isEmpty(), "repeated hide must not repaint an invisible preview");
    });
    std::cout << (failures ? "FAIL " : "PASS ") << cases-failures << "/" << cases
              << " eraser preview cases, scale=" << qEnvironmentVariable("QT_SCALE_FACTOR", "1").toStdString() << std::endl;
    return failures ? 1 : 0;
}
