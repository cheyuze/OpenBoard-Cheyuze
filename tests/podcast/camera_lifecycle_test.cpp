#include <QtCore>
#include <QtWidgets>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>
#include <thread>
#include <cstdio>

#define private public
#include "UBCameraPreviewWindow.h"
#undef private
#include "UBCameraPreviewWindow.cpp"
#include "moc_UBCameraPreviewWindow.cpp"

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    UBCameraPreviewWindow preview;
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::green);
    const QVideoFrame frame(image);

    QVideoSink* oldSink = new QVideoSink(&preview);
    preview.mVideoSink = oldSink;
    QObject::connect(oldSink, &QVideoSink::videoFrameChanged,
                     &preview, &UBCameraPreviewWindow::videoFrameChanged);
    std::thread producer([&]() { oldSink->videoFrameChanged(frame); });
    producer.join();
    preview.stopCamera();
    QCoreApplication::sendPostedEvents(&preview, QEvent::MetaCall);
    if (preview.hasFrame()) {
        std::printf("FAIL: a queued frame repopulated the stopped camera preview\n");
        return 1;
    }

    QVideoSink* activeSink = new QVideoSink(&preview);
    preview.mVideoSink = activeSink;
    QObject::connect(activeSink, &QVideoSink::videoFrameChanged,
                     &preview, &UBCameraPreviewWindow::videoFrameChanged);
    activeSink->videoFrameChanged(frame);
    if (!preview.hasFrame()) {
        std::printf("FAIL: a current camera frame was discarded\n");
        return 1;
    }
    preview.stopCamera();

    int unavailableCount = 0;
    QObject::connect(&preview, &UBCameraPreviewWindow::cameraUnavailable,
                     [&unavailableCount](const QString&) { ++unavailableCount; });
    QCamera* oldCamera = new QCamera(&preview);
    preview.mCamera = oldCamera;
    QObject::connect(oldCamera, &QCamera::errorOccurred,
                     &preview, &UBCameraPreviewWindow::cameraErrorOccurred,
                     Qt::QueuedConnection);
    oldCamera->errorOccurred(QCamera::CameraError, QStringLiteral("synthetic old camera error"));
    preview.stopCamera();
    QCamera* newCamera = new QCamera(&preview);
    preview.mCamera = newCamera;
    QObject::connect(newCamera, &QCamera::errorOccurred,
                     &preview, &UBCameraPreviewWindow::cameraErrorOccurred,
                     Qt::QueuedConnection);
    QCoreApplication::sendPostedEvents(&preview, QEvent::MetaCall);
    if (preview.mCamera != newCamera || unavailableCount != 0) {
        std::printf("FAIL: old queued camera error stopped a replacement camera\n");
        return 1;
    }
    QPointer<QCamera> cameraGuard(newCamera);
    newCamera->errorOccurred(QCamera::CameraError, QStringLiteral("synthetic current error"));
    if (!cameraGuard) {
        std::printf("FAIL: camera deleted while its error signal was still being delivered\n");
        return 1;
    }
    QCoreApplication::sendPostedEvents(&preview, QEvent::MetaCall);
    if (cameraGuard || preview.mCamera || unavailableCount != 1) {
        std::printf("FAIL: current camera error did not stop the preview exactly once\n");
        return 1;
    }
    preview.mDragging = true;
    preview.mResizeEdges = Qt::LeftEdge;
    preview.stopCamera();
    if (preview.mDragging || preview.mResizeEdges != Qt::Edges()) {
        std::printf("FAIL: camera shutdown retained stale drag state\n");
        return 1;
    }
    std::printf("PASS: camera frame/error lifetime guards, deferred cleanup and drag reset\n");
    return 0;
}
