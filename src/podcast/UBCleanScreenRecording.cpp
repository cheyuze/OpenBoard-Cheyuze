// SPDX-License-Identifier: GPL-3.0-or-later
#include "UBCleanScreenRecording.h"
#include <QApplication>
#include <QEvent>
#include <QOperatingSystemVersion>
#include <QScopedValueRollback>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace { constexpr int PauseHotkey = 0x4f31; constexpr int StopHotkey = 0x4f32; }

UBCleanScreenRecording::UBCleanScreenRecording(QObject *parent) : QObject(parent) {}
UBCleanScreenRecording::~UBCleanScreenRecording() { finish(); }

bool UBCleanScreenRecording::fail(const QString &reason)
{
    const bool changed = mLastError != reason;
    mLastError = reason;
    if (changed) emit exclusionFailed(reason);
    return false;
}

bool UBCleanScreenRecording::prepare()
{
    if (mPrepared) return true;
    mLastError.clear();
#ifdef Q_OS_WIN
    const auto version = QOperatingSystemVersion::current();
    BOOL composition = FALSE;
    // Earlier versions accept 0x11 but treat it as WDA_MONITOR (black boxes).
    // Do not silently fall back to hiding controls or covering their pixels.
    if (version < QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 19041)
            || FAILED(DwmIsCompositionEnabled(&composition)) || !composition)
        return fail(QStringLiteral("当前系统不支持在保留控件显示的同时将其排除出录像。需要 Windows 10 2004 或更新版本，也可改用应用窗口录制。"));
    // Shortcuts are optional now: the visible recording bar always works.
    mPauseRegistered = RegisterHotKey(nullptr, PauseHotkey, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F9);
    mStopRegistered = RegisterHotKey(nullptr, StopHotkey, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F10);
    mPrepared = true;
    qApp->installNativeEventFilter(this);
    qApp->installEventFilter(this);
    return true;
#else
    return fail(QStringLiteral("当前系统暂不支持将可见的控制面板排除出全屏录像，请改用应用窗口录制。"));
#endif
}

bool UBCleanScreenRecording::isControlWindow(QWidget *widget) const
{
    if (!widget || !widget->isWindow()) return false;
    // Menus/tooltips are separate native windows, including nested popups
    // created only after recording starts. Limit this to our Qt process.
    if (widget->windowType() == Qt::Popup || widget->windowType() == Qt::ToolTip
            || widget->inherits("UBFloatingPalette") || widget->inherits("UBDockPalette"))
        return true;
    for (QWidget *owner = widget; owner; owner = owner->parentWidget())
        if (mRoots.contains(owner)) return true;
    return false;
}

bool UBCleanScreenRecording::excludeWindow(QWidget *widget)
{
#ifdef Q_OS_WIN
    if (!widget || !widget->isWindow()) return true;
    QScopedValueRollback<bool> guard(mApplying, true);
    const auto handle = widget->winId();
    const HWND native = reinterpret_cast<HWND>(handle);
    DWORD original = WDA_NONE;
    if (!GetWindowDisplayAffinity(native, &original))
        return fail(QStringLiteral("无法读取录制控件的窗口状态（%1）。录制未继续，控件仍可正常操作。")
                    .arg(GetLastError()));

    bool tracked = false;
    for (auto &state : mWindows)
    {
        if (state.widget != widget) continue;
        tracked = true;
        if (state.handle != quintptr(handle))
            state = {widget, quintptr(handle), original}; // Qt recreated its HWND.
        break;
    }
    if (!tracked) mWindows.append({widget, quintptr(handle), original});
    if (original != WDA_EXCLUDEFROMCAPTURE
            && !SetWindowDisplayAffinity(native, WDA_EXCLUDEFROMCAPTURE))
        return fail(QStringLiteral("无法从录像中排除控制面板（%1）。录制未继续，控件仍可正常操作。")
                    .arg(GetLastError()));
    DWORD effective = WDA_NONE;
    if (!GetWindowDisplayAffinity(native, &effective) || effective != WDA_EXCLUDEFROMCAPTURE)
        return fail(QStringLiteral("系统未能启用录制控件排除，录制未继续。"));
    return true;
#else
    Q_UNUSED(widget);
    return false;
#endif
}

bool UBCleanScreenRecording::excludeControls(const QList<QWidget *> &widgets)
{
    if (!mPrepared) return false;
    mLastError.clear();
    for (QWidget *widget : widgets)
        if (widget && !mRoots.contains(widget->window())) mRoots.append(widget->window());
    mExcluding = true;
    for (const auto &root : mRoots)
        if (root && !excludeWindow(root)) return false;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (isControlWindow(widget) && !excludeWindow(widget)) return false;
#ifdef Q_OS_WIN
    DwmFlush(); // Apply exclusions before the first desktop frame is sampled.
#endif
    return true;
}

void UBCleanScreenRecording::finish()
{
    mExcluding = false;
    qApp->removeEventFilter(this);
    qApp->removeNativeEventFilter(this);
#ifdef Q_OS_WIN
    for (const auto &state : mWindows)
    {
        if (!state.widget || quintptr(state.widget->internalWinId()) != state.handle) continue;
        const HWND native = reinterpret_cast<HWND>(state.handle);
        DWORD current = WDA_NONE;
        // Never undo a different exclusion policy applied by the camera or
        // another owner, and never touch a destroyed/reused HWND.
        if (GetWindowDisplayAffinity(native, &current) && current == WDA_EXCLUDEFROMCAPTURE)
            SetWindowDisplayAffinity(native, state.originalAffinity);
    }
    if (mPauseRegistered) UnregisterHotKey(nullptr, PauseHotkey);
    if (mStopRegistered) UnregisterHotKey(nullptr, StopHotkey);
#endif
    mWindows.clear();
    mRoots.clear();
    mPrepared = mPauseRegistered = mStopRegistered = false;
}

bool UBCleanScreenRecording::eventFilter(QObject *object, QEvent *event)
{
    if (mExcluding && !mApplying
            && (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange))
    {
        auto *widget = qobject_cast<QWidget *>(object);
        if (isControlWindow(widget)
                && (event->type() == QEvent::Show || widget->internalWinId()))
            excludeWindow(widget);
    }
    return QObject::eventFilter(object, event);
}

bool UBCleanScreenRecording::nativeEventFilter(const QByteArray &, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (mPrepared && msg->message == WM_HOTKEY
            && ((mPauseRegistered && msg->wParam == PauseHotkey)
                || (mStopRegistered && msg->wParam == StopHotkey)))
    {
        const bool stop = msg->wParam == StopHotkey;
        QTimer::singleShot(0, this, [this, stop] {
            if (!mPrepared) return;
            if (stop) emit stopRequested(); else emit pauseRequested();
        });
        *result = 0;
        return true;
    }
#else
    Q_UNUSED(message); Q_UNUSED(result);
#endif
    return false;
}
