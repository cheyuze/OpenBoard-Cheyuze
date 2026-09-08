/*
 * Copyright (C) 2015-2022 Departement de l'Instruction Publique (DIP-SEM)
 *
 * This file is part of OpenBoard.
 */

#include "UBCameraPreviewWindow.h"

#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVideoFrame>
#include <QVideoSink>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

UBCameraPreviewWindow::UBCameraPreviewWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint
                       | Qt::WindowStaysOnTopHint
                       | Qt::WindowDoesNotAcceptFocus)
    , mCamera(nullptr)
    , mCaptureSession(nullptr)
    , mVideoSink(nullptr)
    , mDragging(false)
    , mUserAdjustedGeometry(false)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("摄像头预览"));
    resize(240, 150);
}

UBCameraPreviewWindow::~UBCameraPreviewWindow()
{
    stopCamera();
}

bool UBCameraPreviewWindow::startCamera()
{
    if (mCamera)
    {
        show();
        ensureSystemTopMost();
        return true;
    }

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty())
    {
        emit cameraUnavailable(QStringLiteral("没有检测到可用的摄像头。"));
        return false;
    }

    QCameraDevice device;
    if (!mCameraDeviceName.isEmpty())
    {
        for (const QCameraDevice &candidate : cameras)
        {
            if (candidate.description() == mCameraDeviceName)
            {
                device = candidate;
                break;
            }
        }
    }
    if (device.isNull())
        device = QMediaDevices::defaultVideoInput();
    if (device.isNull())
        device = cameras.first();

    mCaptureSession = new QMediaCaptureSession(this);
    mVideoSink = new QVideoSink(this);
    mCamera = new QCamera(device, this);

    mCaptureSession->setCamera(mCamera);
    mCaptureSession->setVideoSink(mVideoSink);

    connect(mVideoSink, &QVideoSink::videoFrameChanged,
            this, &UBCameraPreviewWindow::videoFrameChanged);
    connect(mCamera, &QCamera::errorOccurred,
            this, &UBCameraPreviewWindow::cameraErrorOccurred);

    mCurrentFrame = QImage();
    show();
    ensureSystemTopMost();
    mCamera->start();
    update();
    return true;
}

void UBCameraPreviewWindow::stopCamera()
{
    hide();
    clearCameraObjects();
    mCurrentFrame = QImage();
    update();
}

void UBCameraPreviewWindow::setCameraDeviceName(const QString &deviceName)
{
    if (mCameraDeviceName == deviceName)
        return;

    const bool restart = mCamera != nullptr;
    if (restart)
        stopCamera();
    mCameraDeviceName = deviceName;
    if (restart)
        startCamera();
}

QString UBCameraPreviewWindow::cameraDeviceName() const
{
    return mCameraDeviceName;
}

bool UBCameraPreviewWindow::isCameraActive() const
{
    return mCamera && mCamera->isActive();
}

bool UBCameraPreviewWindow::hasFrame() const
{
    return !mCurrentFrame.isNull();
}

void UBCameraPreviewWindow::setInteractionBounds(const QRect &bounds)
{
    mInteractionBounds = bounds;
    if (!bounds.isEmpty() && isVisible())
        setGeometry(constrainedGeometry(geometry(), Qt::Edges()));
}

bool UBCameraPreviewWindow::hasUserAdjustedGeometry() const
{
    return mUserAdjustedGeometry;
}

QImage UBCameraPreviewWindow::renderedPreview()
{
    if (mCurrentFrame.isNull())
        return QImage();

    QImage preview(size(), QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    render(&painter, QPoint(), QRegion(), QWidget::DrawWindowBackground);
    return preview;
}

void UBCameraPreviewWindow::videoFrameChanged(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    QImage image = frame.toImage();
    if (image.isNull())
        return;

    // A mirrored self-view feels natural and matches common meeting software.
    mCurrentFrame = image.mirrored(true, false);
    update();
}

void UBCameraPreviewWindow::cameraErrorOccurred()
{
    const QString message = mCamera && !mCamera->errorString().isEmpty()
            ? QStringLiteral("摄像头无法启动：%1").arg(mCamera->errorString())
            : QStringLiteral("摄像头无法启动，请检查系统权限或是否被其他应用占用。");
    stopCamera();
    emit cameraUnavailable(message);
}

void UBCameraPreviewWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF outer = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    const qreal cornerRadius = 22.0;

    QPainterPath clipPath;
    clipPath.addRoundedRect(outer, cornerRadius, cornerRadius);
    painter.setClipPath(clipPath);
    painter.fillRect(rect(), QColor(15, 23, 42, 242));

    if (!mCurrentFrame.isNull())
    {
        const QSizeF targetSize = outer.size();
        const QSizeF frameSize = mCurrentFrame.size();
        const qreal targetRatio = targetSize.width() / targetSize.height();
        const qreal frameRatio = frameSize.width() / frameSize.height();

        QRectF sourceRect(QPointF(0, 0), frameSize);
        if (frameRatio > targetRatio)
        {
            const qreal sourceWidth = frameSize.height() * targetRatio;
            sourceRect.setLeft((frameSize.width() - sourceWidth) / 2.0);
            sourceRect.setWidth(sourceWidth);
        }
        else
        {
            const qreal sourceHeight = frameSize.width() / targetRatio;
            sourceRect.setTop((frameSize.height() - sourceHeight) / 2.0);
            sourceRect.setHeight(sourceHeight);
        }

        painter.drawImage(outer, mCurrentFrame, sourceRect);
    }
    else
    {
        painter.setPen(QColor(226, 232, 240));
        QFont font = painter.font();
        font.setPointSize(10);
        painter.setFont(font);
        painter.drawText(outer, Qt::AlignCenter, QStringLiteral("摄像头启动中…"));
    }

    painter.setClipping(false);
    painter.setPen(QPen(QColor(255, 255, 255, 185), 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outer, cornerRadius, cornerRadius);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(34, 197, 94));
    painter.drawEllipse(QPointF(17, 17), 5.0, 5.0);
}

void UBCameraPreviewWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureSystemTopMost();
}

void UBCameraPreviewWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    mDragging = true;
    mResizeEdges = resizeEdgesAt(event->position().toPoint());
    mDragStartGeometry = geometry();
    mDragStartGlobalPosition = event->globalPosition().toPoint();
    mUserAdjustedGeometry = true;
    if (mResizeEdges == Qt::Edges())
        setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void UBCameraPreviewWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!mDragging)
    {
        updateInteractionCursor(event->position().toPoint());
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = event->globalPosition().toPoint()
            - mDragStartGlobalPosition;
    QRect adjusted = mDragStartGeometry;

    if (mResizeEdges == Qt::Edges())
    {
        adjusted.translate(delta);
    }
    else
    {
        if (mResizeEdges.testFlag(Qt::LeftEdge))
            adjusted.setLeft(adjusted.left() + delta.x());
        if (mResizeEdges.testFlag(Qt::RightEdge))
            adjusted.setRight(adjusted.right() + delta.x());
        if (mResizeEdges.testFlag(Qt::TopEdge))
            adjusted.setTop(adjusted.top() + delta.y());
        if (mResizeEdges.testFlag(Qt::BottomEdge))
            adjusted.setBottom(adjusted.bottom() + delta.y());
    }

    setGeometry(constrainedGeometry(adjusted, mResizeEdges));
    event->accept();
}

void UBCameraPreviewWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && mDragging)
    {
        mDragging = false;
        mResizeEdges = Qt::Edges();
        updateInteractionCursor(event->position().toPoint());
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void UBCameraPreviewWindow::leaveEvent(QEvent *event)
{
    if (!mDragging)
        unsetCursor();
    QWidget::leaveEvent(event);
}

Qt::Edges UBCameraPreviewWindow::resizeEdgesAt(const QPoint &position) const
{
    constexpr int edgeWidth = 10;
    Qt::Edges edges;
    if (position.x() <= edgeWidth)
        edges |= Qt::LeftEdge;
    else if (position.x() >= width() - edgeWidth - 1)
        edges |= Qt::RightEdge;

    if (position.y() <= edgeWidth)
        edges |= Qt::TopEdge;
    else if (position.y() >= height() - edgeWidth - 1)
        edges |= Qt::BottomEdge;
    return edges;
}

void UBCameraPreviewWindow::updateInteractionCursor(const QPoint &position)
{
    const Qt::Edges edges = resizeEdgesAt(position);
    const bool horizontal = edges.testFlag(Qt::LeftEdge)
            || edges.testFlag(Qt::RightEdge);
    const bool vertical = edges.testFlag(Qt::TopEdge)
            || edges.testFlag(Qt::BottomEdge);

    if (horizontal && vertical)
    {
        const bool forwardDiagonal = (edges.testFlag(Qt::LeftEdge)
                && edges.testFlag(Qt::TopEdge))
                || (edges.testFlag(Qt::RightEdge)
                && edges.testFlag(Qt::BottomEdge));
        setCursor(forwardDiagonal ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    }
    else if (horizontal)
        setCursor(Qt::SizeHorCursor);
    else if (vertical)
        setCursor(Qt::SizeVerCursor);
    else
        setCursor(Qt::SizeAllCursor);
}

QRect UBCameraPreviewWindow::constrainedGeometry(
        const QRect &requestedGeometry, Qt::Edges resizeEdges) const
{
    constexpr int minimumWidth = 140;
    constexpr int minimumHeight = 88;

    QRect result = requestedGeometry;
    const QRect bounds = mInteractionBounds;
    const int effectiveMinimumWidth = bounds.isEmpty()
            ? minimumWidth : qMin(minimumWidth, bounds.width());
    const int effectiveMinimumHeight = bounds.isEmpty()
            ? minimumHeight : qMin(minimumHeight, bounds.height());

    if (result.width() < effectiveMinimumWidth)
    {
        if (resizeEdges.testFlag(Qt::LeftEdge))
            result.setLeft(result.right() - effectiveMinimumWidth + 1);
        else
            result.setRight(result.left() + effectiveMinimumWidth - 1);
    }
    if (result.height() < effectiveMinimumHeight)
    {
        if (resizeEdges.testFlag(Qt::TopEdge))
            result.setTop(result.bottom() - effectiveMinimumHeight + 1);
        else
            result.setBottom(result.top() + effectiveMinimumHeight - 1);
    }

    if (bounds.isEmpty())
        return result;

    if (result.width() > bounds.width())
        result.setWidth(bounds.width());
    if (result.height() > bounds.height())
        result.setHeight(bounds.height());
    if (result.left() < bounds.left())
        result.moveLeft(bounds.left());
    if (result.top() < bounds.top())
        result.moveTop(bounds.top());
    if (result.right() > bounds.right())
        result.moveRight(bounds.right());
    if (result.bottom() > bounds.bottom())
        result.moveBottom(bounds.bottom());
    return result;
}

void UBCameraPreviewWindow::ensureSystemTopMost()
{
    if (isVisible())
        raise();

#ifdef Q_OS_WIN
    if (winId())
    {
        SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        // The preview is composited into the encoder frame explicitly.  Keep
        // the native overlay out of Windows screen grabs to avoid capturing it
        // twice.  Older Windows versions simply ignore this affinity value.
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()),
                WDA_EXCLUDEFROMCAPTURE);
    }
#endif
}

void UBCameraPreviewWindow::clearCameraObjects()
{
    if (mCamera)
        mCamera->stop();

    if (mCaptureSession)
    {
        mCaptureSession->setVideoSink(nullptr);
        mCaptureSession->setCamera(nullptr);
    }

    delete mCamera;
    mCamera = nullptr;
    delete mVideoSink;
    mVideoSink = nullptr;
    delete mCaptureSession;
    mCaptureSession = nullptr;
}
