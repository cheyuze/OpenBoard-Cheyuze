#ifndef UB_UI_THEME_H
#define UB_UI_THEME_H

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;

// Presentation-only helpers. Never owns widgets, changes window flags, or
// installs event filters: recording/cursor/input lifecycles remain unchanged.
namespace UBUiTheme
{
    QColor ink();
    QColor muted();
    QColor blue();
    QColor border();
    QColor selected();
    QIcon icon(const QString& name, const QColor& accent = QColor());
    void applyActionIcons(QWidget* root);
}

#endif
