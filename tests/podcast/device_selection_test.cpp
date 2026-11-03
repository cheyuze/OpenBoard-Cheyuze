// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDebug>
#include "podcast/UBRecordingDeviceSelection.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int checked = 0;
    auto check = [&checked](const QString& requested, const QStringList& available,
                           bool enabled, const QString& expected, bool fellBack) {
        const auto result = UBRecordingDeviceSelection::resolve(requested, available, enabled);
        ++checked;
        if (result.name != expected || result.fellBack != fellBack)
            qFatal("Recording device selection regression failed (case %d)", checked);
    };
    const QString old = QStringLiteral("Plantronics C725");
    const QString current = QString::fromUtf8("麦克风阵列 (Realtek(R) Audio)");
    check(old, {current}, true, "Default", true);
    check(old, {}, true, "Default", true); // Let backend explicitly reject no default.
    check(old, {old, current}, true, old, false);
    check(current, {current}, true, current, false);
    check("Default", {current}, true, "Default", false);
    check("Default", {}, true, "Default", false);
    check("", {current}, true, "Default", false);
    check("None", {current}, true, "None", false);
    check(old, {current}, false, old, false); // Muted microphone is not enabled.
    check(old, {}, false, old, false);        // Silent video leaves settings alone.
    check("None", {}, false, "None", false);
    check("", {}, false, "", false);
    check("Old USB speakers", {"Current HDMI"}, true, "Default", true);
    check("Current HDMI", {"Current HDMI"}, true, "Current HDMI", false);
    check("Old USB speakers", {}, false, "Old USB speakers", false);
    const auto first = UBRecordingDeviceSelection::resolve(old, {current}, true);
    check(first.name, {current}, true, "Default", false); // No repeat migration.
    qInfo() << "PASS" << checked << "recording device fallback cases";
    return 0;
}
