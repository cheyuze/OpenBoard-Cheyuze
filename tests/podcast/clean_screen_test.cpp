#include "podcast/UBCleanScreenRecording.h"
#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <windows.h>
#include <dwmapi.h>
#include <stdexcept>

static void require(bool condition, const char *message)
{ if (!condition) throw std::runtime_error(message); }
static void settle()
{
    QEventLoop events;
    QTimer::singleShot(120, &events, &QEventLoop::quit);
    events.exec();
    DwmFlush();
}
static void color(QWidget &widget, QColor value)
{
    QPalette palette = widget.palette();
    palette.setColor(QPalette::Window, value);
    widget.setPalette(palette);
    widget.setAutoFillBackground(true);
    widget.update();
}
static DWORD affinity(QWidget &widget)
{
    DWORD value = 0;
    require(GetWindowDisplayAffinity(reinterpret_cast<HWND>(widget.winId()), &value), "query affinity");
    return value;
}
static QColor capturedPixel(QPoint global)
{
    QScreen *screen = QGuiApplication::screenAt(global);
    require(screen, "no screen for fixture");
    const QPoint local = global - screen->geometry().topLeft();
    // Exact production capture API, restricted to this synthetic fixture.
    const QImage image = screen->grabWindow(0, local.x(), local.y(), 4, 4).toImage();
    require(!image.isNull(), "screen capture failed");
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try
    {
        constexpr DWORD excluded = 0x11;
        const auto flags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
        QWidget desktop(nullptr, flags), palette(&desktop, flags), camera(nullptr, flags), hidden;
        desktop.setWindowTitle("OpenBoard capture exclusion regression fixture");
        const QPoint origin = QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(60, 120);
        desktop.setGeometry(QRect(origin, QSize(520, 260)));
        palette.setGeometry(QRect(origin + QPoint(30, 30), QSize(160, 90)));
        camera.setGeometry(QRect(origin + QPoint(240, 30), QSize(120, 90)));
        color(desktop, QColor("#245781"));
        color(palette, QColor("#de3048"));
        color(camera, QColor("#24aa58"));
        desktop.show(); palette.show(); camera.show(); palette.raise();
        settle();
        const QPoint sample = palette.mapToGlobal(QPoint(25, 20));
        qInfo() << "Fixture baseline" << sample << palette.geometry() << palette.isVisible()
                << capturedPixel(sample) << "affinity" << affinity(palette);
        require(capturedPixel(sample) == QColor("#de3048"), "fixture control not visible before exclusion");
        require(SetWindowDisplayAffinity(reinterpret_cast<HWND>(camera.winId()), excluded), "camera exclusion setup");
        const QRect paletteGeometry = palette.geometry();
        UBCleanScreenRecording control;
        require(control.prepare(), qPrintable(control.lastError()));
        require(control.excludeControls({&palette, &camera, &hidden}), qPrintable(control.lastError()));
        settle();
        require(control.captureReady(), "capture not ready");
        require(desktop.isVisible() && palette.isVisible() && camera.isVisible(), "controls were physically hidden");
        require(IsWindowVisible(reinterpret_cast<HWND>(palette.winId())), "native window was hidden");
        require(palette.geometry() == paletteGeometry, "control moved/resized");
        require(!hidden.isVisible(), "hidden widget was shown");
        require(affinity(palette) == excluded, "control not excluded");
        require(affinity(desktop) == WDA_NONE, "unrelated capture target excluded");
        require(capturedPixel(sample) == QColor("#245781"), "capture contains control/black box instead of underlying screen");
        color(desktop, QColor("#476d32"));
        settle();
        require(capturedPixel(sample) == QColor("#476d32"), "underlying screen became frozen");
        for (int frame = 0; frame < 6; ++frame)
        {
            const QColor liveColor(40 + frame * 20, 90, 150);
            color(desktop, liveColor);
            palette.move(origin + QPoint(30 + frame * 8, 30));
            settle();
            require(palette.isVisible(), "moving control was hidden");
            require(capturedPixel(palette.mapToGlobal(QPoint(25, 20))) == liveColor,
                    "moving controls exposed their pixels or a stale background");
        }
        QPushButton button("Pause", &palette);
        button.setGeometry(10, 45, 120, 32); button.show();
        int clicks = 0;
        QObject::connect(&button, &QPushButton::clicked, [&] { ++clicks; });
        button.click();
        require(clicks == 1 && button.isEnabled(), "excluded control not operable");

        // A menu and tooltip can be instantiated after capture has started.
        QMenu menu(&palette);
        menu.addAction("Recording controls");
        menu.popup(origin + QPoint(30, 160));
        QWidget tooltip(nullptr, Qt::ToolTip);
        tooltip.resize(60, 30); tooltip.move(origin + QPoint(340, 180)); tooltip.show();
        settle();
        require(menu.isVisible() && tooltip.isVisible(), "popup was hidden");
        require(affinity(menu) == excluded && affinity(tooltip) == excluded, "popup leaked into recording");
        menu.hide(); tooltip.hide();

        // Qt may recreate native windows when flags/owners change.
        palette.setWindowFlag(Qt::WindowStaysOnTopHint, false);
        palette.show(); settle();
        require(palette.isVisible() && affinity(palette) == excluded, "recreated control leaked");
        auto *temporary = new QWidget(&palette, flags);
        temporary->show(); settle();
        require(affinity(*temporary) == excluded, "late child window leaked");
        delete temporary;

        int pause = 0, stop = 0;
        QObject::connect(&control, &UBCleanScreenRecording::pauseRequested, [&] { ++pause; });
        QObject::connect(&control, &UBCleanScreenRecording::stopRequested, [&] { ++stop; });
        qintptr result = 0;
        MSG message = {}; message.message = WM_HOTKEY; message.wParam = 0x4f31;
        const bool pauseRegistered = control.nativeEventFilter({}, &message, &result);
        message.wParam = 0x4f32;
        const bool stopRegistered = control.nativeEventFilter({}, &message, &result);
        QCoreApplication::processEvents();
        require(pause == int(pauseRegistered) && stop == int(stopRegistered), "hotkey actions not delivered");

        control.finish();
        require(palette.isVisible() && camera.isVisible() && !hidden.isVisible(), "finish changed control visibility");
        require(affinity(palette) == WDA_NONE, "control affinity not restored");
        require(affinity(camera) == excluded, "pre-existing camera exclusion lost");
        require(!control.prepared() && !control.captureReady(), "cleanup incomplete");
        control.finish();

        // Production desktop palettes share the annotation window. Exclude
        // that native surface and composite only scene ink, not its widgets.
        palette.hide(); camera.hide();
        QGraphicsScene ink;
        ink.setSceneRect(0, 0, 300, 160);
        ink.addLine(25, 60, 160, 60, QPen(QColor("#d32648"), 8));
        QGraphicsView annotation(&ink, &desktop);
        annotation.setWindowFlags(flags);
        annotation.setAttribute(Qt::WA_TranslucentBackground);
        annotation.setStyleSheet("QGraphicsView { background: transparent; border: none; }");
        annotation.setGeometry(QRect(origin + QPoint(20, 20), QSize(300, 160)));
        QPushButton childControl("Tools", &annotation);
        childControl.setGeometry(180, 80, 90, 40); childControl.show();
        annotation.show(); settle();
        require(control.prepare() && control.excludeControls({&annotation}), "annotation exclusion failed");
        settle();
        require(annotation.isVisible() && childControl.isVisible(), "annotation controls hidden");
        QScreen *screen = QGuiApplication::screenAt(annotation.pos());
        const QRect captureRect(annotation.mapToGlobal(QPoint(0, 0)), annotation.size());
        const QPoint screenPoint = captureRect.topLeft() - screen->geometry().topLeft();
        QPixmap clean = screen->grabWindow(0, screenPoint.x(), screenPoint.y(), captureRect.width(), captureRect.height());
        QPixmap overlay(clean.size()); overlay.setDevicePixelRatio(clean.devicePixelRatio()); overlay.fill(Qt::transparent);
        const QRectF source = annotation.mapToScene(annotation.rect()).boundingRect();
        QPainter inkPainter(&overlay);
        ink.render(&inkPainter, QRectF(QPointF(), QSizeF(captureRect.size())), source, Qt::IgnoreAspectRatio);
        inkPainter.end();
        const auto physical = [ratio=clean.devicePixelRatio()](QPoint point) {
            return QPoint(qRound(point.x() * ratio), qRound(point.y() * ratio));
        };
        const QPoint strokePixel = physical(annotation.mapFromScene(QPointF(70, 60)));
        const QPoint buttonPixel = physical(childControl.geometry().center());
        const QColor behindButton = clean.toImage().pixelColor(buttonPixel);
        require(clean.toImage().pixelColor(strokePixel) != QColor("#d32648"), "annotation surface was not excluded");
        QPainter composition(&clean); composition.drawPixmap(QPointF(), overlay); composition.end();
        require(clean.toImage().pixelColor(strokePixel) == QColor("#d32648"), "ink was lost from final composition");
        require(clean.toImage().pixelColor(buttonPixel) == behindButton, "child controls entered the ink composition");
        control.finish(); annotation.hide();

        // Shortcuts are convenience only: occupation cannot block visible UI.
        const bool holdPause = RegisterHotKey(nullptr, 0x4f41, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F9);
        const bool holdStop = RegisterHotKey(nullptr, 0x4f42, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F10);
        require(control.prepare(), "busy hotkey prevented recording");
        require(control.excludeControls({&palette}), "busy hotkey prevented exclusion");
        // User hides a palette or changes source: finishing must not resurrect it.
        palette.hide(); control.finish();
        require(!palette.isVisible(), "source switch resurrected a hidden control");
        if (holdPause) UnregisterHotKey(nullptr, 0x4f41);
        if (holdStop) UnregisterHotKey(nullptr, 0x4f42);
        qInfo() << "PASS visible controls: live underlying pixels, moving controls, no black rectangle, interactive buttons, menus/tooltips, recreated HWND, ink-only composition, preserved camera policy, busy hotkeys, visibility-neutral cleanup";
    }
    catch (const std::exception &error) { qCritical() << "FAIL" << error.what(); return 1; }
    return 0;
}
