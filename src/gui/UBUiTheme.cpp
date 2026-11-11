#include "UBUiTheme.h"

#include <QAction>
#include <QCache>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>
#include <QWidget>
#include <QToolBar>
#include <QToolButton>
#include <QMenu>
#include <QWidgetAction>
#include <QPointer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QHBoxLayout>
#include "UBToolbarButtonGroup.h"

namespace UBUiTheme
{
QColor ink() { return QColor("#202936"); }
QColor muted() { return QColor("#526174"); }
QColor blue() { return QColor("#2563eb"); }
QColor border() { return QColor("#dbe2eb"); }
QColor selected() { return QColor("#eaf1ff"); }

namespace
{
// Replace only the toolbar disclosure's expansion path. Forward existing
// actions/menus, and never move a toolbar QWidgetAction into the popup.
class BoardOverflowMenu final : public QObject
{
public:
    BoardOverflowMenu(QToolBar* toolbar,QToolButton* extension)
        : QObject(toolbar),mToolbar(toolbar),mExtension(extension),mMenu(new QMenu(toolbar))
    {
        setObjectName(QStringLiteral("ubBoardOverflowController"));
        mMenu->setObjectName(QStringLiteral("ubBoardOverflowMenu"));
        mMenu->setStyleSheet(QStringLiteral(
            "QMenu{background:white;color:#202936;border:1px solid #dbe2eb;border-radius:9px;padding:6px;"
            "font-family:'Microsoft YaHei UI';font-size:13px;menu-scrollable:1;}"
            "QMenu::item{padding:9px 24px 9px 12px;min-width:136px;border-radius:5px;}"
            "QMenu::item:selected{background:#eaf1ff;color:#1d4ed8;}"
            "QMenu::item:disabled{color:#94a3b8;}"
            "QMenu::separator{height:1px;background:#dbe2eb;margin:5px 6px;}"));
        // QMainWindow's native extension otherwise toggles a second toolbar
        // row. Disconnect only signals to that layout, not the tool actions.
        QObject::disconnect(extension,nullptr,toolbar->layout(),nullptr);
        extension->setCheckable(false);
        extension->setMenu(mMenu);
        extension->setPopupMode(QToolButton::InstantPopup);
        extension->setFocusPolicy(Qt::StrongFocus);
        connect(extension,&QToolButton::clicked,this,[this]{show();});
        connect(mMenu,&QMenu::aboutToShow,this,[this]{if(!mPrepared)populate();mPrepared=false;});
        extension->installEventFilter(this);
        toolbar->installEventFilter(this);
    }
private:
    QString actionLabel(QAction* action) const
    {
        const QString text=action->text();
        const QString full=action->property("ubToolbarFullText").toString();
        return !full.isEmpty()&&(text.endsWith("...")||text.endsWith(QChar(0x2026)))?full:text;
    }
    void addAction(QAction* original)
    {
        auto* row=new QAction(mMenu);
        row->setProperty("ubOverflowSourceAction",QVariant::fromValue(static_cast<QObject*>(original)));
        const auto refresh=[this,original,row]{
            QString text=actionLabel(original);
            if(!original->shortcut().isEmpty())text+=QLatin1Char('\t')+original->shortcut().toString(QKeySequence::NativeText);
            row->setText(text);row->setIcon(original->icon());row->setToolTip(original->toolTip());
            row->setEnabled(original->isEnabled());row->setVisible(original->isVisible());
            row->setCheckable(original->isCheckable());row->setChecked(original->isChecked());
        };
        refresh();connect(original,&QAction::changed,row,refresh);
        connect(original,&QObject::destroyed,row,&QObject::deleteLater);
        // Display shortcut text only: the original QAction continues to own
        // the shortcut, avoiding duplicate/ambiguous window accelerators.
        connect(row,&QAction::triggered,original,[original]{original->trigger();});
        mMenu->addAction(row);
    }
    void addButton(QToolButton* button)
    {
        QString label=button->property("compactColorChoice").toBool()?button->toolTip():button->text();
        if(auto* action=button->defaultAction())if(label.endsWith("...")||label.endsWith(QChar(0x2026)))label=actionLabel(action);
        if(label.isEmpty())label=button->toolTip();
        if(label.isEmpty())return;
        auto* row=new QAction(button->icon(),label,mMenu);
        row->setEnabled(button->isEnabled());row->setToolTip(button->toolTip());
        row->setCheckable(button->isCheckable());row->setChecked(button->isChecked());
        if(button->menu())row->setMenu(button->menu());
        else connect(row,&QAction::triggered,button,[button]{button->click();});
        mMenu->addAction(row);
    }
    void addWidget(QWidget* widget)
    {
        // Compound property widgets retain their real button/action wiring.
        // A second view of a slider forwards to the original; it is never
        // reparented or replaced, so returning to a wide toolbar stays safe.
        for(auto* button:widget->findChildren<QToolButton*>())
            if(!button->isHidden()&&(!button->defaultAction()||button->defaultAction()->isVisible()))addButton(button);
        for(auto* original:widget->findChildren<QSlider*>()){
            if(original->isHidden())continue;
            auto* action=new QWidgetAction(mMenu);auto* row=new QWidget;
            auto* layout=new QHBoxLayout(row);layout->setContentsMargins(12,8,12,8);
            const auto* caption=widget->findChild<QLabel*>(QStringLiteral("ubToolbarGroupCaption"));
            auto* label=new QLabel(caption?caption->text():original->toolTip(),row);
            label->setStyleSheet(QStringLiteral("color:#202936;background:white;border:0;"));
            auto* slider=new QSlider(Qt::Horizontal,row);
            slider->setObjectName(QStringLiteral("ubOverflowSlider"));
            slider->setRange(original->minimum(),original->maximum());
            slider->setSingleStep(original->singleStep());slider->setPageStep(original->pageStep());
            slider->setValue(original->value());slider->setMinimumWidth(100);slider->setStyleSheet(original->styleSheet());
            slider->setEnabled(original->isEnabled());slider->setAccessibleName(label->text());
            connect(slider,&QSlider::valueChanged,original,&QSlider::setValue);
            connect(original,&QSlider::valueChanged,slider,&QSlider::setValue);
            layout->addWidget(label);layout->addWidget(slider);row->setFocusProxy(slider);
            action->setDefaultWidget(row);mMenu->addAction(action);
        }
    }
    void populate()
    {
        mMenu->clear();bool separator=false;
        for(auto* action:mToolbar->actions()){
            if(!action->isVisible())continue;
            if(action->isSeparator()){separator=!mMenu->isEmpty();continue;}
            auto* widget=mToolbar->widgetForAction(action);
            if(!widget||widget->isVisible()||widget->objectName()==QStringLiteral("ubBoardToolbarSpacer"))continue;
            if(separator&&!mMenu->isEmpty()){mMenu->addSeparator();separator=false;}
            if(auto* button=qobject_cast<QToolButton*>(widget)){
                if(button->menu()||qobject_cast<QWidgetAction*>(action))addButton(button);else addAction(action);
            }else addWidget(widget);
        }
        while(!mMenu->actions().isEmpty()&&mMenu->actions().last()->isSeparator()){
            auto* separator=mMenu->actions().last();mMenu->removeAction(separator);delete separator;
        }
    }
    void show()
    {
        if(!mToolbar||!mExtension||!mExtension->isVisible()||mMenu->isVisible())return;
        populate();if(mMenu->isEmpty())return;
        mPrepared=true;mExtension->setFocus(Qt::PopupFocusReason);
        const auto size=mMenu->sizeHint();
        mMenu->popup(mExtension->mapToGlobal(QPoint(mExtension->width()-size.width(),mExtension->height())));
        for(auto* action:mMenu->actions())if(action->isEnabled()&&!action->isSeparator()){mMenu->setActiveAction(action);break;}
    }
    bool eventFilter(QObject* watched,QEvent* event) override
    {
        if(watched==mToolbar&&(event->type()==QEvent::Resize||event->type()==QEvent::Hide))mMenu->hide();
        if(watched!=mExtension)return false;
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::MouseButtonDblClick){
            if(static_cast<QMouseEvent*>(event)->button()!=Qt::LeftButton)return false;
            if(event->type()==QEvent::MouseButtonPress)show();event->accept();return true;
        }
        if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){
            const int key=static_cast<QKeyEvent*>(event)->key();
            if(key==Qt::Key_Space||key==Qt::Key_Return||key==Qt::Key_Enter||key==Qt::Key_Down){
                if(event->type()==QEvent::KeyPress)show();event->accept();return true;
            }
        }
        return false;
    }
    QPointer<QToolBar> mToolbar;
    QPointer<QToolButton> mExtension;
    QMenu* mMenu;
    bool mPrepared=false;
};

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
        {"bold", R"(<path d="M7 3h6a4.5 4.5 0 0 1 0 9H7V3Zm0 9h7a4.5 4.5 0 0 1 0 9H7v-9Z"/>)"},
        {"italic", R"(<path d="M11 3h9M4 21h9M15 3 9 21"/>)"},
        {"underline", R"(<path d="M6 3v8a6 6 0 0 0 12 0V3M4 21h16"/>)"},
        {"textcolor", R"(<path d="m5 17 7-14 7 14M8 12h8"/><path d="M4 21h16" stroke="%2" stroke-width="3"/>)"},
        {"alignleft", R"(<path d="M3 4h18M3 9h12M3 14h18M3 19h12"/>)"},
        {"aligncenter", R"(<path d="M3 4h18M6 9h12M3 14h18M6 19h12"/>)"},
        {"alignright", R"(<path d="M3 4h18M9 9h12M3 14h18M9 19h12"/>)"},
        {"shapes", R"(<path d="m7 3 5 8H2l5-8Z"/><rect x="3" y="14" width="7" height="7" rx="1"/><circle cx="17" cy="17" r="4"/>)"},
        {"fill", R"(<path d="m4 10 7-7 9 9-7 7-9-9Zm0 0h16M8 2l5 5M20 15s-2 3-2 4a2 2 0 0 0 4 0c0-1-2-4-2-4Z"/><path d="M6 12h11l-4 4-7-4Z" fill="%2"/>)"},
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
    if (name == "monitor-record")
        return drawings.value("monitor") + QStringLiteral(
            "<circle cx=\"17\" cy=\"8\" r=\"2.75\" fill=\"%3\" stroke=\"none\"/>");
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
                    .replace("%3", (mode == QIcon::Disabled ? stroke : QColor("#dc3545")).name())
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
        // Static desktop-recording affordance; not a live recording indicator.
        {"actionDesktop", "monitor-record"}, {"actionDocument", "folder"},
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

void normalizeBoardToolbar(QToolBar* toolbar, bool displayText)
{
    // Presentation only. Keep colour swatches and sliders as property controls,
    // but align every command to one icon/text metric and one native hit area.
    QFont font(QStringLiteral("Microsoft YaHei UI")); font.setPixelSize(11);
    toolbar->setFont(font); toolbar->setIconSize(QSize(24,24));
    const QFontMetrics metrics(font);
    if(!toolbar->findChild<QObject*>(QStringLiteral("ubBoardOverflowController"),Qt::FindDirectChildrenOnly))
        if(auto* extension=toolbar->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button")))new BoardOverflowMenu(toolbar,extension);
    const QString style=QStringLiteral(
        "QToolButton{font-family:'Microsoft YaHei UI';font-size:11px;font-weight:400;color:#526174;"
        "background:transparent;border:1px solid transparent;border-radius:9px;padding:3px 4px;margin:0;}"
        "QToolButton:hover{background:#f5f7fa;}"
        "QToolButton:checked,QToolButton:pressed{color:#1d4ed8;background:#eaf1ff;border-color:#9cbbfb;}"
        "QToolButton:disabled{color:#94a3b8;}QToolButton:focus{border-color:#2563eb;}"
        "QToolButton::menu-indicator{image:url(:/images/down_arrow.png);width:8px;height:8px;"
        "subcontrol-origin:padding;subcontrol-position:bottom right;right:1px;bottom:8px;}"
        "QToolButton::menu-button{background:#ffffff;border:0;border-left:1px solid #dbe2eb;width:14px;}"
        "QToolButton::menu-button:hover,QToolButton::menu-button:pressed{background:#eaf1ff;}"
        "QToolButton::menu-arrow{image:url(:/images/down_arrow.png);width:8px;height:8px;}");
    for(auto* button:toolbar->findChildren<QToolButton*>()) {
        if(button->objectName()=="qt_toolbar_ext_button") {
            // Keep the reserved native disclosure extent; its adapter opens
            // a vertical action list instead of growing the toolbar.
            button->setText(QStringLiteral("更\n多"));
            button->setAccessibleName(QStringLiteral("更多工具"));
            button->setToolTip(QStringLiteral("更多工具和设置"));
            button->setIcon(QIcon()); button->setArrowType(Qt::NoArrow);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            // Qt reserves its native 16px extension extent. Respect that
            // allocation so a wider minimum cannot push the label offscreen.
            button->setFont(font);button->setFixedSize(16,displayText?58:42);
            button->setStyleSheet(QStringLiteral(
                "QToolButton{font-family:'Microsoft YaHei UI';font-size:11px;color:#526174;"
                "background:#f5f7fa;border:1px solid #dbe2eb;border-radius:5px;padding:0;margin:0;}"
                "QToolButton:hover,QToolButton:pressed{background:#eaf1ff;color:#1d4ed8;}"
                "QToolButton::menu-indicator{image:none;width:0;}"));
            continue;
        }
        if(button->property("compactColorChoice").toBool())continue;
        bool menuChild=false;
        for(auto* parent=button->parentWidget();parent&&parent!=toolbar;parent=parent->parentWidget())
            if(qobject_cast<QMenu*>(parent)){menuChild=true;break;}
        if(menuChild)continue;
        button->setProperty("boardToolbarCommand",true);
        button->setStyleSheet(style);button->setFont(font);button->setIconSize(QSize(24,24));
        button->setToolButtonStyle(displayText?Qt::ToolButtonTextUnderIcon:Qt::ToolButtonIconOnly);
        const int labelWidth=displayText?qMax(metrics.horizontalAdvance(button->text().remove('&')),
            metrics.horizontalAdvance(button->property("toolbarLabelReserve").toString()))+16:0;
        const int menuWidth=button->menu()?(button->property("labelSplitMenu").toBool()?12:button->popupMode()==QToolButton::MenuButtonPopup?16:8):0;
        button->setFixedSize(qMax(44,labelWidth)+menuWidth,displayText?58:42);
    }
    for(auto* group:toolbar->findChildren<UBToolbarButtonGroup*>())group->setToolbarMetrics(displayText);
}
}
