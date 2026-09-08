/*
 * Copyright (C) 2015-2022 Departement de l'Instruction Publique (DIP-SEM)
 *
 * This file is part of OpenBoard.
 */

#ifndef UBCAMERAPREVIEWWINDOW_H_
#define UBCAMERAPREVIEWWINDOW_H_

#include <QImage>
#include <QWidget>

class QCamera;
class QMediaCaptureSession;
class QMouseEvent;
class QVideoFrame;
class QVideoSink;

class UBCameraPreviewWindow : public QWidget
{
    Q_OBJECT

public:
    explicit UBCameraPreviewWindow(QWidget *parent = nullptr);
    ~UBCameraPreviewWindow() override;

    bool startCamera();
    void stopCamera();
    void setCameraDeviceName(const QString &deviceName);
    QString cameraDeviceName() const;
    bool isCameraActive() const;
    bool hasFrame() const;
    void setInteractionBounds(const QRect &bounds);
    bool hasUserAdjustedGeometry() const;

    // Returns the same rounded preview that is displayed on screen.  The
    // podcast controller composites this image into every recording source,
    // including native application-window capture.
    QImage renderedPreview();

signals:
    void cameraUnavailable(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private slots:
    void videoFrameChanged(const QVideoFrame &frame);
    void cameraErrorOccurred();

private:
    void ensureSystemTopMost();
    void clearCameraObjects();
    Qt::Edges resizeEdgesAt(const QPoint &position) const;
    void updateInteractionCursor(const QPoint &position);
    QRect constrainedGeometry(const QRect &geometry, Qt::Edges resizeEdges) const;

    QCamera *mCamera;
    QMediaCaptureSession *mCaptureSession;
    QVideoSink *mVideoSink;
    QImage mCurrentFrame;
    QString mCameraDeviceName;
    QRect mInteractionBounds;
    QRect mDragStartGeometry;
    QPoint mDragStartGlobalPosition;
    Qt::Edges mResizeEdges;
    bool mDragging;
    bool mUserAdjustedGeometry;
};

#endif /* UBCAMERAPREVIEWWINDOW_H_ */
