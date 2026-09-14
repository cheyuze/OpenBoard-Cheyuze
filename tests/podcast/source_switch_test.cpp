#include <QtCore>
#include <QtWidgets>
#include <cstdio>

// Only the source selector is under test. These small dependencies avoid
// opening OpenBoard, documents, desktop capture or recording devices.
class UBBoardView : public QWidget
{
    Q_OBJECT
};

class TestBoardController : public QObject
{
    Q_OBJECT
signals:
    void activeSceneChanged();
    void backgroundChanged();
    void controlViewportChanged();
};

class UBApplicationController
{
public:
    enum MainMode { Board, Internet };
    MainMode mode = Board;
    MainMode displayMode() const { return mode; }
};

class UBApplication
{
public:
    static TestBoardController* boardController;
    static UBApplicationController* applicationController;
};
TestBoardController* UBApplication::boardController = nullptr;
UBApplicationController* UBApplication::applicationController = nullptr;

class UBPodcastController : public QObject
{
    Q_OBJECT
public:
    enum RecordingState { Stopped, Recording, Paused, Stopping };
    void setSourceWidget(QWidget* pWidget);
    QWidget* mSourceWidget = nullptr;
    bool mInitialized = false;
    bool mIsDesktopMode = false;
    QTransform mViewToVideoTransform;
    QImage mLatestCapture = QImage(16, 16, QImage::Format_RGB32);
    unsigned int sBackgroundColor = 0;
    QObject* mSourceScene = nullptr;
    int mScreenGrabingTimerEventID = 0;
    int mVideoFramesPerSecondAtStart = 30;
    RecordingState mRecordingState = Stopped;
    int deliveredTicks = 0;

public slots:
    void activeSceneChanged() { mSourceScene = this; }
    void sceneBackgroundChanged() {}

protected:
    void timerEvent(QTimerEvent* event) override
    {
        if (event->timerId() == mScreenGrabingTimerEventID
                && mRecordingState == Recording)
            ++deliveredTicks;
    }

private:
    void widgetSizeChanged(const QSizeF&) {}
    void startNextChapter() {}
};

// Generated verbatim from UBPodcastController.cpp by the build. The actual
// branching and QObject startTimer/killTimer calls are not copied into a mock.
#include "source_switch_method.inc"

static void tick()
{
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestBoardController boardController;
    UBApplicationController applicationController;
    UBApplication::boardController = &boardController;
    UBApplication::applicationController = &applicationController;

    for (const auto state : {UBPodcastController::Recording, UBPodcastController::Paused}) {
        QWidget desktop;
        UBBoardView board;
        QWidget anotherSource;
        UBPodcastController controller;
        controller.mRecordingState = state;
        controller.mIsDesktopMode = true;
        controller.setSourceWidget(&desktop);
        if (!controller.mScreenGrabingTimerEventID) {
            std::printf("FAIL: desktop capture timer not started for state %d\n", int(state));
            return 1;
        }
        controller.mIsDesktopMode = false;
        controller.setSourceWidget(&board);
        const int timerId = controller.mScreenGrabingTimerEventID;
        if (!timerId) {
            std::printf("FAIL: desktop -> board lost capture timer for state %d\n", int(state));
            return 1;
        }
        controller.setSourceWidget(&board);
        if (controller.mScreenGrabingTimerEventID != timerId) {
            std::printf("FAIL: unchanged source restarted its timer\n");
            return 1;
        }
        controller.mRecordingState = UBPodcastController::Recording;
        tick();
        if (!controller.deliveredTicks) {
            std::printf("FAIL: board timer did not run after returning/resuming\n");
            return 1;
        }
        controller.mRecordingState = UBPodcastController::Stopped;
        controller.setSourceWidget(&anotherSource);
        if (controller.mScreenGrabingTimerEventID) {
            std::printf("FAIL: stopped source switching left a capture timer running\n");
            return 1;
        }
    }
    std::printf("PASS: production source selection preserves Recording/Paused board timers; resume ticks; unchanged/stopped sources\n");
    return 0;
}

#include "source_switch_test.moc"
