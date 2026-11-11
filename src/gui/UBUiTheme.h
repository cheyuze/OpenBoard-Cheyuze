#ifndef UB_UI_THEME_H
#define UB_UI_THEME_H

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;
class QToolBar;

// Presentation helpers. The overflow adapter watches only its own toolbar
// and disclosure; no application-wide or canvas filters. Recording, cursor
// and drawing input lifecycles remain unchanged.
namespace UBUiTheme
{
    QColor ink();
    QColor muted();
    QColor blue();
    QColor border();
    QColor selected();
    QIcon icon(const QString& name, const QColor& accent = QColor());
    void applyActionIcons(QWidget* root);
    void normalizeBoardToolbar(QToolBar* toolbar, bool displayText);
}

#endif
