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




#include "UBResources.h"

#include <QtGui>
#include <QFile>
#include <QSvgRenderer>

#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "frameworks/UBFileSystemUtils.h"
#include "core/memcheck.h"


UBResources* UBResources::sSingleton = 0;

UBResources::UBResources(QObject* pParent)
 : QObject(pParent)
{
    // NOOP
}

UBResources::~UBResources()
{
    // NOOP
}

UBResources* UBResources::resources()
{
    if (!sSingleton)
    {
        sSingleton = new UBResources(UBApplication::staticMemoryCleaner);
        sSingleton->init();
        sSingleton->buildFontList();
    }

    return sSingleton;

}

void UBResources::init()
{
    // Cursors
    // The crosshair is precise but not very intuitive for classroom writing.
    // Use OpenBoard's pencil artwork and place the hotspot at the visible nib.
    // Use the transparent pencil cursor artwork. penOn.png contains the
    // white circular selection background used by the palette icon.
    // The 42x42 artwork's nib is at (7, 35). Qt scales the pixmap and hotspot
    // together on high-DPI displays, so keep these coordinates in image pixels.
    updatePenColor(UBSettings::settings()->currentPenColor());
    updateMarkerColor(UBSettings::settings()->currentMarkerColor());
    eraserCursor    = QCursor(QPixmap(":/images/cursors/eraser.png"), 5, 25);
    captureCursor   = QCursor(QPixmap(":/images/toolbar/cut.png"), 2, 2);
    pointerCursor   = QCursor(QPixmap(":/images/cursors/laser.png"), 2, 1);
    handCursor      = QCursor(Qt::OpenHandCursor);
    zoomInCursor    = QCursor(QPixmap(":/images/cursors/zoomIn.png"), 9, 9);
    zoomOutCursor   = QCursor(QPixmap(":/images/cursors/zoomOut.png"), 9, 9);
    arrowCursor     = QCursor(Qt::ArrowCursor);
    playCursor      = QCursor(QPixmap(":/images/cursors/play.png"), 6, 1);
    textCursor      = QCursor(Qt::ArrowCursor);
    rotateCursor    = QCursor(QPixmap(":/images/cursors/rotate.png"), 16, 16);
    drawLineRulerCursor = QCursor(QPixmap(":/images/cursors/drawRulerLine.png"), 3, 12);
}

QPixmap UBResources::renderColoredPen(const QString &resourcePath,
        const QColor &color) const
{
    QFile source(resourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return QPixmap(resourcePath);

    QByteArray svg = source.readAll();
    QColor body = color;
    if (!body.isValid())
        body = QColor("#EF4444");

    // Keep very light and very dark colours legible while preserving the
    // original SVG's metallic holder, wood and highlight details.
    QColor shadow = body.darker(260);
    if (body.lightness() > 235)
        shadow = QColor("#64748B");
    else if (body.lightness() < 25)
    {
        body = QColor("#334155");
        shadow = QColor("#020617");
    }

    svg.replace("#ff0000", body.name(QColor::HexRgb).toUtf8());
    svg.replace("#2b0000", shadow.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svg);
    QPixmap pixmap(42, 42);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, 42, 42));
    return pixmap;
}

void UBResources::updatePenColor(const QColor &color)
{
    mPenColor = color.isValid() ? color : QColor("#EF4444");
    penCursor = QCursor(renderColoredPen(
            QStringLiteral(":/images/stylusPalette/pen.svg"), mPenColor), 7, 35);
}

QIcon UBResources::coloredPenIcon(bool desktopArrow) const
{
    const QString offPath = desktopArrow
            ? QStringLiteral(":/images/stylusPalette/penArrow.svg")
            : QStringLiteral(":/images/stylusPalette/pen.svg");
    const QString onPath = desktopArrow
            ? QStringLiteral(":/images/stylusPalette/penOnArrow.svg")
            : QStringLiteral(":/images/stylusPalette/penOn.svg");

    QIcon icon;
    icon.addPixmap(renderColoredPen(offPath, mPenColor),
            QIcon::Normal, QIcon::Off);
    icon.addPixmap(renderColoredPen(onPath, mPenColor),
            QIcon::Normal, QIcon::On);
    return icon;
}

QPixmap UBResources::renderColoredMarker(const QString &resourcePath,
        const QColor &color) const
{
    QFile source(resourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return QPixmap(resourcePath);

    QByteArray svg = source.readAll();
    QColor body = color.isValid() ? color : QColor("#F9E100");
    body.setAlpha(255);

    const QColor light = body.lighter(135);
    const QColor highlight = body.lighter(115);
    const QColor mid = body.darker(110);
    const QColor dark = body.darker(165);
    const QColor deepest = body.darker(260);

    svg.replace("#f9e100", body.name(QColor::HexRgb).toUtf8());
    svg.replace("#ffe052", light.name(QColor::HexRgb).toUtf8());
    svg.replace("#ffc748", highlight.name(QColor::HexRgb).toUtf8());
    svg.replace("#ffb800", mid.name(QColor::HexRgb).toUtf8());
    svg.replace("#ff842e", dark.name(QColor::HexRgb).toUtf8());
    svg.replace("#d45500", dark.name(QColor::HexRgb).toUtf8());
    svg.replace("#2f1700", deepest.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svg);
    QPixmap pixmap(42, 42);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, 42, 42));
    return pixmap;
}

void UBResources::updateMarkerColor(const QColor &color)
{
    mMarkerColor = color.isValid() ? color : QColor("#F9E100");
    // Keep the canvas cursor consistent with the toolbar: the coloured brush
    // tip is the actual drawing hot spot.
    markerCursor = QCursor(renderMarkerBrushIcon(false, false), 7, 38);
}

QIcon UBResources::coloredMarkerIcon(bool desktopArrow) const
{
    QIcon icon;
    icon.addPixmap(renderMarkerBrushIcon(false, desktopArrow),
            QIcon::Normal, QIcon::Off);
    icon.addPixmap(renderMarkerBrushIcon(true, desktopArrow),
            QIcon::Normal, QIcon::On);
    return icon;
}

QPixmap UBResources::renderMarkerBrushIcon(bool selected, bool desktopArrow) const
{
    QPixmap pixmap(42, 42);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (selected)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(219, 234, 254, 185));
        painter.drawEllipse(QRectF(1.5, 1.5, 39, 39));
    }

    QColor brushColor = mMarkerColor.isValid()
            ? mMarkerColor : QColor(QStringLiteral("#F9E100"));
    brushColor.setAlpha(255);

    // Draw a deliberately broad paint brush so the highlighter remains easy
    // to distinguish from the pencil in both checked and unchecked states.
    painter.setPen(QPen(QColor(QStringLiteral("#713F12")), 5.8,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(34, 7), QPointF(23, 19));
    painter.setPen(QPen(QColor(QStringLiteral("#64748B")), 1.2));
    painter.setBrush(QColor(QStringLiteral("#E2E8F0")));
    painter.drawPolygon(QPolygonF() << QPointF(18, 16) << QPointF(27, 24)
            << QPointF(21, 30) << QPointF(12, 22));

    QPainterPath bristles;
    bristles.moveTo(12, 21);
    bristles.lineTo(22, 30);
    bristles.lineTo(14, 38);
    bristles.cubicTo(11, 40, 8, 38, 5, 39);
    bristles.cubicTo(7, 35, 5, 32, 7, 29);
    bristles.closeSubpath();
    painter.setPen(QPen(brushColor.darker(145), 1.2,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(brushColor);
    painter.drawPath(bristles);

    painter.setPen(QPen(brushColor.lighter(145), 1.5,
            Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(11, 27), QPointF(8, 35));

    if (desktopArrow)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#0F172A")));
        painter.drawPolygon(QPolygonF() << QPointF(27, 28) << QPointF(38, 33)
                << QPointF(33, 35) << QPointF(31, 40));
    }

    return pixmap;
}

void UBResources::buildFontList()
{
    QString customFontDirectory = UBSettings::settings()->applicationCustomFontDirectory();
    QStringList fontFiles = UBFileSystemUtils::allFiles(customFontDirectory);
    foreach(QString fontFile, fontFiles){
        int fontId = QFontDatabase::addApplicationFont(fontFile);
        mCustomFontList << QFontDatabase::applicationFontFamilies(fontId);
    }

    mCustomFontList.removeDuplicates();
}
