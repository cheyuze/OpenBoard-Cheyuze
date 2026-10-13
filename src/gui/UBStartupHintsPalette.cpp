/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */



#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QCheckBox>
#include <QDesktopServices>
#include <QWebEnginePage>
#include <QWebChannel>
#include "UBStartupHintsPalette.h"
#include "core/UBApplication.h"
#include "web/UBWebController.h"

#include "globals/UBGlobals.h"
#include "core/UBSettings.h"
#include "gui/UBMainWindow.h"

namespace
{
const QString seenVersionKey = QStringLiteral("App/LastStartupHintsVersion");

class UBStartupHintsPage : public QWebEnginePage
{
public:
    explicit UBStartupHintsPage(QObject *parent = nullptr)
        : QWebEnginePage(parent)
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        if (url.scheme().compare(QStringLiteral("yach"), Qt::CaseInsensitive) == 0)
        {
            QDesktopServices::openUrl(url);
            return false;
        }

        if (type == NavigationTypeLinkClicked && url.scheme() == QStringLiteral("https"))
        {
            QDesktopServices::openUrl(url);
            return false;
        }

        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
};
}





UBStartupHintsPalette::UBStartupHintsPalette(QWidget *parent) :
    UBFloatingPalette(Qt::BottomRightCorner,parent)
{
    setObjectName("UBStartupHintsPalette");
    setFixedSize(800,600);
    mLayout = new QVBoxLayout();
    mLayout->setContentsMargins(10,28,10,10);
    setLayout(mLayout);
    QString url = UBSettings::settings()->applicationStartupHintsDirectory() + "/index.html";
    mpWebView = new UBWebEngineView(this);
    mpWebView->setPage(new UBStartupHintsPage(mpWebView));
    mpSankoreAPI = new UBWidgetUniboardAPI(0);
    QWebChannel* channel = new QWebChannel(this);
    mpWebView->page()->setWebChannel(channel);
    mpWebView->page()->webChannel()->registerObject("sankore", mpSankoreAPI);
    UBWebController::injectScripts(mpWebView);
    mpWebView->setUrl(QUrl::fromLocalFile(url));
    mpWebView->setAcceptDrops(false);
    mLayout->addWidget(mpWebView);
    mButtonLayout = new QHBoxLayout();
    mLayout->addLayout(mButtonLayout);
    mShowNextTime = new QCheckBox(tr("Visible next time"),this);
    mShowNextTime->setChecked(UBSettings::settings()->appStartupHintsEnabled->get().toBool());
    connect(mShowNextTime,SIGNAL(stateChanged(int)),this,SLOT(onShowNextTimeStateChanged(int)));
    mButtonLayout->addStretch();
    mButtonLayout->addWidget(mShowNextTime);
    hide();
}

UBStartupHintsPalette::~UBStartupHintsPalette()
{
//    DELETEPTR(mButtonLayout);
//    DELETEPTR(mLayout);
}

bool UBStartupHintsPalette::shouldShowAtStartup(bool recurringHintsEnabled,
        const QString& lastSeenVersion, const QString& currentVersion)
{
    return recurringHintsEnabled || (!currentVersion.isEmpty() && lastSeenVersion != currentVersion);
}

void UBStartupHintsPalette::showAtStartup()
{
    auto* settings = UBSettings::settings();
    if (!shouldShowAtStartup(settings->appStartupHintsEnabled->get().toBool(),
            settings->value(seenVersionKey).toString(), QCoreApplication::applicationVersion()))
        return;

    // The board container is hidden when starting in document history. Keep
    // release notes on the shared central surface so they are visible there too.
    if (UBApplication::mainWindow && UBApplication::mainWindow->centralWidget())
        setParent(UBApplication::mainWindow->centralWidget());
    show();
    raise();
}

void UBStartupHintsPalette::paintEvent(QPaintEvent *event)
{
    UBFloatingPalette::paintEvent(event);

    QPainter painter(this);
    painter.drawPixmap(0, 0, QPixmap(":/images/close.svg"));
}


void UBStartupHintsPalette::close()
{
    hide();
}


void UBStartupHintsPalette::mouseReleaseEvent(QMouseEvent * event)
{
    if (event->pos().x() >= 0 && event->pos().x() < QPixmap(":/images/close.svg").width()
        && event->pos().y() >= 0 && event->pos().y() < QPixmap(":/images/close.svg").height())
    {
        event->accept();
        close();
    }

    UBFloatingPalette::mouseReleaseEvent(event);
}

void UBStartupHintsPalette::onShowNextTimeStateChanged(int state)
{
    UBSettings::settings()->appStartupHintsEnabled->setBool(state == Qt::Checked);
}

void UBStartupHintsPalette::showEvent(QShowEvent *event)
{
    UBFloatingPalette::showEvent(event);
    adjustSizeAndPosition();
    if (parentWidget())
        move(qMax(0, (parentWidget()->width() - width()) / 2),
             qMax(0, (parentWidget()->height() - height()) / 5));

    const QString version = QCoreApplication::applicationVersion();
    auto* settings = UBSettings::settings();
    if (!version.isEmpty() && settings->value(seenVersionKey).toString() != version)
    {
        settings->setValue(seenVersionKey, version);
        // Persist immediately, including for users who close the app without
        // another normal startup. Never reset their recurring-hints checkbox.
        settings->save();
    }
}


int UBStartupHintsPalette::border()
{
    return 40;
}
