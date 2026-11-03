// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UBRECORDINGDEVICESELECTION_H
#define UBRECORDINGDEVICESELECTION_H

#include <QStringList>

namespace UBRecordingDeviceSelection
{
struct Selection
{
    QString name;
    bool fellBack = false;
};

// Resolve only sources the user wants recorded. Never turn a muted source on,
// change a recording's sound mode, or silently select an arbitrary device.
inline Selection resolve(const QString& requested, const QStringList& available,
                         bool enabled)
{
    if (!enabled || requested == QStringLiteral("None"))
        return {requested, false};
    if (requested.isEmpty() || requested == QStringLiteral("Default"))
        return {QStringLiteral("Default"), false};
    if (available.contains(requested))
        return {requested, false};
    return {QStringLiteral("Default"), true};
}
}

#endif
