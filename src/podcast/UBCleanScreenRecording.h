// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UBCLEANSCREENRECORDING_H
#define UBCLEANSCREENRECORDING_H
#include <QObject>
#include <QPointer>
#include <QList>
#include <QWidget>
#include <QAbstractNativeEventFilter>

// Keep controls interactive on the monitor, but exclude their native windows
// from desktop capture. This never changes widget visibility or window flags.
class UBCleanScreenRecording : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit UBCleanScreenRecording(QObject *parent = nullptr);
    ~UBCleanScreenRecording() override;
    bool prepare();
    bool prepared() const { return mPrepared; }
    bool captureReady() const { return mPrepared && mExcluding && mLastError.isEmpty(); }
    QString lastError() const { return mLastError; }
    bool excludeControls(const QList<QWidget *> &widgets);
    void finish();
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override;
    bool eventFilter(QObject *object, QEvent *event) override;
signals:
    void pauseRequested();
    void stopRequested();
    void exclusionFailed(const QString &reason);
private:
    struct WindowState
    {
        QPointer<QWidget> widget;
        quintptr handle;
        quint32 originalAffinity;
    };
    bool isControlWindow(QWidget *widget) const;
    bool excludeWindow(QWidget *widget);
    bool fail(const QString &reason);
    QList<QPointer<QWidget>> mRoots;
    QList<WindowState> mWindows;
    QString mLastError;
    bool mPrepared = false;
    bool mExcluding = false;
    bool mApplying = false;
    bool mPauseRegistered = false;
    bool mStopRegistered = false;
};
#endif
