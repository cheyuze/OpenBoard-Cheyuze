#ifndef UBTEACHINGWORKBENCH_H
#define UBTEACHINGWORKBENCH_H
#include <QAction>
#include <QDesktopServices>
#include <QUrl>

namespace UBTeachingWorkbench
{
inline QUrl url() { return QUrl(QStringLiteral("https://xiwang.cheyuze.top")); }
inline void configure(QAction* action, QObject* context)
{
    action->setData(url());
    QObject::connect(action, &QAction::triggered, context, [] { QDesktopServices::openUrl(url()); });
}
}
#endif
