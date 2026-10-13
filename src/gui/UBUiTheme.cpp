#include "UBUiTheme.h"

#include <QAction>
#include <QCache>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>
#include <QWidget>

namespace UBUiTheme
{
QColor ink() { return QColor("#202936"); }
QColor muted() { return QColor("#526174"); }
QColor blue() { return QColor("#2563eb"); }
QColor border() { return QColor("#dbe2eb"); }
QColor selected() { return QColor("#eaf1ff"); }

namespace
{
QString drawing(const QString& name)
{
    // The same 24px / 1.7px line family as the approved local preview.
    static const QHash<QString, QString> drawings {
        {"pen", R"(<path d="m14 4 6 6M4 20l2-6L17 3a2.1 2.1 0 0 1 3 3L9 17l-5 3Zm2-6 3 3M4 21h12"/><path d="m4 20 2-6 3 3Z" fill="%2"/>)"},
        {"brush", R"(<path d="m10 12 8-9a2 2 0 0 1 3 3l-9 8M10 12l3 3"/><path fill="%2" d="M10 12c-4-1-5 4-5 6-1 1-2 1-3 1 6 3 10 0 11-4Z"/>)"},
        {"eraser", R"(<path d="m14 3 7 7-11 11H5L1 17 14 3Zm-9 10 7 7M11 21h11"/>)"},
        {"pointer", R"(<path d="m5 2 14 12-7 1-4 7L5 2Z"/>)"},
        {"hand", R"(<path d="M9 12V5a1.5 1.5 0 0 1 3 0v7-9a1.5 1.5 0 0 1 3 0v9-7a1.5 1.5 0 0 1 3 0v8-4a1.5 1.5 0 0 1 3 0v5c0 5-3 8-7 8-4 0-5-1-7-4l-4-5a1.6 1.6 0 0 1 2-2l4 3"/>)"},
        {"laser", R"(<path d="m3 19 9-9 3 3-9 9-3-3Zm11-11 4-4M19 1v3M23 5h-3M22 2l-3 3M14 10l1 3"/>)"},
        {"text", R"(<path d="M4 4h16M12 4v17M8 21h8M4 4v3M20 4v3"/>)"},
        {"shapes", R"(<path d="m7 3 5 8H2l5-8Z"/><rect x="3" y="14" width="7" height="7" rx="1"/><circle cx="17" cy="17" r="4"/>)"},
        {"home", R"(<path d="m3 10 9-7 9 7M5 9v12h14V9M10 21v-7h4v7"/>)"},
        {"newdoc", R"(<path d="M13 2H5v20h14V8l-6-6Zm0 0v6h6M8 15h8M12 11v8"/>)"},
        {"page", R"(<rect x="3" y="4" width="17" height="14" rx="2"/><path d="M17 14v8M13 18h8"/>)"},
        {"clear", R"(<path d="M4 14V5h16v12M3 21h18m-5-10 5 5-5 5h-5l-4-4 9-6Z"/>)"},
        {"clearall", R"(<path d="M3 15V3h14M7 7h14v13H7m4-9 6 6m0-6-6 6"/>)"},
        {"record", R"(<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4" fill="%1" stroke="none"/>)"},
        {"stop", R"(<circle cx="12" cy="12" r="9"/><rect x="8" y="8" width="8" height="8" rx="1" fill="%1" stroke="none"/>)"},
        {"monitor", R"(<rect x="2" y="3" width="20" height="14" rx="2"/><path d="M12 17v4M8 21h8"/>)"},
        {"folder", R"(<path d="M2 6V4h7l3 3h10v13H2V6Z"/>)"},
        {"settings", R"(<circle cx="12" cy="12" r="3"/><path d="m10 2 4 0 1 3 3 1 3-1 2 4-3 2v3l3 2-2 4-3-1-3 1-1 3h-4l-1-3-3-1-3 1-2-4 3-2v-3L1 9l2-4 3 1 3-1 1-3Z"/>)"},
        {"back", R"(<path d="m14 5-7 7 7 7"/>)"},
        {"next", R"(<path d="m10 5 7 7-7 7"/>)"},
        {"pages", R"(<rect x="7" y="3" width="13" height="17" rx="2"/><path d="M4 6H3v16h13v-1"/>)"},
        {"import", R"(<path d="M12 2v12m-5-5 5 5 5-5M4 13v8h16v-8"/>)"},
        {"export", R"(<path d="M12 16V2m-5 5 5-5 5 5M4 13v8h16v-8"/>)"},
        {"undo", R"(<path d="m9 4-6 6 6 6M3 10h9a8 8 0 0 1 8 8v2"/>)"},
        {"redo", R"(<path d="m15 4 6 6-6 6M21 10h-9a8 8 0 0 0-8 8v2"/>)"},
        {"capture", R"(<path d="M3 8V3h5m8 0h5v5M3 16v5h5m8 0h5v-5"/><rect x="6" y="7" width="12" height="10" rx="2"/><circle cx="12" cy="12" r="2"/>)"},
        {"keyboard", R"(<rect x="2" y="5" width="20" height="14" rx="2"/><path d="M6 9h1m4 0h1m4 0h1M6 12h1m4 0h1m4 0h1M7 16h10"/>)"},
        {"minus", R"(<path d="M5 12h14"/>)"},
        {"plus", R"(<path d="M5 12h14M12 5v14"/>)"},
        {"pause", R"(<path d="M8 5v14M16 5v14" stroke-width="2.5"/>)"},
        {"play", R"(<path d="m7 4 13 8-13 8V4Z"/>)"},
        {"camera", R"(<rect x="2" y="6" width="14" height="12" rx="2"/><path d="m16 10 6-4v12l-6-4"/>)"},
        {"mic", R"(<rect x="9" y="2" width="6" height="13" rx="3"/><path d="M5 10v2a7 7 0 0 0 14 0v-2M12 19v3M8 22h8"/>)"},
        {"volume", R"(<path d="m10 4-5 5H2v6h3l5 5V4Zm4 4a6 6 0 0 1 0 8m4-12a11 11 0 0 1 0 16"/>)"},
        {"grid", R"(<path d="M8 2v20M16 2v20M2 8h20M2 16h20"/>)"},
        {"picture", R"(<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8" cy="8" r="2"/><path d="m3 18 6-6 4 4 4-7 4 6"/>)"},
        {"star", R"(<path d="m12 2 3 6 7 1-5 5 1 8-6-4-6 4 1-8-5-5 7-1 3-6Z"/>)"},
        {"globe", R"(<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18"/>)"},
        {"close", R"(<path d="m6 6 12 12M18 6 6 18"/>)"},
        {"trash", R"(<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7m4-7v7"/>)"},
        {"apps", R"(<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>)"},
        {"palette", R"(<path d="M12 3a9 9 0 1 0 0 18h1a2 2 0 0 0 1-4c-1-1 0-3 2-3h2a3 3 0 0 0 3-3c0-5-4-8-9-8Z"/><circle cx="7" cy="10" r="1"/><circle cx="11" cy="7" r="1"/><circle cx="16" cy="8" r="1"/><circle cx="7" cy="15" r="1"/>)"},
        {"copy", R"(<rect x="8" y="8" width="13" height="13" rx="2"/><path d="M16 8V3H3v13h5"/>)"},
        {"rename", R"(<path d="M12 3H3v18h18v-9M10 14l1-4L19 2l3 3-8 8-4 1Z"/>)"},
        {"save", R"(<path d="M3 3h15l3 3v15H3V3ZM7 3v6h10V3M7 21v-8h10v8"/>)"},
        {"eye", R"(<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12Z"/><circle cx="12" cy="12" r="3"/>)"},
        {"eyeoff", R"(<path d="m3 3 18 18M10 5h2c6 0 10 7 10 7a18 18 0 0 1-4 4M6 6a19 19 0 0 0-4 6s4 7 10 7h2M10 10a3 3 0 0 0 4 4"/>)"},
        {"rotate", R"(<path d="M20 8V3l-4 4M20 8a9 9 0 1 0 1 7"/>)"},
        {"flip", R"(<path d="M12 2v3m0 3v3m0 3v3m0 3v2M3 6h5v12H3V6Zm13 0h5v12h-5V6Z"/>)"},
        {"size", R"(<rect x="4" y="4" width="16" height="16" rx="1"/><path d="m9 15 6-6M10 9h5v5M4 1v2M1 4h2M21 20h2M20 21v2"/>)"},
        {"layers", R"(<path d="m12 3 10 5-10 5L2 8l10-5ZM2 12l10 5 10-5M2 16l10 5 10-5"/>)"},
        {"lock", R"(<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V6a4 4 0 0 1 8 0v4M12 14v3"/>)"},
        {"unlock", R"(<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V6a4 4 0 0 1 8 0M12 14v3"/>)"},
        {"more", R"(<circle cx="4" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="20" cy="12" r="1"/>)"}
        ,{"workbench", R"(<path d="M13 3H3v18h18V11M3 9h10M9 9v12M15 3h6v6m0-6-7 7"/>)"}
    };
    return drawings.value(name);
}
}

QIcon icon(const QString& name, const QColor& accent)
{
    // Cached at UI changes only. No icon generation on the capture or encode
    // path, and both standard and high-DPI pixmaps use identical geometry.
    // Bound live colour-picker variants as well as the fixed icon family.
    static QCache<QString, QIcon> cache(96);
    const QString key = name + (accent.isValid() ? accent.name() : QString());
    if (const QIcon* found = cache.object(key)) return *found;
    const QString body = drawing(name);
    if (body.isEmpty()) return QIcon();

    QIcon result;
    const bool recording = name == "record" || name == "stop";
    for (QIcon::Mode mode : {QIcon::Normal, QIcon::Active, QIcon::Disabled})
    {
        for (QIcon::State state : {QIcon::Off, QIcon::On})
        {
            const QColor stroke = mode == QIcon::Disabled ? QColor("#b8c1cd")
                : recording ? QColor("#dc3545")
                : state == QIcon::On || mode == QIcon::Active ? blue() : muted();
            const QColor tip = mode == QIcon::Disabled ? stroke
                : accent.isValid() ? accent : stroke;
            const QString svg = QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" "
                "viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" "
                "stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\">")
                .arg(stroke.name()) + QString(body).replace("%1", stroke.name()).replace("%2", tip.name())
                + QStringLiteral("</svg>");
            QSvgRenderer renderer(svg.toUtf8());
            for (int size : {24, 32, 48})
            {
                for (int scale : {1, 2})
                {
                    QPixmap pixmap(size * scale, size * scale);
                    pixmap.fill(Qt::transparent);
                    QPainter painter(&pixmap);
                    renderer.render(&painter, QRectF(0, 0, pixmap.width(), pixmap.height()));
                    painter.end();
                    pixmap.setDevicePixelRatio(scale);
                    result.addPixmap(pixmap, mode, state);
                }
            }
        }
    }
    cache.insert(key, new QIcon(result));
    return result;
}

void applyActionIcons(QWidget* root)
{
    static const QHash<QString, QString> actions {
        {"actionEraser", "eraser"}, {"actionSelector", "pointer"},
        {"actionPlay", "hand"}, {"actionPointer", "laser"}, {"actionText", "text"},
        {"actionCustomColor", "palette"}, {"actionBackgrounds", "picture"},
        {"actionPages", "page"}, {"actionNewPage", "page"},
        {"actionBack", "back"}, {"actionForward", "next"},
        {"actionErase", "clear"}, {"actionClearPage", "clear"},
        {"actionPodcast", "record"}, {"actionBoard", "pages"},
        {"actionDesktop", "monitor"}, {"actionDocument", "folder"},
        {"actionMenu", "settings"}, {"actionPreferences", "settings"},
        {"actionTeachingWorkbench", "workbench"},
        {"actionImport", "import"}, {"actionImportPage", "import"},
        {"actionExport", "export"}, {"actionRename", "rename"},
        {"actionNewDocument", "newdoc"}, {"actionNewFolder", "folder"},
        {"actionDuplicate", "copy"}, {"actionDelete", "trash"},
        {"actionUndo", "undo"}, {"actionRedo", "redo"},
        {"actionCapture", "capture"}, {"actionVirtualKeyboard", "keyboard"},
        {"actionAddDocumentToFavorites", "star"}, {"actionCopy", "copy"},
        {"actionSnapToGrid", "grid"}, {"actionDuplicatePage", "copy"},
        {"actionAddItemToCurrentPage", "import"}, {"actionAddItemToNewPage", "page"},
        {"actionAddItemToLibrary", "home"}, {"actionEraseAnnotations", "eraser"},
        {"actionEraseItems", "trash"}, {"actionEraseBackground", "picture"}
    };
    for (QAction* action : root->findChildren<QAction*>())
    {
        const auto match = actions.constFind(action->objectName());
        if (match != actions.constEnd()) action->setIcon(icon(match.value()));
    }
}
}
