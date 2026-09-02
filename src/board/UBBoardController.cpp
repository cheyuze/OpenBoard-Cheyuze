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




#include "UBBoardController.h"

#include <QtWidgets>

#include <functional>

#include "adaptors/UBMetadataDcSubsetAdaptor.h"
#include "adaptors/UBSvgSubsetAdaptor.h"

#include "board/UBBoardPaletteManager.h"
#include "board/UBBoardView.h"
#include "board/UBDrawingController.h"
#include "board/UBFeaturesController.h"

#include "frameworks/UBFileSystemUtils.h"
#include "frameworks/UBPlatformUtils.h"

#include "core/UBApplication.h"
#include "core/UBApplicationController.h"
#include "core/UBDisplayManager.h"
#include "core/UBDocumentManager.h"
#include "core/UBDownloadManager.h"
#include "core/UBMimeData.h"
#include "core/UBPersistenceManager.h"
#include "core/UBSetting.h"
#include "core/UBSettings.h"
#include "core/UBSettings.h"

#include "document/UBDocument.h"
#include "document/UBDocumentController.h"
#include "document/UBDocumentProxy.h"

#include "domain/UBGraphicsGroupContainerItem.h"
#include "domain/UBGraphicsItemUndoCommand.h"
#include "domain/UBGraphicsMediaItem.h"
#include "domain/UBGraphicsPDFItem.h"
#include "domain/UBGraphicsPixmapItem.h"
#include "domain/UBGraphicsSvgItem.h"
#include "domain/UBGraphicsTextItem.h"
#include "domain/UBGraphicsWidgetItem.h"
#include "domain/UBItem.h"
#include "domain/UBPageSizeUndoCommand.h"

#include "gui/UBFeaturesWidget.h"
#include "gui/UBKeyboardPalette.h"
#include "gui/UBMagnifer.h"
#include "gui/UBMainWindow.h"
#include "gui/UBMessageWindow.h"
#include "gui/UBResources.h"
#include "gui/UBThumbnailScene.h"
#include "gui/UBToolWidget.h"
#include "gui/UBToolbarButtonGroup.h"

#include "podcast/UBPodcastController.h"

#include "tools/UBToolsManager.h"

#include "web/UBEmbedController.h"
#include "web/UBEmbedParser.h"

#include "core/memcheck.h"

namespace
{
    class ZoomPercentageLabel final : public QLabel
    {
    public:
        explicit ZoomPercentageLabel(QWidget* parent = nullptr)
            : QLabel(parent)
        {
            mSingleClickTimer.setSingleShot(true);
            connect(&mSingleClickTimer, &QTimer::timeout, this, [this]() {
                if (mSingleClickHandler)
                    mSingleClickHandler();
            });
        }

        void setSingleClickHandler(std::function<void()> handler)
        {
            mSingleClickHandler = std::move(handler);
        }

        void setDoubleClickHandler(std::function<void()> handler)
        {
            mDoubleClickHandler = std::move(handler);
        }

    protected:
        void mouseReleaseEvent(QMouseEvent* event) override
        {
            if (event->button() == Qt::LeftButton)
            {
                if (mIgnoreNextRelease)
                {
                    mIgnoreNextRelease = false;
                    event->accept();
                    return;
                }

                mSingleClickTimer.start(QApplication::doubleClickInterval());
                event->accept();
                return;
            }
            QLabel::mouseReleaseEvent(event);
        }

        void mouseDoubleClickEvent(QMouseEvent* event) override
        {
            if (event->button() == Qt::LeftButton)
            {
                mSingleClickTimer.stop();
                mIgnoreNextRelease = true;
                if (mDoubleClickHandler)
                    mDoubleClickHandler();
                event->accept();
                return;
            }
            QLabel::mouseDoubleClickEvent(event);
        }

    private:
        QTimer mSingleClickTimer;
        std::function<void()> mSingleClickHandler;
        std::function<void()> mDoubleClickHandler;
        bool mIgnoreNextRelease = false;
    };


    QPixmap historyArrowPixmap(bool redo, const QColor& color)
    {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 2.8, Qt::SolidLine,
                Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);

        QPainterPath path;
        path.moveTo(26, 25);
        path.cubicTo(25, 15, 17, 9, 8, 13);
        path.moveTo(8, 13);
        path.lineTo(14, 6);
        path.moveTo(8, 13);
        path.lineTo(15, 18);

        if (redo)
        {
            QTransform mirror;
            mirror.translate(32, 0);
            mirror.scale(-1, 1);
            path = mirror.map(path);
        }

        painter.drawPath(path);
        return pixmap;
    }


    QIcon historyArrowIcon(bool redo)
    {
        QIcon icon;
        icon.addPixmap(historyArrowPixmap(redo,
                QColor(42, 78, 125, 220)), QIcon::Normal);
        icon.addPixmap(historyArrowPixmap(redo,
                QColor(91, 106, 126, 75)), QIcon::Disabled);
        return icon;
    }


    QPixmap captureQuickPixmap(const QColor& color)
    {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 2.3, Qt::SolidLine,
                Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);

        painter.drawLine(QPointF(5, 12), QPointF(5, 6));
        painter.drawLine(QPointF(5, 6), QPointF(11, 6));
        painter.drawLine(QPointF(21, 6), QPointF(27, 6));
        painter.drawLine(QPointF(27, 6), QPointF(27, 12));
        painter.drawLine(QPointF(27, 20), QPointF(27, 26));
        painter.drawLine(QPointF(27, 26), QPointF(21, 26));
        painter.drawLine(QPointF(11, 26), QPointF(5, 26));
        painter.drawLine(QPointF(5, 26), QPointF(5, 20));
        painter.drawRoundedRect(QRectF(10, 11, 12, 10), 2, 2);
        painter.drawEllipse(QPointF(16, 16), 2.6, 2.6);
        return pixmap;
    }


    QPixmap keyboardQuickPixmap(const QColor& color)
    {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 2.0, Qt::SolidLine,
                Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(4.5, 7.5, 23, 17), 3, 3);

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for (int row = 0; row < 2; ++row)
        {
            for (int column = 0; column < 5; ++column)
                painter.drawRoundedRect(QRectF(7 + column * 4, 11 + row * 4,
                        2.5, 2.3), 0.7, 0.7);
        }
        painter.drawRoundedRect(QRectF(10, 19, 12, 2.3), 0.8, 0.8);
        return pixmap;
    }


    QIcon quickToolIcon(bool keyboard)
    {
        QIcon icon;
        const auto pixmap = [keyboard](const QColor& color) {
            return keyboard ? keyboardQuickPixmap(color)
                    : captureQuickPixmap(color);
        };
        icon.addPixmap(pixmap(QColor(42, 78, 125, 220)),
                QIcon::Normal, QIcon::Off);
        icon.addPixmap(pixmap(QColor(37, 99, 235, 235)),
                QIcon::Normal, QIcon::On);
        icon.addPixmap(pixmap(QColor(91, 106, 126, 75)),
                QIcon::Disabled, QIcon::Off);
        return icon;
    }


    QIcon lineOptionIcon(int option)
    {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(QStringLiteral("#1E293B")), 2.0,
                option == 4 ? Qt::DashLine : Qt::SolidLine,
                Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        switch (option)
        {
        case 1:
            painter.drawRoundedRect(QRectF(4.5, 4.5, 15, 15), 1.5, 1.5);
            break;
        case 2:
            painter.drawEllipse(QRectF(4.5, 4.5, 15, 15));
            break;
        case 5:
            painter.drawPolygon(QPolygonF()
                    << QPointF(12, 4) << QPointF(20, 20) << QPointF(4, 20));
            break;
        case 6:
            painter.drawLine(QPointF(4, 12), QPointF(20, 12));
            painter.drawLine(QPointF(20, 12), QPointF(14, 7));
            painter.drawLine(QPointF(20, 12), QPointF(14, 17));
            break;
        case 7:
            painter.drawLine(QPointF(4, 16), QPointF(20, 16));
            painter.drawLine(QPointF(12, 21), QPointF(12, 4));
            painter.drawLine(QPointF(20, 16), QPointF(16, 13));
            painter.drawLine(QPointF(20, 16), QPointF(16, 19));
            painter.drawLine(QPointF(12, 4), QPointF(9, 8));
            painter.drawLine(QPointF(12, 4), QPointF(15, 8));
            break;
        case 8:
            painter.drawPolygon(QPolygonF() << QPointF(4, 17) << QPointF(8, 5)
                    << QPointF(17, 7) << QPointF(20, 18) << QPointF(10, 20));
            break;
        case 9:
        {
            QPainterPath path;
            path.moveTo(QPointF(4, 5));
            path.quadTo(QPointF(12, 22), QPointF(20, 5));
            painter.drawPath(path);
            break;
        }
        case 10:
        {
            QPainterPath first;
            first.moveTo(QPointF(4, 5));
            first.cubicTo(QPointF(8, 5), QPointF(9, 8), QPointF(10, 11));
            QPainterPath second;
            second.moveTo(QPointF(14, 13));
            second.cubicTo(QPointF(15, 16), QPointF(16, 19), QPointF(20, 19));
            painter.drawPath(first);
            painter.drawPath(second);
            break;
        }
        case 11:
        {
            QPainterPath path;
            path.moveTo(QPointF(3, 12));
            path.cubicTo(QPointF(6, 3), QPointF(9, 3), QPointF(12, 12));
            path.cubicTo(QPointF(15, 21), QPointF(18, 21), QPointF(21, 12));
            painter.drawPath(path);
            break;
        }
        case 12:
            painter.drawLine(QPointF(3, 12), QPointF(21, 12));
            painter.drawLine(QPointF(3, 12), QPointF(7, 9));
            painter.drawLine(QPointF(3, 12), QPointF(7, 15));
            painter.drawLine(QPointF(21, 12), QPointF(17, 9));
            painter.drawLine(QPointF(21, 12), QPointF(17, 15));
            for (int x = 8; x <= 16; x += 4)
                painter.drawLine(QPointF(x, 9), QPointF(x, 15));
            break;
        case 13:
            painter.drawPolygon(QPolygonF() << QPointF(7, 5) << QPointF(21, 5)
                    << QPointF(17, 19) << QPointF(3, 19));
            break;
        case 14:
            painter.drawPolygon(QPolygonF() << QPointF(8, 5) << QPointF(16, 5)
                    << QPointF(21, 19) << QPointF(3, 19));
            break;
        case 15:
            painter.drawPolygon(QPolygonF() << QPointF(12, 3) << QPointF(21, 10)
                    << QPointF(17, 21) << QPointF(7, 21) << QPointF(3, 10));
            break;
        case 16:
            painter.drawPolygon(QPolygonF() << QPointF(7, 3) << QPointF(17, 3)
                    << QPointF(22, 12) << QPointF(17, 21) << QPointF(7, 21)
                    << QPointF(2, 12));
            break;
        case 17:
        case 18:
            painter.drawRect(QRectF(4, 8, 12, 12));
            painter.drawRect(QRectF(8, 4, 12, 12));
            painter.drawLine(QPointF(4, 8), QPointF(8, 4));
            painter.drawLine(QPointF(16, 8), QPointF(20, 4));
            painter.drawLine(QPointF(16, 20), QPointF(20, 16));
            break;
        case 19:
            painter.drawEllipse(QRectF(4, 3, 16, 6));
            painter.drawEllipse(QRectF(4, 15, 16, 6));
            painter.drawLine(QPointF(4, 6), QPointF(4, 18));
            painter.drawLine(QPointF(20, 6), QPointF(20, 18));
            break;
        case 20:
            painter.drawLine(QPointF(12, 3), QPointF(4, 18));
            painter.drawLine(QPointF(12, 3), QPointF(20, 18));
            painter.drawEllipse(QRectF(4, 15, 16, 6));
            break;
        case 21:
            painter.drawEllipse(QRectF(3, 3, 18, 18));
            painter.drawEllipse(QRectF(3, 9, 18, 6));
            break;
        case 22:
            painter.drawLine(QPointF(12, 3), QPointF(3, 17));
            painter.drawLine(QPointF(12, 3), QPointF(12, 21));
            painter.drawLine(QPointF(12, 3), QPointF(21, 17));
            painter.drawPolygon(QPolygonF() << QPointF(3, 17) << QPointF(12, 13)
                    << QPointF(21, 17) << QPointF(12, 21));
            break;
        case 23:
            painter.drawPolygon(QPolygonF() << QPointF(3, 19) << QPointF(8, 7)
                    << QPointF(13, 19));
            painter.drawPolygon(QPolygonF() << QPointF(11, 15) << QPointF(16, 3)
                    << QPointF(21, 15));
            painter.drawLine(QPointF(3, 19), QPointF(11, 15));
            painter.drawLine(QPointF(8, 7), QPointF(16, 3));
            painter.drawLine(QPointF(13, 19), QPointF(21, 15));
            break;
        default:
            painter.drawLine(QPointF(4, 12), QPointF(20, 12));
            break;
        }

        return QIcon(pixmap);
    }

    QPixmap shapeToolPixmap(const QColor& color)
    {
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(5, 7, 17, 14), 2, 2);
        painter.drawEllipse(QRectF(27, 6, 15, 15));
        painter.drawPolygon(QPolygonF() << QPointF(13, 27) << QPointF(22, 42)
                << QPointF(4, 42));
        painter.drawLine(QPointF(27, 35), QPointF(43, 35));
        painter.drawLine(QPointF(43, 35), QPointF(38, 30));
        painter.drawLine(QPointF(43, 35), QPointF(38, 40));
        return pixmap;
    }

    QIcon shapeToolIcon()
    {
        QIcon icon;
        icon.addPixmap(shapeToolPixmap(QColor(QStringLiteral("#334155"))),
                QIcon::Normal, QIcon::Off);
        icon.addPixmap(shapeToolPixmap(QColor(QStringLiteral("#2563EB"))),
                QIcon::Normal, QIcon::On);
        return icon;
    }

    QIcon fillOptionIcon(bool enabled, const QColor& color, int opacity)
    {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor visibleColor = color;
        visibleColor.setAlpha(qRound(255.0 * qBound(0, opacity, 100) / 100.0));
        painter.setPen(QPen(QColor(QStringLiteral("#64748B")), 1.6));
        painter.setBrush(enabled ? QBrush(visibleColor) : Qt::NoBrush);
        painter.drawRoundedRect(QRectF(4, 4, 16, 16), 3, 3);
        if (!enabled)
        {
            painter.setPen(QPen(QColor(QStringLiteral("#EF4444")), 1.8));
            painter.drawLine(QPointF(5, 19), QPointF(19, 5));
        }
        return QIcon(pixmap);
    }

    std::shared_ptr<UBDocumentProxy> findDocumentByUuid(
            UBDocumentTreeNode *node, const QUuid &uuid)
    {
        if (!node || uuid.isNull())
            return nullptr;

        foreach (UBDocumentTreeNode *child, node->children())
        {
            if (child->nodeType() == UBDocumentTreeNode::Document)
            {
                const std::shared_ptr<UBDocumentProxy> proxy = child->proxyData();
                if (proxy && !proxy->isBroken() && proxy->pageCount() > 0
                        && proxy->uuid() == uuid)
                {
                    return proxy;
                }
            }
            else
            {
                const std::shared_ptr<UBDocumentProxy> result =
                        findDocumentByUuid(child, uuid);
                if (result)
                    return result;
            }
        }

        return nullptr;
    }

    void findMostRecentlyUpdatedDocument(
            UBDocumentTreeNode *node,
            std::shared_ptr<UBDocumentProxy> &mostRecent,
            QDateTime &mostRecentUpdate)
    {
        if (!node)
            return;

        foreach (UBDocumentTreeNode *child, node->children())
        {
            if (child->nodeType() == UBDocumentTreeNode::Document)
            {
                const std::shared_ptr<UBDocumentProxy> proxy = child->proxyData();
                if (!proxy || proxy->isBroken() || proxy->pageCount() <= 0)
                    continue;

                const QDateTime updatedAt = proxy->lastUpdate();
                if (!mostRecent || updatedAt > mostRecentUpdate)
                {
                    mostRecent = proxy;
                    mostRecentUpdate = updatedAt;
                }
            }
            else
            {
                findMostRecentlyUpdatedDocument(child, mostRecent, mostRecentUpdate);
            }
        }
    }
}

UBBoardController::UBBoardController(UBMainWindow* mainWindow)
    : UBDocumentContainer(mainWindow->centralWidget())
    , mMainWindow(mainWindow)
    , mActiveScene(0)
    , mActiveSceneIndex(-1)
    , mPaletteManager(0)
    , mSoftwareUpdateDialog(0)
    , mMessageWindow(0)
    , mEmbedController(nullptr)
    , mControlView(0)
    , mDisplayView(0)
    , mControlContainer(0)
    , mControlLayout(0)
    , mZoomControl(0)
    , mZoomSlider(0)
    , mZoomLabel(0)
    , mZoomOutButton(0)
    , mZoomInButton(0)
    , mUndoRedoControl(0)
    , mUndoButton(0)
    , mRedoButton(0)
    , mCaptureButton(0)
    , mVirtualKeyboardButton(0)
    , mZoomFactor(1.0)
    , mIsClosing(false)
    , mSystemScaleFactor(1.0)
    , mCleanupDone(false)
    , mCacheWidgetIsEnabled(false)
    , mDeletingSceneIndex(-1)
    , mMovingSceneIndex(-1)
    , mActionGroupText(tr("Group"))
    , mActionUngroupText(tr("Ungroup"))
    , mAutosaveTimer(0)
{
    mZoomFactor = UBSettings::settings()->boardZoomFactor->get().toDouble();

    int penColorIndex = UBSettings::settings()->penColorIndex();
    int markerColorIndex = UBSettings::settings()->markerColorIndex();

    mPenColorOnDarkBackground = UBSettings::settings()->penColors(true).at(penColorIndex);
    mPenColorOnLightBackground = UBSettings::settings()->penColors(false).at(penColorIndex);
    mMarkerColorOnDarkBackground = UBSettings::settings()->markerColors(true).at(markerColorIndex);
    mMarkerColorOnLightBackground = UBSettings::settings()->markerColors(false).at(markerColorIndex);
}


void UBBoardController::init()
{
    setupViews();
    setupToolbar();

    connect(UBApplication::undoStack, SIGNAL(canUndoChanged(bool))
            , this, SLOT(undoRedoStateChange(bool)));

    connect(UBApplication::undoStack, SIGNAL(canRedoChanged(bool))
            , this, SLOT(undoRedoStateChange(bool)));

    connect(UBDrawingController::drawingController(), SIGNAL(stylusToolChanged(int))
            , this, SLOT(setToolCursor(int)));

    connect(UBDrawingController::drawingController(), SIGNAL(stylusToolChanged(int))
            , this, SLOT(stylusToolChanged(int)));

    connect(UBApplication::app(), SIGNAL(lastWindowClosed())
            , this, SLOT(lastWindowClosed()));

    connect(UBDownloadManager::downloadManager(), SIGNAL(downloadModalFinished()), this, SLOT(onDownloadModalFinished()));
    connect(UBDownloadManager::downloadManager(), SIGNAL(addDownloadedFileToBoard(bool,QUrl,QUrl,QString,QByteArray,QPointF,QSize,bool)), this, SLOT(downloadFinished(bool,QUrl,QUrl,QString,QByteArray,QPointF,QSize,bool)));

    auto persistenceManager{UBPersistenceManager::persistenceManager()};
    connect(persistenceManager, &UBPersistenceManager::documentSceneDuplicated, this, &UBBoardController::documentSceneDuplicated);
    connect(persistenceManager, &UBPersistenceManager::documentSceneMoved, this, &UBBoardController::documentSceneMoved);
    connect(persistenceManager, &UBPersistenceManager::documentSceneDeleted, this, &UBBoardController::documentSceneDeleted);

    UBDocumentTreeModel *documentModel = persistenceManager->mDocumentTreeStructureModel;
    UBDocumentTreeNode *myDocumentsNode = documentModel
            ? documentModel->nodeFromIndex(documentModel->myDocumentsIndex()) : nullptr;

    std::shared_ptr<UBDocumentProxy> doc;
    const bool createNewAtStartup = UBSettings::settings()
            ->appStartupBehavior->get().toInt() == UBSettings::CreateNewDocument;

    if (!createNewAtStartup)
    {
        const QString lastDocumentUuid = UBSettings::settings()
                ->appLastSessionDocumentUUID->get().toString().trimmed();
        if (!lastDocumentUuid.isEmpty())
            doc = findDocumentByUuid(myDocumentsNode, QUuid(lastDocumentUuid));

        // Existing installations do not yet have LastSessionDocumentUUID. In
        // that case open the most recently edited board instead of creating an
        // empty document on the first launch after upgrading.
        if (!doc)
        {
            QDateTime mostRecentUpdate;
            findMostRecentlyUpdatedDocument(myDocumentsNode, doc, mostRecentUpdate);
        }
    }

    if (doc)
    {
        if (!setActiveDocumentScene(doc, doc->lastVisitedSceneIndex()))
            doc.reset();
    }

    if (!doc)
    {
        doc = persistenceManager->createNewDocument();
        if (doc)
            mInitialDocumentScene = setActiveDocumentScene(doc);
    }

    connect(UBApplication::displayManager, &UBDisplayManager::screenRolesAssigned, this, [this](){
        initBackgroundGridSize();
    });

    undoRedoStateChange(true);
}


UBBoardController::~UBBoardController()
{
    delete mDisplayView;
}

/**
 * @brief Set the default background grid size to appear as roughly 1cm on screen
 */
void UBBoardController::initBackgroundGridSize()
{
    // Besides adjusting for DPI, we also need to scale the grid size by the ratio of the control view size
    // to document size. Here we approximate this ratio as (document resolution) / (screen resolution).
    // Later on, this is calculated by `updateSystemScaleFactor` and stored in `mSystemScaleFactor`.

    qreal dpi = UBApplication::displayManager->logicalDpi(ScreenRole::Control);

    //qDebug() << "dpi: " << dpi;

    qreal screenY = UBApplication::displayManager->screenSize(ScreenRole::Control).height();
    qreal documentY = mActiveScene->nominalSize().height();
    qreal resolutionRatio = documentY / screenY;

    //qDebug() << "resolution ratio: " << resolutionRatio;

    int gridSize = (resolutionRatio * 10. * dpi) / UBGeometryUtils::inchSize;

    UBSettings::settings()->crossSize = gridSize;
    UBSettings::settings()->defaultCrossSize = gridSize;
    mActiveScene->setBackgroundGridSize(gridSize);

    //qDebug() << "grid size: " << gridSize;
}

int UBBoardController::currentPage() const
{
    return mActiveSceneIndex + 1;
}

void UBBoardController::setupViews()
{
    mControlContainer = new QWidget(mMainWindow->centralWidget());

    mControlLayout = new QHBoxLayout(mControlContainer);
    mControlLayout->setContentsMargins(0, 0, 0, 0);

    mControlView = new UBBoardView(this, mControlContainer, true, false);
    mControlView->setObjectName(CONTROLVIEW_OBJ_NAME);
    mControlView->setInteractive(true);
    mControlView->setMouseTracking(true);

    mControlView->grabGesture(Qt::SwipeGesture);

    mControlView->setTransformationAnchor(QGraphicsView::NoAnchor);

    mControlLayout->addWidget(mControlView);
    mControlContainer->setObjectName("ubBoardControlContainer");
    mMainWindow->addBoardWidget(mControlContainer);

    setupZoomControl();
    setupUndoRedoControl();

    connect(mControlView, SIGNAL(resized(QResizeEvent*)), this, SLOT(boardViewResized(QResizeEvent*)));

    // TODO UB 4.x Optimization do we have to create the display view even if their is
    // only 1 screen
    //
    mDisplayView = new UBBoardView(this, UBItemLayerType::FixedBackground, UBItemLayerType::Tool, 0);
    mDisplayView->setInteractive(false);
    mDisplayView->setTransformationAnchor(QGraphicsView::NoAnchor);

    mPaletteManager = new UBBoardPaletteManager(mControlContainer, this);

    mMessageWindow = new UBMessageWindow(mControlContainer);
    mMessageWindow->hide();

    connect(this, SIGNAL(activeSceneChanged()), mPaletteManager, SLOT(activeSceneChanged()));
}


void UBBoardController::setupZoomControl()
{
    mZoomControl = new QFrame(mControlContainer);
    mZoomControl->setObjectName(QStringLiteral("ubBoardZoomControl"));
    mZoomControl->setAttribute(Qt::WA_StyledBackground, true);
    mZoomControl->setFixedHeight(42);

    QHBoxLayout* layout = new QHBoxLayout(mZoomControl);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);

    auto createButton = [this](const QString& objectName, const QString& text,
            const QString& tooltip) {
        QToolButton* button = new QToolButton(mZoomControl);
        button->setObjectName(objectName);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(28, 28);
        return button;
    };

    mZoomOutButton = createButton(QStringLiteral("ubBoardZoomOutButton"),
            QString::fromUtf8("\xE2\x88\x92"), QStringLiteral("缩小画布"));
    mZoomInButton = createButton(QStringLiteral("ubBoardZoomInButton"),
            QStringLiteral("+"), QStringLiteral("放大画布"));

    mZoomSlider = new QSlider(Qt::Horizontal, mZoomControl);
    mZoomSlider->setObjectName(QStringLiteral("ubBoardZoomSlider"));
    // OpenBoard has no fixed lower zoom constant: the native wheel zoom can
    // continue below 25%.  Use 1% as the smallest meaningful value the
    // integer percentage control can represent, while keeping OpenBoard's
    // native UB_MAX_ZOOM upper bound.
    mZoomSlider->setRange(1, UB_MAX_ZOOM * 100);
    mZoomSlider->setSingleStep(5);
    mZoomSlider->setPageStep(25);
    mZoomSlider->setValue(100);
    mZoomSlider->setFixedWidth(150);
    mZoomSlider->setCursor(Qt::PointingHandCursor);
    mZoomSlider->setToolTip(QStringLiteral("拖动调整画布缩放比例"));

    ZoomPercentageLabel* zoomLabel = new ZoomPercentageLabel(mZoomControl);
    mZoomLabel = zoomLabel;
    mZoomLabel->setText(QStringLiteral("100%"));
    mZoomLabel->setObjectName(QStringLiteral("ubBoardZoomLabel"));
    mZoomLabel->setAlignment(Qt::AlignCenter);
    mZoomLabel->setFixedWidth(48);
    mZoomLabel->setCursor(Qt::PointingHandCursor);
    mZoomLabel->setToolTip(QStringLiteral("单击选择缩放比例，双击恢复 100%"));

    QMenu* zoomPresetMenu = new QMenu(mZoomLabel);
    zoomPresetMenu->setObjectName(QStringLiteral("ubZoomPresetMenu"));
    QActionGroup* zoomPresetGroup = new QActionGroup(zoomPresetMenu);
    zoomPresetGroup->setExclusive(true);
    const QList<int> zoomPresets { 10, 30, 50, 100, 150, 200, 300, 500 };
    for (int percentage : zoomPresets)
    {
        QAction* action = zoomPresetMenu->addAction(
                QStringLiteral("%1%").arg(percentage));
        action->setCheckable(true);
        action->setData(percentage);
        zoomPresetGroup->addAction(action);
    }
    connect(zoomPresetGroup, &QActionGroup::triggered, mZoomControl,
            [this](QAction* action) {
        mZoomSlider->setValue(action->data().toInt());
    });
    connect(zoomPresetMenu, &QMenu::aboutToShow, mZoomControl,
            [this, zoomPresetGroup]() {
        for (QAction* action : zoomPresetGroup->actions())
            action->setChecked(action->data().toInt() == mZoomSlider->value());
    });
    zoomLabel->setSingleClickHandler([this, zoomPresetMenu]() {
        const QSize menuSize = zoomPresetMenu->sizeHint();
        const QPoint labelTopLeft = mZoomLabel->mapToGlobal(QPoint(0, 0));
        const int x = labelTopLeft.x() + mZoomLabel->width() - menuSize.width();
        const int y = labelTopLeft.y() - menuSize.height() - 4;
        zoomPresetMenu->popup(QPoint(x, y));
    });
    zoomLabel->setDoubleClickHandler([this]() {
        mZoomSlider->setValue(100);
    });

    layout->addWidget(mZoomOutButton);
    layout->addWidget(mZoomSlider);
    layout->addWidget(mZoomInButton);
    layout->addWidget(mZoomLabel);
    mZoomControl->setFixedWidth(layout->sizeHint().width());

    connect(mZoomSlider, &QSlider::valueChanged, mZoomControl,
            [this](int value) { setZoomPercentage(value); });
    connect(mZoomOutButton, &QToolButton::clicked, mZoomControl, [this]() {
        mZoomSlider->setValue(qMax(mZoomSlider->minimum(), mZoomSlider->value() - 10));
    });
    connect(mZoomInButton, &QToolButton::clicked, mZoomControl, [this]() {
        mZoomSlider->setValue(qMin(mZoomSlider->maximum(), mZoomSlider->value() + 10));
    });
    connect(this, &UBBoardController::zoomChanged, mZoomControl,
            [this](qreal zoomFactor) { updateZoomControl(zoomFactor); });
    connect(this, &UBBoardController::activeSceneChanged, mZoomControl,
            [this]() { updateZoomControl(currentZoom()); });

    updateZoomControl(1.0);
    positionZoomControl();
    mZoomControl->show();
    mZoomControl->raise();
}


void UBBoardController::positionZoomControl()
{
    if (!mZoomControl || !mControlView || !mControlContainer)
        return;

    const QRect viewportRect = mControlView->viewport()->geometry();
    const QPoint viewportBottomRight = mControlView->mapTo(
            mControlContainer, viewportRect.bottomRight());
    const int margin = 16;
    const int x = qMax(margin, viewportBottomRight.x() - mZoomControl->width() - margin);
    const int y = qMax(margin, viewportBottomRight.y() - mZoomControl->height() - margin);
    mZoomControl->move(x, y);
    mZoomControl->raise();
}


void UBBoardController::setupUndoRedoControl()
{
    mUndoRedoControl = new QFrame(mControlContainer);
    mUndoRedoControl->setObjectName(QStringLiteral("ubBoardUndoRedoControl"));
    mUndoRedoControl->setAttribute(Qt::WA_StyledBackground, true);
    mUndoRedoControl->setFixedHeight(44);

    QHBoxLayout* layout = new QHBoxLayout(mUndoRedoControl);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);

    auto createButton = [this, layout](const QString& objectName,
            const QIcon& icon, const QString& tooltip, QAction* action) {
        QToolButton* button = new QToolButton(mUndoRedoControl);
        button->setObjectName(objectName);
        button->setIcon(icon);
        button->setIconSize(QSize(28, 28));
        button->setToolTip(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(QSize(38, 32));
        button->setEnabled(action->isEnabled());
        button->setCheckable(action->isCheckable());
        button->setChecked(action->isChecked());

        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button, [button, action]() {
            button->setEnabled(action->isEnabled());
            button->setChecked(action->isChecked());
        });

        layout->addWidget(button);
        return button;
    };

    mUndoButton = createButton(QStringLiteral("ubBoardUndoButton"),
            historyArrowIcon(false), QStringLiteral("撤销"),
            mMainWindow->actionUndo);
    mRedoButton = createButton(QStringLiteral("ubBoardRedoButton"),
            historyArrowIcon(true), QStringLiteral("重做"),
            mMainWindow->actionRedo);

    QFrame* separator = new QFrame(mUndoRedoControl);
    separator->setObjectName(QStringLiteral("ubBottomQuickSeparator"));
    separator->setFrameShape(QFrame::VLine);
    separator->setFixedSize(1, 24);
    layout->addWidget(separator);

    mCaptureButton = createButton(QStringLiteral("ubBoardCaptureButton"),
            quickToolIcon(false), QStringLiteral("截屏"),
            mMainWindow->actionCapture);
    if (UBPlatformUtils::hasVirtualKeyboard())
    {
        mVirtualKeyboardButton = createButton(
                QStringLiteral("ubBoardKeyboardButton"),
                quickToolIcon(true), QStringLiteral("虚拟键盘"),
                mMainWindow->actionVirtualKeyboard);
    }

    mUndoRedoControl->setFixedWidth(layout->sizeHint().width());
    positionUndoRedoControl();
    mUndoRedoControl->show();
    mUndoRedoControl->raise();
}


void UBBoardController::positionUndoRedoControl()
{
    if (!mUndoRedoControl || !mControlView || !mControlContainer)
        return;

    const QRect viewportRect = mControlView->viewport()->geometry();
    const QPoint viewportBottomLeft = mControlView->mapTo(
            mControlContainer, viewportRect.bottomLeft());
    const int margin = 16;
    const int x = viewportBottomLeft.x() + margin;
    const int y = qMax(margin,
            viewportBottomLeft.y() - mUndoRedoControl->height() - margin);
    mUndoRedoControl->move(x, y);
    mUndoRedoControl->raise();
}


void UBBoardController::updateZoomControl(qreal zoomFactor)
{
    if (!mZoomSlider || !mZoomLabel)
        return;

    const int percentage = qBound(mZoomSlider->minimum(),
            qRound(zoomFactor * 100.0), mZoomSlider->maximum());
    const QSignalBlocker blocker(mZoomSlider);
    mZoomSlider->setValue(percentage);
    mZoomLabel->setText(QStringLiteral("%1%").arg(percentage));
    mZoomOutButton->setEnabled(percentage > mZoomSlider->minimum());
    mZoomInButton->setEnabled(percentage < mZoomSlider->maximum());
}


void UBBoardController::setZoomPercentage(int percentage)
{
    if (!mControlView || !mZoomSlider)
        return;

    const qreal minimumZoom = static_cast<qreal>(mZoomSlider->minimum()) / 100.0;
    const qreal targetZoom = qBound(minimumZoom, percentage / 100.0,
            static_cast<qreal>(UB_MAX_ZOOM));
    const qreal existingZoom = currentZoom();
    if (qFuzzyIsNull(existingZoom) || qFuzzyCompare(targetZoom, existingZoom))
    {
        updateZoomControl(targetZoom);
        return;
    }

    const QPointF sceneCenter = mControlView->mapToScene(
            mControlView->viewport()->rect().center());
    zoom(targetZoom / existingZoom, sceneCenter);
}


void UBBoardController::setupLayout()
{
    if(mPaletteManager)
        mPaletteManager->setupLayout();
}


void UBBoardController::setBoxing(QRect displayRect)
{
    if (displayRect.isNull())
    {
        mControlView->setBoxing({});
        return;
    }

    // compute boxing based on the assumed widget size for fullscreen
    QSize centralWidgetSize = mMainWindow->centralWidget()->size();
    QSize controlWindowSize = mMainWindow->size();
    QSize controlScreenSize = UBApplication::displayManager->screenSize(ScreenRole::Control);
    qreal controlWidth = controlScreenSize.width();
    qreal controlHeight = controlScreenSize.height() - controlWindowSize.height() + centralWidgetSize.height();
    qreal displayWidth = (qreal)displayRect.width();
    qreal displayHeight = (qreal)displayRect.height();

    qreal displayRatio = displayWidth / displayHeight;
    qreal controlRatio = controlWidth / controlHeight;

    if (displayRatio < controlRatio)
    {
        // Pillarboxing
        int boxWidth = (centralWidgetSize.width() - (displayWidth * (controlHeight / displayHeight))) / 2;

        if (boxWidth < 0)
        {
            boxWidth = 0;
        }

        mControlView->setBoxing({boxWidth, 0, boxWidth, 0});
    }
    else if (displayRatio > controlRatio)
    {
        // Letterboxing
        int boxHeight = (centralWidgetSize.height() - (displayHeight * (controlWidth / displayWidth))) / 2;

        if (boxHeight < 0)
        {
            boxHeight = 0;
        }

        mControlView->setBoxing({0, boxHeight, 0, boxHeight});
    }
    else
    {
        // No boxing
        mControlView->setBoxing({});
    }
}

void UBBoardController::setCursorFromAngle(qreal angle, const QPoint offset)
{
        QString displayedAngle = QString::number(angle, 'f', 1);
        QWidget *controlViewport = controlView()->viewport();

        QSize cursorSize(45,30);
        QSize bitmapSize = cursorSize;
        int hotX = -1;
        int hotY = -1;

        if (!offset.isNull())
        {
            bitmapSize.setWidth(std::max(bitmapSize.width(), 2 * std::abs(offset.x())));
            bitmapSize.setHeight(std::max(bitmapSize.height(), 2 * std::abs(offset.y())));
            hotX = bitmapSize.width() / 2 - offset.x();
            hotY = bitmapSize.height() / 2 - offset.y();
        }

        QSize origin = (bitmapSize - cursorSize) / 2;

        QImage mask_img(bitmapSize, QImage::Format_Mono);
        mask_img.fill(0xff);
        QPainter mask_ptr(&mask_img);
        mask_ptr.setBrush( QBrush( QColor(0, 0, 0) ) );
        mask_ptr.drawRoundedRect(origin.width(), origin.height(), cursorSize.width()-1, cursorSize.height()-1, 6, 6);
        QBitmap bmpMask = QBitmap::fromImage(mask_img);


        QPixmap pixCursor(bitmapSize);
        pixCursor.fill(QColor(Qt::white));

        QPainter painter(&pixCursor);

        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter.setBrush(QBrush(Qt::white));
        painter.setPen(QPen(QColor(Qt::black)));
        painter.drawRoundedRect(origin.width() + 1, origin.height() + 1,cursorSize.width()-2,cursorSize.height()-2,6,6);
        painter.setFont(QFont("Arial", 10));
        painter.drawText(origin.width() + 1, origin.height() + 1,cursorSize.width(),cursorSize.height(), Qt::AlignCenter, displayedAngle.append(QChar(176)));
        painter.end();

        pixCursor.setMask(bmpMask);
        controlViewport->setCursor(QCursor(pixCursor, hotX, hotY));
}


void UBBoardController::setupToolbar()
{
    UBSettings *settings = UBSettings::settings();

    // The stylus palette is kept internally because it owns the exclusive
    // tool action group, but its former toolbar entry is no longer part of
    // the user interface.
    mMainWindow->actionStylus->setChecked(false);
    mMainWindow->actionStylus->setVisible(false);

    // The former diagonal-line glyph suggested that this tool could only draw
    // lines.  Use a compact family-of-shapes icon in both the stylus palette
    // and its checked state instead.
    mMainWindow->actionLine->setIcon(shapeToolIcon());
    mMainWindow->actionLine->setText(QStringLiteral("形状"));
    mMainWindow->actionLine->setToolTip(QStringLiteral("绘制直线和常用几何图形"));

    // Keep the four most frequently used classroom tools within immediate
    // reach on the main toolbar.  Reuse the existing actions so shortcuts,
    // checked state and the dynamically coloured pen icon stay synchronized.
    QWidget* primaryTools = new QWidget(mMainWindow->boardToolBar);
    primaryTools->setObjectName(QStringLiteral("ubPrimaryTools"));
    primaryTools->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QHBoxLayout* primaryToolsLayout = new QHBoxLayout(primaryTools);
    primaryToolsLayout->setSizeConstraint(QLayout::SetFixedSize);
    primaryToolsLayout->setContentsMargins(3, 0, 5, 0);
    primaryToolsLayout->setSpacing(2);

    const QList<QAction*> primaryToolActions {
        mMainWindow->actionPen,
        mMainWindow->actionMarker,
        mMainWindow->actionEraser,
        mMainWindow->actionSelector,
        mMainWindow->actionPlay,
        mMainWindow->actionPointer,
        mMainWindow->actionText
    };
    for (QAction* action : primaryToolActions) {
        QToolButton* button = new QToolButton(primaryTools);
        button->setObjectName(QStringLiteral("ubPrimaryToolButton"));
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(30, 30));
        button->setFixedSize(QSize(44, 54));
        button->setCursor(Qt::PointingHandCursor);
        primaryToolsLayout->addWidget(button, 0, Qt::AlignVCenter);
    }
    mMainWindow->boardToolBar->insertWidget(mMainWindow->actionBackgrounds,
            primaryTools);

    // Setup color choice widget
    QList<QAction *> colorActions;
    colorActions.append(mMainWindow->actionColor0);
    colorActions.append(mMainWindow->actionColor1);
    colorActions.append(mMainWindow->actionColor2);
    colorActions.append(mMainWindow->actionColor3);
    colorActions.append(mMainWindow->actionColor4);

    UBToolbarButtonGroup *colorChoice =
            new UBToolbarButtonGroup(mMainWindow->boardToolBar, colorActions);
    colorChoice->setLabel(tr("Color"));

    mMainWindow->boardToolBar->insertWidget(mMainWindow->actionBackgrounds, colorChoice);
    mMainWindow->boardToolBar->insertAction(mMainWindow->actionBackgrounds, mMainWindow->actionCustomColor);

    connect(settings->appToolBarDisplayText, SIGNAL(changed(QVariant)), colorChoice, SLOT(displayText(QVariant)));
    connect(colorChoice, SIGNAL(activated(int)), this, SLOT(setColorIndex(int)));
    connect(mMainWindow->actionCustomColor, &QAction::triggered, this, &UBBoardController::chooseCustomColor);
    connect(UBDrawingController::drawingController(), SIGNAL(colorIndexChanged(int)), colorChoice, SLOT(setCurrentIndex(int)));
    connect(UBDrawingController::drawingController(), SIGNAL(colorIndexChanged(int)), UBDrawingController::drawingController(), SIGNAL(colorPaletteChanged()));
    connect(UBDrawingController::drawingController(), SIGNAL(colorPaletteChanged()), colorChoice, SLOT(colorPaletteChanged()));
    connect(UBDrawingController::drawingController(), SIGNAL(colorPaletteChanged()), this, SLOT(colorPaletteChanged()));

    colorChoice->displayText(QVariant(settings->appToolBarDisplayText->get().toBool()));
    colorChoice->colorPaletteChanged();
    colorChoice->setCurrentIndex(settings->penColorIndex());
    colorActions.at(settings->penColorIndex())->setChecked(true);

    // Setup line width choice widget
    QList<QAction *> lineWidthActions;
    lineWidthActions.append(mMainWindow->actionLineSmall);
    lineWidthActions.append(mMainWindow->actionLineMedium);
    lineWidthActions.append(mMainWindow->actionLineLarge);

    UBToolbarButtonGroup *lineWidthChoice =
            new UBToolbarButtonGroup(mMainWindow->boardToolBar, lineWidthActions);

    connect(settings->appToolBarDisplayText, SIGNAL(changed(QVariant)), lineWidthChoice, SLOT(displayText(QVariant)));

    connect(lineWidthChoice, SIGNAL(activated(int))
            , UBDrawingController::drawingController(), SLOT(setLineWidthIndex(int)));

    connect(UBDrawingController::drawingController(), SIGNAL(lineWidthIndexChanged(int))
            , lineWidthChoice, SLOT(setCurrentIndex(int)));
    connect(UBDrawingController::drawingController(), SIGNAL(colorPaletteChanged())
            , lineWidthChoice, SLOT(colorPaletteChanged()));

    lineWidthChoice->displayText(QVariant(settings->appToolBarDisplayText->get().toBool()));
    lineWidthChoice->setCurrentIndex(settings->penWidthIndex());
    lineWidthActions.at(settings->penWidthIndex())->setChecked(true);

    QAction* lineWidthChoiceAction =
            mMainWindow->boardToolBar->insertWidget(mMainWindow->actionBackgrounds, lineWidthChoice);

    // Contextual geometry and line-style controls. Keep only the current
    // selection visible in the toolbar; the complete choices live in menus.
    // This remains readable at 100%-200% Windows display scaling and avoids
    // making five state buttons visually dominate the toolbar.
    QWidget* lineOptions = new QWidget(mMainWindow->boardToolBar);
    lineOptions->setObjectName(QStringLiteral("ubLineOptions"));
    lineOptions->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QHBoxLayout* lineOptionsLayout = new QHBoxLayout(lineOptions);
    lineOptionsLayout->setSizeConstraint(QLayout::SetFixedSize);
    lineOptionsLayout->setContentsMargins(4, 0, 4, 0);
    lineOptionsLayout->setSpacing(7);

    auto makeOptionMenuButton = [lineOptions, lineOptionsLayout](
            const QString& text, const QString& tooltip) {
        QToolButton* button = new QToolButton(lineOptions);
        button->setObjectName(QStringLiteral("ubLineOptionMenuButton"));
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setIconSize(QSize(20, 20));
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tooltip);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

        QMenu* menu = new QMenu(button);
        menu->setObjectName(QStringLiteral("ubLineOptionMenu"));
        button->setMenu(menu);
        lineOptionsLayout->addWidget(button, 0, Qt::AlignVCenter);
        return qMakePair(button, menu);
    };

    const auto geometryControl = makeOptionMenuButton(QStringLiteral("形状"),
            QStringLiteral("选择基础图形和常用数学图形"));
    const auto patternControl = makeOptionMenuButton(QStringLiteral("线型"),
            QStringLiteral("选择实线或虚线"));
    const auto fillControl = makeOptionMenuButton(QStringLiteral("填充"),
            QStringLiteral("设置闭合图形的填充颜色和透明度"));

    auto addMenuChoice = [](QMenu* menu, QActionGroup* group, int iconOption,
            const QString& text, int value, const QString& tooltip) {
        QAction* action = menu->addAction(lineOptionIcon(iconOption), text);
        action->setCheckable(true);
        action->setData(value);
        action->setProperty("lineIconOption", iconOption);
        action->setToolTip(tooltip);
        group->addAction(action);
        return action;
    };

    QActionGroup* geometryGroup = new QActionGroup(lineOptions);
    geometryGroup->setExclusive(true);
    geometryControl.second->addSection(QStringLiteral("基础图形"));
    addMenuChoice(geometryControl.second, geometryGroup, 0,
            QStringLiteral("直线"), UBDrawingController::StraightLineGeometry,
            QStringLiteral("绘制直线"));
    addMenuChoice(geometryControl.second, geometryGroup, 1,
            QStringLiteral("长方形"), UBDrawingController::SquareGeometry,
            QStringLiteral("自由绘制长方形；按住 Shift 绘制正方形"));
    addMenuChoice(geometryControl.second, geometryGroup, 2,
            QStringLiteral("椭圆"), UBDrawingController::CircleGeometry,
            QStringLiteral("自由绘制椭圆；按住 Shift 绘制圆形"));
    addMenuChoice(geometryControl.second, geometryGroup, 5,
            QStringLiteral("三角形"), UBDrawingController::TriangleGeometry,
            QStringLiteral("自由绘制三角形；按住 Shift 保持等宽等高"));
    addMenuChoice(geometryControl.second, geometryGroup, 6,
            QStringLiteral("箭头"), UBDrawingController::ArrowGeometry,
            QStringLiteral("拖动绘制带箭头的指示线"));
    addMenuChoice(geometryControl.second, geometryGroup, 8,
            QStringLiteral("多边形"), UBDrawingController::PolygonGeometry,
            QStringLiteral("单击添加顶点，右击闭合并结束"));

    geometryControl.second->addSection(QStringLiteral("数学图形"));
    addMenuChoice(geometryControl.second, geometryGroup, 12,
            QStringLiteral("数轴"), UBDrawingController::NumberLineGeometry,
            QStringLiteral("拖动绘制带刻度和双向箭头的数轴"));
    addMenuChoice(geometryControl.second, geometryGroup, 7,
            QStringLiteral("坐标轴"), UBDrawingController::AxesGeometry,
            QStringLiteral("拖动绘制带正方向箭头的横纵坐标轴"));
    addMenuChoice(geometryControl.second, geometryGroup, 9,
            QStringLiteral("抛物线"), UBDrawingController::ParabolaGeometry,
            QStringLiteral("拖动确定范围；向下拖动绘制开口向上的抛物线"));
    addMenuChoice(geometryControl.second, geometryGroup, 10,
            QStringLiteral("双曲线"), UBDrawingController::HyperbolaGeometry,
            QStringLiteral("拖动确定双曲线的范围"));
    addMenuChoice(geometryControl.second, geometryGroup, 11,
            QStringLiteral("正弦曲线"), UBDrawingController::SineGeometry,
            QStringLiteral("拖动确定一个完整周期的范围"));

    geometryControl.second->addSeparator();
    QMenu* moreGeometryMenu = geometryControl.second->addMenu(QStringLiteral("更多"));
    moreGeometryMenu->setObjectName(QStringLiteral("ubMoreGeometryMenu"));
    moreGeometryMenu->addSection(QStringLiteral("平面图形"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 13,
            QStringLiteral("平行四边形"), UBDrawingController::ParallelogramGeometry,
            QStringLiteral("拖动绘制平行四边形"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 14,
            QStringLiteral("梯形"), UBDrawingController::TrapezoidGeometry,
            QStringLiteral("拖动绘制梯形"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 15,
            QStringLiteral("五边形"), UBDrawingController::PentagonGeometry,
            QStringLiteral("拖动绘制正五边形"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 16,
            QStringLiteral("六边形"), UBDrawingController::HexagonGeometry,
            QStringLiteral("拖动绘制正六边形"));
    moreGeometryMenu->addSection(QStringLiteral("立体图形"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 17,
            QStringLiteral("正方体"), UBDrawingController::CubeGeometry,
            QStringLiteral("拖动绘制正方体"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 18,
            QStringLiteral("长方体"), UBDrawingController::CuboidGeometry,
            QStringLiteral("拖动绘制长方体"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 19,
            QStringLiteral("圆柱体"), UBDrawingController::CylinderGeometry,
            QStringLiteral("拖动绘制圆柱体"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 20,
            QStringLiteral("圆锥体"), UBDrawingController::ConeGeometry,
            QStringLiteral("拖动绘制圆锥体"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 21,
            QStringLiteral("球体"), UBDrawingController::SphereGeometry,
            QStringLiteral("拖动绘制球体"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 22,
            QStringLiteral("四棱锥"), UBDrawingController::PyramidGeometry,
            QStringLiteral("拖动绘制四棱锥"));
    addMenuChoice(moreGeometryMenu, geometryGroup, 23,
            QStringLiteral("三棱柱"), UBDrawingController::TriangularPrismGeometry,
            QStringLiteral("拖动绘制三棱柱"));

    QActionGroup* patternGroup = new QActionGroup(lineOptions);
    patternGroup->setExclusive(true);
    addMenuChoice(patternControl.second, patternGroup, 3,
            QStringLiteral("实线"), UBDrawingController::SolidLinePattern,
            QStringLiteral("绘制实线"));
    addMenuChoice(patternControl.second, patternGroup, 4,
            QStringLiteral("虚线"), UBDrawingController::DashedLinePattern,
            QStringLiteral("绘制虚线"));

    UBDrawingController* drawingController = UBDrawingController::drawingController();

    QAction* fillEnabledAction = fillControl.second->addAction(
            QStringLiteral("启用填充"));
    fillEnabledAction->setCheckable(true);
    fillEnabledAction->setToolTip(QStringLiteral("仅对长方形、椭圆、三角形和多边形生效"));

    QAction* fillColorAction = fillControl.second->addAction(
            QStringLiteral("选择填充颜色…"));
    fillControl.second->addSeparator();

    QWidgetAction* opacityAction = new QWidgetAction(fillControl.second);
    QWidget* opacityWidget = new QWidget(fillControl.second);
    QHBoxLayout* opacityLayout = new QHBoxLayout(opacityWidget);
    opacityLayout->setContentsMargins(12, 6, 12, 8);
    opacityLayout->setSpacing(8);
    QLabel* opacityCaption = new QLabel(QStringLiteral("透明度"), opacityWidget);
    QSlider* opacitySlider = new QSlider(Qt::Horizontal, opacityWidget);
    opacitySlider->setRange(0, 100);
    opacitySlider->setSingleStep(1);
    opacitySlider->setPageStep(10);
    opacitySlider->setFixedWidth(120);
    QLabel* opacityValue = new QLabel(opacityWidget);
    opacityValue->setMinimumWidth(38);
    opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opacityLayout->addWidget(opacityCaption);
    opacityLayout->addWidget(opacitySlider);
    opacityLayout->addWidget(opacityValue);
    opacityAction->setDefaultWidget(opacityWidget);
    fillControl.second->addAction(opacityAction);

    auto updateMenuButton = [](QToolButton* button, QActionGroup* group,
            int value, const QString& prefix) {
        for (QAction* action : group->actions()) {
            const bool selected = action->data().toInt() == value;
            action->setChecked(selected);
            if (selected) {
                button->setIcon(action->icon());
                button->setToolTip(prefix + action->text());
            }
        }
    };

    auto updateGeometryButton = [geometryControl, geometryGroup, updateMenuButton](int mode) {
        updateMenuButton(geometryControl.first, geometryGroup, mode,
                QStringLiteral("当前形状："));
    };
    auto updatePatternButton = [patternControl, patternGroup, updateMenuButton](int pattern) {
        updateMenuButton(patternControl.first, patternGroup, pattern,
                QStringLiteral("当前线型："));
    };
    auto updateFillButton = [fillControl, fillEnabledAction, fillColorAction,
            opacitySlider, opacityValue, drawingController]() {
        const bool enabled = drawingController->shapeFillEnabled();
        const QColor color = drawingController->shapeFillColor();
        const int opacity = drawingController->shapeFillOpacity();
        fillEnabledAction->setChecked(enabled);
        fillColorAction->setIcon(fillOptionIcon(true, color, opacity));
        opacitySlider->setValue(opacity);
        opacityValue->setText(QStringLiteral("%1%").arg(opacity));
        fillControl.first->setIcon(fillOptionIcon(enabled, color, opacity));
        fillControl.first->setToolTip(enabled
                ? QStringLiteral("填充：%1，透明度 %2%").arg(color.name().toUpper()).arg(opacity)
                : QStringLiteral("填充：无"));
    };

    connect(geometryGroup, &QActionGroup::triggered, lineOptions,
            [drawingController](QAction* action) {
        drawingController->setLineGeometryMode(action->data().toInt());
    });
    connect(patternGroup, &QActionGroup::triggered, lineOptions,
            [drawingController](QAction* action) {
        drawingController->setLinePattern(action->data().toInt());
    });
    connect(fillEnabledAction, &QAction::toggled, lineOptions,
            [drawingController](bool enabled) {
        drawingController->setShapeFillEnabled(enabled);
    });
    connect(fillColorAction, &QAction::triggered, lineOptions,
            [this, drawingController]() {
        QColor initial = drawingController->shapeFillColor();
        initial.setAlpha(255);
        const QColor selected = QColorDialog::getColor(initial, mMainWindow,
                QStringLiteral("选择填充颜色"), QColorDialog::ShowAlphaChannel);
        if (!selected.isValid())
            return;
        drawingController->setShapeFillColor(selected);
        drawingController->setShapeFillOpacity(
                qRound(selected.alphaF() * 100.0));
        drawingController->setShapeFillEnabled(true);
    });
    connect(opacitySlider, &QSlider::valueChanged, lineOptions,
            [drawingController](int value) {
        drawingController->setShapeFillOpacity(value);
    });
    connect(drawingController, &UBDrawingController::lineGeometryModeChanged,
            lineOptions, updateGeometryButton);
    connect(drawingController, &UBDrawingController::linePatternChanged,
            lineOptions, updatePatternButton);
    connect(drawingController, &UBDrawingController::shapeFillEnabledChanged,
            lineOptions, [updateFillButton](bool) { updateFillButton(); });
    connect(drawingController, &UBDrawingController::shapeFillColorChanged,
            lineOptions, [updateFillButton](const QColor&) { updateFillButton(); });
    connect(drawingController, &UBDrawingController::shapeFillOpacityChanged,
            lineOptions, [updateFillButton](int) { updateFillButton(); });

    updateGeometryButton(drawingController->lineGeometryMode());
    updatePatternButton(drawingController->linePattern());
    updateFillButton();

    QAction* lineOptionsAction =
            mMainWindow->boardToolBar->insertWidget(mMainWindow->actionBackgrounds, lineOptions);

    //-----------------------------------------------------------//
    // Setup eraser width choice widget

    QList<QAction *> eraserWidthActions;
    eraserWidthActions.append(mMainWindow->actionEraserSmall);
    eraserWidthActions.append(mMainWindow->actionEraserMedium);
    eraserWidthActions.append(mMainWindow->actionEraserLarge);

    UBToolbarButtonGroup *eraserWidthChoice =
            new UBToolbarButtonGroup(mMainWindow->boardToolBar, eraserWidthActions);

    QAction* eraserWidthChoiceAction =
            mMainWindow->boardToolBar->insertWidget(lineOptionsAction, eraserWidthChoice);

    connect(settings->appToolBarDisplayText, SIGNAL(changed(QVariant)), eraserWidthChoice, SLOT(displayText(QVariant)));
    connect(eraserWidthChoice, SIGNAL(activated(int)), UBDrawingController::drawingController(), SLOT(setEraserWidthIndex(int)));

    eraserWidthChoice->displayText(QVariant(settings->appToolBarDisplayText->get().toBool()));
    eraserWidthChoice->setCurrentIndex(settings->eraserWidthIndex());
    eraserWidthActions.at(settings->eraserWidthIndex())->setChecked(true);

    QAction* shapeLibraryAction = new QAction(
            QIcon(QStringLiteral(":/images/libpalette/home.png")),
            QStringLiteral("库"), mMainWindow->boardToolBar);
    shapeLibraryAction->setToolTip(QStringLiteral("打开完整资源库"));
    connect(shapeLibraryAction, &QAction::triggered, mMainWindow->boardToolBar,
            [this]() {
        if (!mPaletteManager)
            return;
        mPaletteManager->featuresWidget()->showRoot();
        mPaletteManager->rightPalette()->activateWidget(
                mPaletteManager->featuresWidget());
    });
    mMainWindow->boardToolBar->insertAction(
            mMainWindow->actionBackgrounds, shapeLibraryAction);

    auto updateContextControls = [lineOptionsAction, geometryControl,
            patternControl, fillControl, lineOptions, lineWidthChoiceAction,
            eraserWidthChoiceAction](int tool) {
        const bool lineTool = tool == UBStylusTool::Line;
        // Shape, line pattern and fill are global shortcuts. Keep all three
        // visible from startup so their availability does not depend on the
        // user selecting a shape once.
        lineOptionsAction->setVisible(true);
        geometryControl.first->setVisible(true);
        patternControl.first->setVisible(true);
        fillControl.first->setVisible(true);
        lineOptions->updateGeometry();
        lineWidthChoiceAction->setVisible(tool == UBStylusTool::Pen
                || tool == UBStylusTool::Marker || lineTool);
        eraserWidthChoiceAction->setVisible(tool == UBStylusTool::Eraser);
    };
    connect(drawingController, &UBDrawingController::stylusToolChanged,
            lineOptions, [updateContextControls](int tool, int) {
        updateContextControls(tool);
    });
    updateContextControls(drawingController->stylusTool());

    // Keep property controls compact on the left and navigation/application
    // controls consistently aligned to the right. The spacer collapses first
    // on narrower displays, so the toolbar still remains on one row.
    QWidget* toolbarSpacer = new QWidget(mMainWindow->boardToolBar);
    toolbarSpacer->setObjectName(QStringLiteral("ubBoardToolbarSpacer"));
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mMainWindow->boardToolBar->insertWidget(mMainWindow->actionBackgrounds,
            toolbarSpacer);

    //-----------------------------------------------------------//

    UBApplication::app()->decorateActionMenu(mMainWindow->actionMenu);

    // These actions remain available through the Settings menu or the
    // floating canvas controls, but no longer consume toolbar space.
    const QList<QAction*> toolbarActions = mMainWindow->boardToolBar->actions();
    const int backgroundIndex = toolbarActions.indexOf(mMainWindow->actionBackgrounds);
    const int pagesIndex = toolbarActions.indexOf(mMainWindow->actionPages);
    QList<QAction*> obsoleteSeparators;
    if (backgroundIndex >= 0 && pagesIndex > backgroundIndex)
    {
        for (int index = backgroundIndex; index < pagesIndex; ++index)
        {
            QAction* action = toolbarActions.at(index);
            if (action->isSeparator())
                obsoleteSeparators.append(action);
        }
    }

    mMainWindow->boardToolBar->removeAction(mMainWindow->actionBackgrounds);
    mMainWindow->boardToolBar->removeAction(mMainWindow->actionUndo);
    mMainWindow->boardToolBar->removeAction(mMainWindow->actionRedo);
    for (QAction* separator : obsoleteSeparators)
        mMainWindow->boardToolBar->removeAction(separator);

    mMainWindow->actionBoard->setVisible(false);

    mMainWindow->webToolBar->hide();
    mMainWindow->documentToolBar->hide();

    connectToolbar();
    initToolbarTexts();

    UBApplication::app()->toolBarDisplayTextChanged(QVariant(settings->appToolBarDisplayText->get().toBool()));
    refreshPenVisuals();
}


void UBBoardController::setToolCursor(int tool)
{
    if (mActiveScene)
        mActiveScene->setToolCursor(tool);

    mControlView->setToolCursor(tool);
}


void UBBoardController::connectToolbar()
{
    connect(mMainWindow->actionAdd, SIGNAL(triggered()), this, SLOT(addItem()));
    connect(mMainWindow->actionNewPage, SIGNAL(triggered()), this, SLOT(addScene()));
    connect(mMainWindow->actionDuplicatePage, SIGNAL(triggered()), this, SLOT(duplicateScene()));

    connect(mMainWindow->actionClearPage, SIGNAL(triggered()), this, SLOT(clearScene()));
    connect(mMainWindow->actionEraseItems, SIGNAL(triggered()), this, SLOT(clearSceneItems()));
    connect(mMainWindow->actionEraseAnnotations, SIGNAL(triggered()), this, SLOT(clearSceneAnnotation()));
    connect(mMainWindow->actionEraseBackground,SIGNAL(triggered()),this,SLOT(clearSceneBackground()));

    connect(mMainWindow->actionUndo, SIGNAL(triggered()), UBApplication::undoStack, SLOT(undo()));
    connect(mMainWindow->actionRedo, SIGNAL(triggered()), UBApplication::undoStack, SLOT(redo()));
    connect(mMainWindow->actionRedo, SIGNAL(triggered()), this, SLOT(startScript()));
    connect(mMainWindow->actionBack, SIGNAL( triggered()), this, SLOT(previousScene()));
    connect(mMainWindow->actionForward, SIGNAL(triggered()), this, SLOT(nextScene()));
    connect(mMainWindow->actionSleep, SIGNAL(triggered()), this, SLOT(stopScript()));
    connect(mMainWindow->actionSleep, SIGNAL(triggered()), this, SLOT(blackout()));
    connect(mMainWindow->actionVirtualKeyboard, SIGNAL(triggered(bool)), this, SLOT(showKeyboard(bool)));
    connect(mMainWindow->actionImportPage, SIGNAL(triggered()), this, SLOT(importPage()));
}

void UBBoardController::startScript()
{
    freezeW3CWidgets(false);
}

void UBBoardController::stopScript()
{
    freezeW3CWidgets(true);
}

void UBBoardController::saveData(SaveFlags fls)
{
    bool verbose = fls | sf_showProgress;
    if (verbose) {
        UBApplication::showMessage(tr("Saving document..."));
    }
    if (mActiveScene && mActiveScene->isModified()) {
        persistCurrentScene(true);
    }
    if (verbose) {
        UBApplication::showMessage(tr("Document has just been saved..."));
    }
}

void UBBoardController::documentSceneDuplicated(std::shared_ptr<UBDocumentProxy> proxy, int index)
{
    // index is duplicated page
    if (selectedDocument() == proxy)
    {
        if (UBApplication::applicationController->displayMode() == UBApplicationController::Board)
        {
            // directly change scene to new duplicate
            setActiveDocumentScene(index);
        }
        else if (index <= mActiveSceneIndex)
        {
            // just shift selection and remember for the next time we switch to Board mode
            mSwitchToSceneIndex = mActiveSceneIndex + 1;
        }
    }
}

void UBBoardController::documentSceneMoved(std::shared_ptr<UBDocumentProxy> proxy, int fromIndex, int toIndex)
{
    if (selectedDocument() == proxy)
    {
        int nextSceneIndex = mActiveSceneIndex;

        if (fromIndex < mActiveSceneIndex && toIndex >= mActiveSceneIndex)
        {
            --nextSceneIndex;
        }
        else if (fromIndex > mActiveSceneIndex && toIndex <= mActiveSceneIndex)
        {
            ++nextSceneIndex;
        }
        else if (fromIndex == mActiveSceneIndex)
        {
            nextSceneIndex = toIndex;
        }

        if (nextSceneIndex == mActiveSceneIndex)
        {
            // no change
            return;
        }

        if (UBApplication::applicationController->displayMode() == UBApplicationController::Board)
        {
            // directly change scene
            setActiveDocumentScene(nextSceneIndex);
        }
        else
        {
            // just remember for the next time we switch to Board mode
            mSwitchToSceneIndex = nextSceneIndex;
        }
    }
}

void UBBoardController::documentSceneDeleted(std::shared_ptr<UBDocumentProxy> proxy, int index)
{
    if (selectedDocument() == proxy)
    {
        int nextSceneIndex = mActiveSceneIndex;

        if (index < mActiveSceneIndex || (index == mActiveSceneIndex && index == proxy->pageCount() && index > 0))
        {
            --nextSceneIndex;
        }

        if (UBApplication::applicationController->displayMode() == UBApplicationController::Board)
        {
            // directly change scene
            setActiveDocumentScene(nextSceneIndex);
        }
        else
        {
            // just remember for the next time we switch to Board mode
            mSwitchToSceneIndex = nextSceneIndex;
        }
    }
}

void UBBoardController::initToolbarTexts()
{
    QList<QAction*> allToolbarActions;

    allToolbarActions << mMainWindow->boardToolBar->actions();
    allToolbarActions << mMainWindow->webToolBar->actions();
    allToolbarActions << mMainWindow->documentToolBar->actions();

    foreach(QAction* action, allToolbarActions)
    {
        QString nominalText = action->text();
        QString shortText = truncate(nominalText, 48);
        QPair<QString, QString> texts(nominalText, shortText);

        mActionTexts.insert(action, texts);
    }
}


void UBBoardController::setToolbarTexts()
{
    QSize iconSize;

    if (mMainWindow->width() <= 1280)
        iconSize = QSize(24, 24);
    else
        iconSize = QSize(32, 26);

    mMainWindow->boardToolBar->setIconSize(iconSize);
    mMainWindow->webToolBar->setIconSize(iconSize);
    mMainWindow->documentToolBar->setIconSize(iconSize);

    foreach(QAction* action, mActionTexts.keys())
    {
        QPair<QString, QString> texts = mActionTexts.value(action);

        if (mMainWindow->width() <= 1024)
            action->setText(texts.second);
        else
            action->setText(texts.first);

        action->setToolTip(texts.first);
    }
}


QString UBBoardController::truncate(QString text, int maxWidth) const
{
    QFontMetricsF fontMetrics(mMainWindow->font());
    return fontMetrics.elidedText(text, Qt::ElideRight, maxWidth);
}


void UBBoardController::stylusToolDoubleClicked(int tool)
{
    if (tool == UBStylusTool::ZoomIn || tool == UBStylusTool::ZoomOut)
    {
        zoomRestore();
    }
    else if (tool == UBStylusTool::Hand)
    {
        centerRestore();
    }
}



void UBBoardController::addScene()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    persistViewPositionOnCurrentScene();
    persistCurrentScene(false,true);

    auto document = UBDocument::getDocument(selectedDocument());
    document->createPage(mActiveSceneIndex + 1);

    QDateTime now = QDateTime::currentDateTime();
    selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));

    setActiveDocumentScene(mActiveSceneIndex + 1);
    QApplication::restoreOverrideCursor();

    UBPersistenceManager::persistenceManager()->persistDocumentMetadata(selectedDocument());
}

void UBBoardController::addScene(std::shared_ptr<UBGraphicsScene> scene, bool replaceActiveIfEmpty)
{
    if (scene)
    {
        std::shared_ptr<UBGraphicsScene> clone = scene->sceneDeepCopy();

        if (scene->document() && (scene->document() != selectedDocument()))
        {
            foreach(QUrl relativeFile, scene->relativeDependencies())
            {
                QString source = scene->document()->persistencePath() + "/" + relativeFile.path();
                QString destination = selectedDocument()->persistencePath() + "/" + relativeFile.path();

                UBFileSystemUtils::copy(source, destination, true);
            }
        }

        auto document = UBDocument::getDocument(selectedDocument());

        if (replaceActiveIfEmpty && mActiveScene->isEmpty())
        {
            document->insertPage(clone, mActiveSceneIndex);
            setActiveDocumentScene(mActiveSceneIndex);
            deleteScene(mActiveSceneIndex + 1);
        }
        else
        {
            persistCurrentScene(false,true);
            document->insertPage(clone, mActiveSceneIndex + 1);
            setActiveDocumentScene(mActiveSceneIndex + 1);
        }

        QDateTime now = QDateTime::currentDateTime();
        selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));
    }
}


void UBBoardController::addScene(std::shared_ptr<UBDocumentProxy> proxy, int sceneIndex, bool replaceActiveIfEmpty)
{
    std::shared_ptr<UBGraphicsScene> scene = UBPersistenceManager::persistenceManager()->loadDocumentScene(proxy, sceneIndex);

    if (scene)
    {
        addScene(scene, replaceActiveIfEmpty);
    }
}

void UBBoardController::duplicateScene(int nIndex)
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    persistCurrentScene(false,true);

    duplicatePage(nIndex);

    QDateTime now = QDateTime::currentDateTime();
    selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));

    setActiveDocumentScene(nIndex + 1);
    QApplication::restoreOverrideCursor();
}

void UBBoardController::duplicateScene()
{
    if (UBApplication::applicationController->displayMode() != UBApplicationController::Board)
        return;
    duplicateScene(mActiveSceneIndex);
}

UBGraphicsItem *UBBoardController::duplicateItem(UBItem *item)
{
    if (!item)
        return NULL;

    UBGraphicsItem *retItem = NULL;

    mLastCreatedItem = NULL;

    QUrl sourceUrl;
    QByteArray pData;

    //common parameters for any item
    QPointF itemPos;
    QSizeF itemSize;

    QGraphicsItem *commonItem = dynamic_cast<QGraphicsItem*>(item);
    if (commonItem)
    {
        qreal shifting = UBSettings::settings()->objectFrameWidth;
        itemPos = commonItem->pos() + QPointF(shifting,shifting);
        itemSize = commonItem->boundingRect().size();
        commonItem->setSelected(false);

    }

    UBMimeType::Enum itemMimeType;

    QString srcFile = item->sourceUrl().toLocalFile();
    if (srcFile.isEmpty())
        srcFile = item->sourceUrl().toString();

    QString contentTypeHeader;
    if (!srcFile.isEmpty())
        contentTypeHeader = UBFileSystemUtils::mimeTypeFromFileName(srcFile);

    if(NULL != qgraphicsitem_cast<UBGraphicsGroupContainerItem*>(commonItem))
        itemMimeType = UBMimeType::Group;
    else
        itemMimeType = UBFileSystemUtils::mimeTypeFromString(contentTypeHeader);

    switch(static_cast<int>(itemMimeType))
    {
    case UBMimeType::AppleWidget:
    case UBMimeType::W3CWidget:
        {
            UBGraphicsWidgetItem *witem = dynamic_cast<UBGraphicsWidgetItem*>(item);
            if (witem)
            {
                sourceUrl = witem->getOwnFolder();
            }
        }break;

    case UBMimeType::Video:
    case UBMimeType::Audio:
        {
            UBGraphicsMediaItem *mitem = dynamic_cast<UBGraphicsMediaItem*>(item);
            if (mitem)
            {
                sourceUrl = mitem->mediaFileUrl();
                downloadURL(sourceUrl, srcFile, itemPos, QSize(itemSize.width(), itemSize.height()), false, false);
                return NULL; // async operation
            }
        }break;

    case UBMimeType::VectorImage:
        {
            UBGraphicsSvgItem *viitem = dynamic_cast<UBGraphicsSvgItem*>(item);
            if (viitem)
            {
                pData = viitem->fileData();
                sourceUrl = item->sourceUrl();
            }
        }break;

    case UBMimeType::RasterImage:
        {
            UBGraphicsPixmapItem *pixitem = dynamic_cast<UBGraphicsPixmapItem*>(item);
            if (pixitem)
            {
                 QBuffer buffer(&pData);
                 buffer.open(QIODevice::WriteOnly);
                 QString format = UBFileSystemUtils::extension(item->sourceUrl().toString(QUrl::DecodeReserved));
                 pixitem->pixmap().save(&buffer, format.toLatin1());
            }
        }break;

    case UBMimeType::Group:
    {
        UBGraphicsGroupContainerItem* groupItem = dynamic_cast<UBGraphicsGroupContainerItem*>(item);
        UBGraphicsGroupContainerItem* duplicatedGroup = NULL;

        QList<QGraphicsItem*> duplicatedItems;
        QList<QGraphicsItem*> children = groupItem->childItems();

        mActiveScene->setURStackEnable(false);
        foreach(QGraphicsItem* pIt, children){
            UBItem* pItem = dynamic_cast<UBItem*>(pIt);
            if(pItem)
            {
                QGraphicsItem * itemToGroup = dynamic_cast<QGraphicsItem *>(duplicateItem(pItem));
                if (itemToGroup)
                {
                    itemToGroup->setZValue(pIt->zValue());
                    itemToGroup->setData(UBGraphicsItemData::ItemOwnZValue, pIt->data(UBGraphicsItemData::ItemOwnZValue).toReal());
                    duplicatedItems.append(itemToGroup);
                }
            }
        }
        duplicatedGroup = mActiveScene->createGroup(duplicatedItems);
        duplicatedGroup->setTransform(groupItem->transform());
        groupItem->copyItemParameters(duplicatedGroup);
        groupItem->setSelected(false);


        retItem = dynamic_cast<UBGraphicsItem *>(duplicatedGroup);

        QGraphicsItem * itemToAdd = dynamic_cast<QGraphicsItem *>(retItem);
        if (itemToAdd)
        {
            mActiveScene->addItem(itemToAdd);
            itemToAdd->setSelected(true);
        }
        mActiveScene->setURStackEnable(true);
    }break;

    case UBMimeType::UNKNOWN:
        {
            QGraphicsItem *copiedItem = dynamic_cast<QGraphicsItem*>(item);
            QGraphicsItem *gitem = dynamic_cast<QGraphicsItem*>(item->deepCopy());
            if (gitem)
            {
                mActiveScene->addItem(gitem);

                if (copiedItem)
                {
                    if (mActiveScene->tools().contains(copiedItem))
                    {
                        mActiveScene->registerTool(gitem);
                    }
                }
                gitem->setPos(itemPos);

                mLastCreatedItem = gitem;
                gitem->setSelected(true);
            }
            retItem = dynamic_cast<UBGraphicsItem *>(gitem);
        }break;
    }

    if (retItem)
    {
        QGraphicsItem *graphicsRetItem = dynamic_cast<QGraphicsItem *>(retItem);
        if (mActiveScene->isURStackIsEnabled()) { //should be deleted after scene own undo stack implemented
             UBGraphicsItemUndoCommand* uc = new UBGraphicsItemUndoCommand(mActiveScene, 0, graphicsRetItem);
             UBApplication::undoStack->push(uc);
        }
        return retItem;
    }

    UBItem *createdItem = downloadFinished(true, sourceUrl, QUrl::fromLocalFile(srcFile), contentTypeHeader, pData, itemPos, QSize(itemSize.width(), itemSize.height()), false);
    if (createdItem)
    {
        createdItem->setSourceUrl(item->sourceUrl());
        item->copyItemParameters(createdItem);

        QGraphicsItem *createdGitem = dynamic_cast<QGraphicsItem*>(createdItem);
        if (createdGitem)
            createdGitem->setPos(itemPos);
        mLastCreatedItem = dynamic_cast<QGraphicsItem*>(createdItem);
        mLastCreatedItem->setSelected(true);

        retItem = dynamic_cast<UBGraphicsItem *>(createdItem);
    }

    return retItem;
}

void UBBoardController::deleteScene(int nIndex)
{
    if (selectedDocument()->pageCount()>=2)
    {
        mDeletingSceneIndex = nIndex;
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        persistCurrentScene();
        UBApplication::showMessage(tr("Deleting page %1").arg(nIndex+1), true);

        auto document = UBDocument::getDocument(selectedDocument());
        document->deletePages({nIndex});

        QDateTime now = QDateTime::currentDateTime();
        selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));
        UBMetadataDcSubsetAdaptor::persist(selectedDocument());

        if (nIndex >= pageCount())
            nIndex = pageCount()-1;
        setActiveDocumentScene(nIndex);
        UBApplication::showMessage(tr("Page %1 deleted").arg(nIndex+1));
        QApplication::restoreOverrideCursor();
        mDeletingSceneIndex = -1;
    }
}


void UBBoardController::clearScene()
{
    if (mActiveScene)
    {
        freezeW3CWidgets(true);
        mActiveScene->clearContent(UBGraphicsScene::clearItemsAndAnnotations);
        updateActionStates();
    }
}


void UBBoardController::clearSceneItems()
{
    if (mActiveScene)
    {
        freezeW3CWidgets(true);
        mActiveScene->clearContent(UBGraphicsScene::clearItems);
        updateActionStates();
    }
}


void UBBoardController::clearSceneAnnotation()
{
    if (mActiveScene)
    {
        mActiveScene->clearContent(UBGraphicsScene::clearAnnotations);
        updateActionStates();
    }
}

void UBBoardController::clearSceneBackground()
{
    if (mActiveScene)
    {
        mActiveScene->clearContent(UBGraphicsScene::clearBackground);
        updateActionStates();
    }
}

void UBBoardController::showDocumentsDialog()
{
    if (selectedDocument())
        persistCurrentScene();

    UBApplication::mainWindow->actionLibrary->setChecked(false);

}

void UBBoardController::libraryDialogClosed(int ret)
{
    Q_UNUSED(ret);

    mMainWindow->actionLibrary->setChecked(false);
}


void UBBoardController::blackout()
{
    UBApplication::applicationController->blackout();
}

void UBBoardController::showKeyboard(bool show)
{
    if(show)
        UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);

    if(UBSettings::settings()->useSystemOnScreenKeyboard->get().toBool())
        UBPlatformUtils::showOSK(show);
    else
        mPaletteManager->showVirtualKeyboard(show);
}


void UBBoardController::zoomIn(QPointF scenePoint)
{
    if (mControlView->transform().m11() > UB_MAX_ZOOM)
    {
        qApp->beep();
        return;
    }
    zoom(mZoomFactor, scenePoint);
}


void UBBoardController::zoomOut(QPointF scenePoint)
{
    if ((mControlView->horizontalScrollBar()->maximum() == 0) && (mControlView->verticalScrollBar()->maximum() == 0))
    {
        // Do not zoom out if we reached the maximum
        qApp->beep();
        return;
    }

    qreal newZoomFactor = 1 / mZoomFactor;

    zoom(newZoomFactor, scenePoint);
}


void UBBoardController::zoomRestore()
{
    QTransform tr;

    tr.scale(mSystemScaleFactor, mSystemScaleFactor);
    mControlView->setTransform(tr);

    centerRestore();

    emit zoomChanged(1.0);

    emit controlViewportChanged();
    mActiveScene->setBackgroundZoomFactor(mControlView->transform().m11());
}


void UBBoardController::centerRestore()
{
    // reset transformation and scrollbar values
    centerOn({0, 0});
    mControlView->centerOn({0, 0});

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    // workaround: foreground not repainted after scrolling on Qt5 (fixed in Qt6)
    // setForegroundBrush internally invokes the private function uopdateAll() unconditionally
    mControlView->setForegroundBrush(mControlView->foregroundBrush());
#endif

    persistViewPositionOnCurrentScene();
    UBApplication::applicationController->adjustDisplayView();
}


void UBBoardController::centerOn(QPointF scenePoint) const
{
    // centerOn without using scroll bars
    const auto before = mControlView->transform();

    // create a transformation with the same scaling where the scenePoint is in the center
    QTransform after;
    after.scale(before.m11(), before.m22());
    after.translate(-scenePoint.x(), -scenePoint.y());
    mControlView->setTransform(after);

    if (UBApplication::applicationController)
    {
        UBApplication::applicationController->adjustDisplayView();
    }
}


void UBBoardController::zoom(const qreal ratio, QPointF scenePoint)
{
    qreal currentZoom = ratio * mControlView->transform().m11() / mSystemScaleFactor;
    qreal usedRatio = ratio;

    if (currentZoom > UB_MAX_ZOOM)
    {
        currentZoom = UB_MAX_ZOOM;
        usedRatio = currentZoom * mSystemScaleFactor / mControlView->transform().m11();
    }

    /*
     * The shiftFactor is calculated from the condition that the scenePoint should have the
     * same coordinates on the view after zooming. Let m11, m31 be the transformation parameters
     * before zoom and m11', m31' the parameters after zoom. The equations for scenePoint.x:
     *   m11' = m11 * ratio
     *   x * m11 + m31 = x * m11' + m31'
     * We now solve this equation to get the additional translation m31' - m31:
     *   m31' - m31 = x * (m11 - m11')
     *              = x * m11 * (1 - ratio)
     *              = x * m11' * (1 - ratio) / ratio
     * The translate function works in scene coordinates and multiplies its parameter internally
     * by the scale factor m11', so we omit this factor in the function call below.
     */
    const auto shiftFactor = (1 - usedRatio) / usedRatio;
    mControlView->scale(usedRatio, usedRatio);
    mControlView->translate(scenePoint.x() * shiftFactor, scenePoint.y() * shiftFactor);

    emit zoomChanged(currentZoom);
    UBApplication::applicationController->adjustDisplayView();

    emit controlViewportChanged();
    mActiveScene->setBackgroundZoomFactor(mControlView->transform().m11());
}


void UBBoardController::handScroll(qreal dx, qreal dy)
{
    qreal antiScaleRatio = 1/(mSystemScaleFactor * currentZoom());
    mControlView->translate(dx*antiScaleRatio, dy*antiScaleRatio);

    UBApplication::applicationController->adjustDisplayView();

    emit controlViewportChanged();
}

void UBBoardController::persistViewPositionOnCurrentScene() const
{
    if (mActiveScene)
    {
        // calculate center from transformation
        const QPointF viewRelativeCenter = mControlView->transform().inverted().map(QPointF{0, 0});
        UBGraphicsScene::SceneViewState viewState
        {
            mControlView->transform().m11() / mSystemScaleFactor,
            mControlView->horizontalScrollBar()->value(),
            mControlView->verticalScrollBar()->value(),
            viewRelativeCenter
        };

        mActiveScene->setViewState(viewState);
    }
}

void UBBoardController::restoreViewPositionOnCurrentScene() const
{
    if (mActiveScene)
    {
        const auto viewState = mActiveScene->viewState();
        mControlView->horizontalScrollBar()->setValue(viewState.horizontalPosition);
        mControlView->verticalScrollBar()->setValue(viewState.verticalPostition);
        QTransform transform;
        double scale = viewState.zoomFactor * mSystemScaleFactor;
        transform.scale(scale, scale);
        mControlView->setTransform(transform);
        centerOn(viewState.mLastSceneCenter);
    }
}

void UBBoardController::previousScene()
{
    if (mActiveSceneIndex > 0)
    {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        setActiveDocumentScene(mActiveSceneIndex - 1);
        QApplication::restoreOverrideCursor();
    }

    updateActionStates();
}


void UBBoardController::nextScene()
{
    if (mActiveSceneIndex < selectedDocument()->pageCount() - 1)
    {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        setActiveDocumentScene(mActiveSceneIndex + 1);
        QApplication::restoreOverrideCursor();
    }

    updateActionStates();
}


void UBBoardController::firstScene()
{
    if (mActiveSceneIndex > 0)
    {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        setActiveDocumentScene(0);
        QApplication::restoreOverrideCursor();
    }

    updateActionStates();
}


void UBBoardController::lastScene()
{
    if (mActiveSceneIndex < selectedDocument()->pageCount() - 1)
    {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        setActiveDocumentScene(selectedDocument()->pageCount() - 1);
        QApplication::restoreOverrideCursor();
    }

    updateActionStates();
}

void UBBoardController::downloadURL(const QUrl& url, QString contentSourceUrl, const QPointF& pPos, const QSize& pSize, bool isBackground, bool internalData)
{
    QString sUrl = url.toString();
    qDebug() << "something has been dropped on the board! Url is: " << sUrl.left(255);

    QGraphicsItem *oldBackgroundObject = NULL;
    if (isBackground)
        oldBackgroundObject = mActiveScene->backgroundObject();

    if(url.scheme() == "openboardtool")
    {
        downloadFinished(true, url, QUrl(), "application/openboard-tool", QByteArray(), pPos, pSize, isBackground);
    }
    else if (url.scheme() == "file" || url.scheme() == "")
    {
        QUrl formedUrl = url.scheme() == "file" ? url : QUrl::fromLocalFile(sUrl);
        QString fileName = formedUrl.toLocalFile();
        QString contentType = UBFileSystemUtils::mimeTypeFromFileName(fileName);

        // directly add local file to document without copying
        QFile file(fileName);
        QByteArray data;

        if (file.open(QIODevice::ReadOnly))
        {
            data = file.readAll();
        }

        downloadFinished(true, formedUrl, QUrl(), contentType, data, pPos, pSize, isBackground, internalData);
        file.close();
    }
    else
    {
        // When we fall there, it means that we are dropping something from the web to the board
        sDownloadFileDesc desc;
        desc.modal = true;
        desc.srcUrl = sUrl;
        desc.currentSize = 0;
        desc.name = url.scheme() == "data" ? "Local data" : url.fileName();
        desc.totalSize = 0; // The total size will be retrieved during the download
        desc.pos = pPos;
        desc.size = pSize;
        desc.isBackground = isBackground;

        UBDownloadManager::downloadManager()->addFileToDownload(desc);
    }

    if (isBackground && oldBackgroundObject != mActiveScene->backgroundObject())
    {
        if (mActiveScene->isURStackIsEnabled()) { //should be deleted after scene own undo stack implemented
            UBGraphicsItemUndoCommand* uc = new UBGraphicsItemUndoCommand(mActiveScene, oldBackgroundObject, mActiveScene->backgroundObject());
            UBApplication::undoStack->push(uc);
        }
    }


}


UBItem *UBBoardController::downloadFinished(bool pSuccess, QUrl sourceUrl, QUrl contentUrl, QString pContentTypeHeader,
                                            QByteArray pData, QPointF pPos, QSize pSize,
                                            bool isBackground, bool internalData)
{
    QString mimeType = pContentTypeHeader;

    // In some cases "image/jpeg;charset=" is retourned by the drag-n-drop. That is
    // why we will check if an ; exists and take the first part (the standard allows this kind of mimetype)
    if(mimeType.isEmpty())
      mimeType = UBFileSystemUtils::mimeTypeFromFileName(sourceUrl.toString());

    int position=mimeType.indexOf(";");
    if(position != -1)
        mimeType=mimeType.left(position);

    UBMimeType::Enum itemMimeType = UBFileSystemUtils::mimeTypeFromString(mimeType);

    if (!pSuccess)
    {
        UBApplication::showMessage(tr("Downloading content %1 failed").arg(sourceUrl.toString()));
        return NULL;
    }


    mActiveScene->deselectAllItems();
    const QString scheme = sourceUrl.scheme();

    if (scheme != "file" && scheme != "openboardtool" && scheme != "data")
        UBApplication::showMessage(tr("Download finished"));

    if (UBMimeType::RasterImage == itemMimeType)
    {

        qDebug() << "accepting mime type" << mimeType << "as raster image";

        if (pData.length() == 0)
        {
            QFile file(sourceUrl.toLocalFile());

            if (file.open(QFile::ReadOnly))
            {
                pData = file.readAll();
                file.close();
            }
        }

        UBGraphicsPixmapItem* pixItem = mActiveScene->addImage(pData, nullptr, pPos, 1.);

        if (scheme == "data")
        {
            // create a shorter, but still unique URL using a hash function
            QCryptographicHash hash(QCryptographicHash::Md5);
            hash.addData(sourceUrl.toString().toLatin1());
            QByteArray result = hash.result();
            QString hashedUrl = "md5:" + result.toBase64();
            pixItem->setSourceUrl(hashedUrl);
        }
        else
        {
            pixItem->setSourceUrl(sourceUrl);
        }

        if (isBackground)
        {
            mActiveScene->setAsBackgroundObject(pixItem, true);
        }
        else
        {
            mActiveScene->scaleToFitDocumentSize(pixItem, true, UBSettings::objectInControlViewMargin);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }

        return pixItem;
    }
    else if (UBMimeType::VectorImage == itemMimeType)
    {
        qDebug() << "accepting mime type" << mimeType << "as vector image";

        UBGraphicsSvgItem* svgItem = mActiveScene->addSvg(sourceUrl, pPos, pData);
        svgItem->setSourceUrl(sourceUrl);

        if (isBackground)
        {
            mActiveScene->setAsBackgroundObject(svgItem);
        }
        else
        {
            mActiveScene->scaleToFitDocumentSize(svgItem, true, UBSettings::objectInControlViewMargin);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }

        return svgItem;
    }
    else if (UBMimeType::AppleWidget == itemMimeType) //mime type invented by us :-(
    {
        qDebug() << "accepting mime type" << mimeType << "as Apple widget";

        QUrl widgetUrl = sourceUrl;

        if (pData.length() > 0)
        {
            widgetUrl = expandWidgetToTempDir(pData, "wdgt");
        }

        UBGraphicsWidgetItem* appleWidgetItem = mActiveScene->addAppleWidget(widgetUrl, pPos);

        appleWidgetItem->setSourceUrl(sourceUrl);

        if (isBackground)
        {
            mActiveScene->setAsBackgroundObject(appleWidgetItem);
        }
        else
        {
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }

        return appleWidgetItem;
    }
    else if (UBMimeType::W3CWidget == itemMimeType)
    {
        qDebug() << "accepting mime type" << mimeType << "as W3C widget";
        QUrl widgetUrl = sourceUrl;

        if (pData.length() > 0)
        {
            widgetUrl = expandWidgetToTempDir(pData);
        }

        UBGraphicsWidgetItem *w3cWidgetItem = addW3cWidget(widgetUrl, pPos);

        if (isBackground)
        {
            mActiveScene->setAsBackgroundObject(w3cWidgetItem);
        }
        else
        {
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }

        return w3cWidgetItem;
    }
    else if (UBMimeType::Video == itemMimeType)
    {
        qDebug() << "accepting mime type" << mimeType << "as video";

        UBGraphicsMediaItem *mediaVideoItem = 0;
        QUuid uuid = QUuid::createUuid();
        if (pData.length() > 0)
        {
            QString destFile;
            bool b = UBPersistenceManager::persistenceManager()->addFileToDocument(selectedDocument(),
                sourceUrl.toString(),
                UBPersistenceManager::videoDirectory,
                uuid,
                destFile,
                &pData);
            if (!b)
            {
                UBApplication::showMessage(tr("Add file operation failed: file copying error"));
                return NULL;
            }

            QUrl url = QUrl::fromLocalFile(destFile);

            mediaVideoItem = mActiveScene->addMedia(url, false, pPos);
        }
        else
        {
            qDebug() << sourceUrl.toString();
            mediaVideoItem = addVideo(sourceUrl, false, pPos, true);
        }

        if(mediaVideoItem){
            if (contentUrl.isEmpty())
                mediaVideoItem->setSourceUrl(sourceUrl);
            else
                mediaVideoItem->setSourceUrl(contentUrl);
            mediaVideoItem->setUuid(uuid);
            connect(this, SIGNAL(activeSceneChanged()), mediaVideoItem, SLOT(activeSceneChanged()));
        }

        UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);

        return mediaVideoItem;
    }
    else if (UBMimeType::Audio == itemMimeType)
    {
        qDebug() << "accepting mime type" << mimeType << "as audio";

        UBGraphicsMediaItem *audioMediaItem = 0;

        QUuid uuid = QUuid::createUuid();
        if (pData.length() > 0)
        {
            QString destFile;
            bool b = UBPersistenceManager::persistenceManager()->addFileToDocument(selectedDocument(),
                sourceUrl.toString(),
                UBPersistenceManager::audioDirectory,
                uuid,
                destFile,
                &pData);
            if (!b)
            {
                UBApplication::showMessage(tr("Add file operation failed: file copying error"));
                return NULL;
            }

            QUrl url = QUrl::fromLocalFile(destFile);

            audioMediaItem = mActiveScene->addMedia(url, false, pPos);
        }
        else
        {
            audioMediaItem = addAudio(sourceUrl, false, pPos, true);
        }

        if(audioMediaItem){
            if (contentUrl.isEmpty())
                audioMediaItem->setSourceUrl(sourceUrl);
            else
                audioMediaItem->setSourceUrl(contentUrl);
            audioMediaItem->setUuid(uuid);
            connect(this, SIGNAL(activeSceneChanged()), audioMediaItem, SLOT(activeSceneChanged()));
        }

        UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);

        return audioMediaItem;
    }
    else if (UBMimeType::Flash == itemMimeType)
    {

        qDebug() << "accepting mime type" << mimeType << "as flash";

        QString sUrl = sourceUrl.toString();

        if (sUrl.startsWith("file://") || sUrl.startsWith("/"))
        {
            sUrl = sourceUrl.toLocalFile();
        }

        QSize size;

        if (pSize.height() > 0 && pSize.width() > 0)
            size = pSize;
        else
            size = mActiveScene->nominalSize() * .8;

        Q_UNUSED(internalData)

        QString widgetUrl = UBGraphicsW3CWidgetItem::createNPAPIWrapper(sUrl, mimeType, size);
        UBFileSystemUtils::deleteFile(sourceUrl.toLocalFile());
        emit npapiWidgetCreated(widgetUrl);

        if (widgetUrl.length() > 0)
        {
            UBGraphicsWidgetItem *widgetItem = mActiveScene->addW3CWidget(QUrl::fromLocalFile(widgetUrl), pPos);
            widgetItem->setUuid(QUuid::createUuid());
            widgetItem->setSourceUrl(QUrl::fromLocalFile(widgetUrl));
            qDebug() << widgetItem->getOwnFolder();
            qDebug() << widgetItem->getSnapshotPath();

            widgetItem->setSnapshotPath(widgetItem->getOwnFolder());

            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);

            return widgetItem;
        }
    }
    else if (UBMimeType::PDF == itemMimeType)
    {
        qDebug() << "accepting mime type" << mimeType << "as PDF";
        qDebug() << "pdf data length: " << pData.size();
        qDebug() << "sourceurl : " + sourceUrl.toString();
        QString sUrl = sourceUrl.toString();

        int numberOfImportedDocuments = 0;

        if (!sourceUrl.isEmpty() && (sUrl.startsWith("file://") || sUrl.startsWith("/")))
        {
            QStringList fileNames;
            fileNames << sourceUrl.toLocalFile();
            numberOfImportedDocuments = UBDocumentManager::documentManager()->addFilesToDocument(selectedDocument(), fileNames);
        }
        else if(pData.size()){
            QTemporaryFile pdfFile("XXXXXX.pdf");
            if (pdfFile.open())
            {
                pdfFile.write(pData);
                pdfFile.close();
                QStringList fileNames;
                fileNames << pdfFile.fileName();
                numberOfImportedDocuments = UBDocumentManager::documentManager()->addFilesToDocument(selectedDocument(), fileNames);
            }
        }

        if (numberOfImportedDocuments > 0)
        {
            QDateTime now = QDateTime::currentDateTime();
            selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));
            updateActionStates();
        }
    }
    else if (UBMimeType::OpenboardTool == itemMimeType)
    {
        qDebug() << "accepting mime type" << mimeType << "OpenBoard Tool";

        if (sourceUrl.toString() == UBToolsManager::manager()->compass.id)
        {
            mActiveScene->addCompass(pPos);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }
        else if (sourceUrl.toString() == UBToolsManager::manager()->ruler.id)
        {
            mActiveScene->addRuler(pPos);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }
        else if (sourceUrl.toString() == UBToolsManager::manager()->axes.id)
        {
            mActiveScene->addAxes(pPos);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }
        else if (sourceUrl.toString() == UBToolsManager::manager()->protractor.id)
        {
            mActiveScene->addProtractor(pPos);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }
        else if (sourceUrl.toString() == UBToolsManager::manager()->triangle.id)
        {
            mActiveScene->addTriangle(pPos);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }
        else if (sourceUrl.toString() == UBToolsManager::manager()->cache.id)
        {
            mActiveScene->addCache();
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }
        else if (sourceUrl.toString() == UBToolsManager::manager()->magnifier.id)
        {
            UBMagnifierParams params;
            params.x = controlContainer()->geometry().width() / 2;
            params.y = controlContainer()->geometry().height() / 2;
            params.zoom = 2;
            params.sizePercentFromScene = 20;
            mActiveScene->addMagnifier(params);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }
        else if (sourceUrl.toString() == UBToolsManager::manager()->mask.id)
        {
            mActiveScene->addMask(pPos);
            UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
        }
        else
        {
            UBApplication::showMessage(tr("Unknown tool type %1").arg(sourceUrl.toString()));
        }
    }
    else if (UBMimeType::Html == itemMimeType)
    {
        if (!mEmbedController)
        {
            mEmbedController = new UBEmbedController(mControlView);
        }

        static const QRegularExpression matchTitle("<title>([^<]*)</title>");

        QRegularExpressionMatch match = matchTitle.match(pData);
        QString title = match.hasMatch() ? match.captured(1) : tr("Untitled");

        mEmbedController->pageTitleChanged(title);
        mEmbedController->pageUrlChanged(sourceUrl);
        mEmbedController->showEmbedDialog();

        UBEmbedParser* parser = new UBEmbedParser(this);
        connect(parser, &UBEmbedParser::parseResult, this, [this,parser](bool hasEmbeddedContent){
            QList<UBEmbedContent> list = parser->embeddedContent();
            mEmbedController->updateListOfEmbeddableContent(list);
            parser->deleteLater();
        });

        parser->parse(pData);
    }
    else if (UBMimeType::Document == itemMimeType)
    {
        QString documentFolderName = sourceUrl.toString().section('/', -2, -2); //section before "/metadata.rdf" is documentFolderName

        std::shared_ptr<UBDocumentProxy> document = UBPersistenceManager::persistenceManager()->mDocumentTreeStructureModel->findDocumentByFolderName(documentFolderName);

        if (document)
        {
            setActiveDocumentScene(document, document->lastVisitedSceneIndex());
        }
        else
        {
            UBApplication::showMessage(tr("Could not find document."));
        }
    }
    else
    {
        UBApplication::showMessage(tr("Unknown content type %1").arg(pContentTypeHeader));
        qWarning() << "ignoring mime type" << pContentTypeHeader ;
    }

    return NULL;
}

std::shared_ptr<UBGraphicsScene> UBBoardController::setActiveDocumentScene(int pSceneIndex)
{
    return setActiveDocumentScene(selectedDocument(), pSceneIndex);
}

std::shared_ptr<UBGraphicsScene> UBBoardController::setActiveDocumentScene(std::shared_ptr<UBDocumentProxy> pDocumentProxy, const int pSceneIndex, bool forceReload, bool onImport)
{
    UBApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    persistViewPositionOnCurrentScene();

    bool documentChange = selectedDocument() != pDocumentProxy;

    int index = pSceneIndex;
    int sceneCount = pDocumentProxy->pageCount();
    if (index >= sceneCount && sceneCount > 0)
        index = sceneCount - 1;

    std::shared_ptr<UBGraphicsScene> targetScene = UBPersistenceManager::persistenceManager()->loadDocumentScene(pDocumentProxy, index);

    bool sceneChange = targetScene != mActiveScene;

    if (targetScene)
    {
        if (mActiveScene && !onImport)
        {
            persistCurrentScene();
            freezeW3CWidgets(true);
            ClearUndoStack();
        }else
        {
            UBApplication::undoStack->clear();
        }

        mActiveScene = targetScene;
        mActiveSceneIndex = index;

        setDocument(pDocumentProxy, forceReload);

        updateSystemScaleFactor();

        if (mControlView->scene())
        {
            disconnect(UBApplication::undoStack.data(), SIGNAL(indexChanged(int)), mControlView->scene().get(), SLOT(updateSelectionFrameWrapper(int)));
        }

        mControlView->setScene(mActiveScene.get());
        connect(UBApplication::undoStack.data(), SIGNAL(indexChanged(int)), mControlView->scene().get(), SLOT(updateSelectionFrameWrapper(int)));

        mDisplayView->setScene(mActiveScene.get());
        mActiveScene->setBackgroundZoomFactor(mControlView->transform().m11());
        pDocumentProxy->setDefaultDocumentSize(mActiveScene->nominalSize());
        updatePageSizeState();

        adjustDisplayViews();

        UBSettings::settings()->setDarkBackground(mActiveScene->isDarkBackground());
        UBSettings::settings()->setPageBackground(mActiveScene->pageBackground());

        freezeW3CWidgets(false);

        selectionChanged();

        updateBackgroundActionsState(mActiveScene->isDarkBackground(), mActiveScene->pageBackground());

        if (documentChange)
        {
            UBGraphicsTextItem::lastUsedTextColor = QColor(Qt::black);
        }

        if (sceneChange)
        {
            emit activeSceneChanged();
        }

        pDocumentProxy->setLastVisitedSceneIndex(mActiveSceneIndex);
        UBSettings::settings()->appLastSessionDocumentUUID->set(
                UBStringUtils::toCanonicalUuid(pDocumentProxy->uuid()));

        UBFeaturesController* featuresController = paletteManager()->featuresWidget()->getFeaturesController();

        QUrl url = QUrl::fromLocalFile(pDocumentProxy->persistencePath() + "/metadata.rdf");
        QString documentFolderName = pDocumentProxy->documentFolderName();

        if (!featuresController->isDocumentInFavoriteList(documentFolderName) && !featuresController->isInRecentlyOpenDocuments(documentFolderName))
        {
            featuresController->addToFavorite(url, pDocumentProxy->name(), true);

            // keep recent UBDocument instances alive for fast switching
            auto document = UBDocument::getDocument(pDocumentProxy);

            if (!mRecentDocuments.contains(document))
            {
                mRecentDocuments.append(UBDocument::getDocument(pDocumentProxy));
            }
        }

        auto document = UBDocument::getDocument(pDocumentProxy);
        document->thumbnailScene()->hightlightItem(mActiveSceneIndex, true);
    }
    else
    {
        qWarning() << "could not load document scene : '" << pDocumentProxy->persistencePath() << "', page index : " << pSceneIndex;
    }
    UBApplication::restoreOverrideCursor();

    return targetScene;
}

void UBBoardController::findUniquesItems(const QUndoCommand *parent, QSet<QGraphicsItem*> &items)
{
    if (parent->childCount()) {
        for (int i = 0; i < parent->childCount(); i++) {
            findUniquesItems(parent->child(i), items);
        }
    }

    // Undo command transaction macros. Process separatedly
    if (parent->text() == UBSettings::undoCommandTransactionName) {
        return;
    }

    const UBUndoCommand *undoCmd = static_cast<const UBUndoCommand*>(parent);
    if(undoCmd->getType() != UBUndoType::undotype_GRAPHICITEM)
        return;

    const UBGraphicsItemUndoCommand *cmd = dynamic_cast<const UBGraphicsItemUndoCommand*>(parent);

    // go through all added and removed objects, for create list of unique objects
    // grouped items will be deleted by groups, so we don't need do delete that items.
    QSetIterator<QGraphicsItem*> itAdded(cmd->GetAddedList());
    while (itAdded.hasNext())
    {
        QGraphicsItem* item = itAdded.next();
        if (!items.contains(item) &&
            !(item->parentItem() && UBGraphicsGroupContainerItem::Type == item->parentItem()->type()) &&
            !items.contains(item->parentItem())
            )
        {
            items.insert(item);
        }
    }

    QSetIterator<QGraphicsItem*> itRemoved(cmd->GetRemovedList());
    while (itRemoved.hasNext())
    {
        QGraphicsItem* item = itRemoved.next();
        if (!items.contains(item) &&
            !(item->parentItem() && UBGraphicsGroupContainerItem::Type == item->parentItem()->type()) &&
            !items.contains(item->parentItem())
            )
        {
            items.insert(item);
        }
    }
}

void UBBoardController::ClearUndoStack()
{
    QSet<QGraphicsItem*> uniqueItems;
    // go through all stack command
    for (int i = 0; i < UBApplication::undoStack->count(); i++) {
        findUniquesItems(UBApplication::undoStack->command(i), uniqueItems);
    }

    // Get items from clipboard in order not to delete an item that was cut
    // (using source URL of graphics items as a surrogate for equality testing)
    // This ensures that we can cut and paste a media item, widget, etc. from one page to the next.
    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData* data = clipboard->mimeData();
    QList<QUrl> sourceURLs;

    if (data && data->hasFormat(UBApplication::mimeTypeUniboardPageItem)) {
        const UBMimeDataGraphicsItem* mimeDataGI = qobject_cast <const UBMimeDataGraphicsItem*>(data);

        if (mimeDataGI) {
            foreach (UBItem* sourceItem, mimeDataGI->items()) {
                sourceURLs << sourceItem->sourceUrl();
            }
        }
    }

    // go through all unique items, and check, if they are on scene, or not.
    // if not on scene, than item can be deleted
    QSetIterator<QGraphicsItem*> itUniq(uniqueItems);
    while (itUniq.hasNext())
    {
        QGraphicsItem* item = itUniq.next();
        UBGraphicsScene* scene = nullptr;
        if (item->scene()) {
            scene = dynamic_cast<UBGraphicsScene*>(item->scene());
        }

        bool inClipboard = false;
        UBItem* ubi = dynamic_cast<UBItem*>(item);
        if (ubi && sourceURLs.contains(ubi->sourceUrl()))
            inClipboard = true;

        if(!scene && !inClipboard)
        {
            if (!mActiveScene->deleteItem(item)){
                delete item;
                item = 0;
            }
        }
    }

    // clear stack, and command list
    UBApplication::undoStack->clear();
}

void UBBoardController::adjustDisplayViews()
{
    if (UBApplication::applicationController)
    {
        UBApplication::applicationController->adjustDisplayView();
        UBApplication::applicationController->adjustPreviousViews(mActiveSceneIndex, selectedDocument());
    }
}


int UBBoardController::autosaveTimeoutFromSettings() const
{
    int value = UBSettings::settings()->autoSaveInterval->get().toInt();
    int minute = 60 * 1000;

    return value * minute;
}

void UBBoardController::changeBackground(bool isDark, UBPageBackground pageBackground)
{
    bool currentIsDark = mActiveScene->isDarkBackground();
    UBPageBackground currentBackgroundType = mActiveScene->pageBackground();

    if ((isDark != currentIsDark) || (currentBackgroundType != pageBackground))
    {
        UBSettings::settings()->setDarkBackground(isDark);
        UBSettings::settings()->setPageBackground(pageBackground);

        mActiveScene->setBackground(isDark, pageBackground);

        emit backgroundChanged();
    }
}

void UBBoardController::boardViewResized(QResizeEvent* event)
{
    Q_UNUSED(event);

    int innerMargin = UBSettings::boardMargin;
    int userHeight = mControlContainer->height() - (2 * innerMargin);

    mMessageWindow->move(innerMargin, innerMargin + userHeight - mMessageWindow->height());
    mMessageWindow->adjustSizeAndPosition();

    UBApplication::applicationController->initViewState(
                mControlView->horizontalScrollBar()->value(),
                mControlView->verticalScrollBar()->value());

    updateSystemScaleFactor();

    mControlView->centerOn(0,0);

    if (mDisplayView && UBApplication::displayManager->hasDisplay()) {
        UBApplication::applicationController->adjustDisplayView();
        mDisplayView->centerOn(0,0);
        setBoxing(mDisplayView->geometry());
    }

    mPaletteManager->containerResized();

    updateZoomControl(currentZoom());
    positionZoomControl();
    positionUndoRedoControl();

    UBApplication::boardController->controlView()->scene()->moveMagnifier();

}


void UBBoardController::showMessage(const QString& message, bool showSpinningWheel)
{
    mMessageWindow->showMessage(message, showSpinningWheel);
}


void UBBoardController::hideMessage()
{
    mMessageWindow->hideMessage();
}


void UBBoardController::setDisabled(bool disable)
{
    mMainWindow->boardToolBar->setDisabled(disable);
    mControlView->setDisabled(disable);
    if (mUndoRedoControl)
        mUndoRedoControl->setDisabled(disable);
}


void UBBoardController::selectionChanged()
{
    updateActionStates();
    emit pageSelectionChanged(activeSceneIndex());
}


void UBBoardController::undoRedoStateChange(bool canUndo)
{
    Q_UNUSED(canUndo);

    mMainWindow->actionUndo->setEnabled(UBApplication::undoStack->canUndo());
    mMainWindow->actionRedo->setEnabled(UBApplication::undoStack->canRedo());

    updateActionStates();
}


void UBBoardController::updateActionStates()
{
    mMainWindow->actionBack->setEnabled(selectedDocument() && (mActiveSceneIndex > 0));
    mMainWindow->actionForward->setEnabled(selectedDocument() && (mActiveSceneIndex < selectedDocument()->pageCount() - 1));
    mMainWindow->actionErase->setEnabled(mActiveScene && !mActiveScene->isEmpty());
}


std::shared_ptr<UBGraphicsScene> UBBoardController::activeScene() const
{
    return mActiveScene;
}


int UBBoardController::activeSceneIndex() const
{
    return mActiveSceneIndex;
}

void UBBoardController::setActiveSceneIndex(int i)
{
    mActiveSceneIndex = i;
}

void UBBoardController::documentSceneChanged(std::shared_ptr<UBDocumentProxy> pDocumentProxy, int pIndex)
{
    Q_UNUSED(pIndex);

    if(selectedDocument() == pDocumentProxy)
    {
        setActiveDocumentScene(mActiveSceneIndex);
    }
}

void UBBoardController::autosaveTimeout()
{
    if (UBApplication::applicationController->displayMode() != UBApplicationController::Board) {
        //perform autosave only in board mode
        return;
    }

    saveData(sf_showProgress);
    UBSettings::settings()->save();
}

void UBBoardController::appMainModeChanged(UBApplicationController::MainMode md)
{
    int autoSaveInterval = autosaveTimeoutFromSettings();
    if (!autoSaveInterval) {
        return;
    }

    if (!mAutosaveTimer) {
        mAutosaveTimer = new QTimer(this);
        connect(mAutosaveTimer, SIGNAL(timeout()), this, SLOT(autosaveTimeout()));
    }

    if (md == UBApplicationController::Board) {
        mAutosaveTimer->start(autoSaveInterval);
    } else if (mAutosaveTimer->isActive()) {
        mAutosaveTimer->stop();
    }
}

void UBBoardController::closing()
{
    mIsClosing = true;
    lastWindowClosed();
    ClearUndoStack();
#ifdef Q_OS_OSX
    if (!UBPlatformUtils::errorOpeningVirtualKeyboard)
        showKeyboard(false);
#else
        showKeyboard(false);
#endif
}

void UBBoardController::lastWindowClosed()
{
    if (!mCleanupDone)
    {
        if (initialDocumentScene() && initialDocumentScene()->document())
        {
            if (initialDocumentScene()->isEmpty() && (initialDocumentScene()->document()->documentDate() == initialDocumentScene()->document()->lastUpdate()))
            {
                // intial scene or document have not been modified at all, so we can delete the document.
                UBPersistenceManager::persistenceManager()->deleteDocument(initialDocumentScene()->document());

                //if current scene is not the initial document scene, we still need to persist it to ensure no data can be lost this way
                if (activeScene() != initialDocumentScene())
                {
                    persistCurrentScene();
                    UBPersistenceManager::persistenceManager()->persistDocumentMetadata(selectedDocument());
                }
            }
            else
            {
                // if intial scene or document changed, then rather the initial document scene is the current scene,
                // or current scene changed and initial document scene has already been persisted.
                // Now, we just persist the current scene before closing the app, to ensure no data can be lost this way.
                persistCurrentScene();
                UBPersistenceManager::persistenceManager()->persistDocumentMetadata(selectedDocument());
            }
        }
        else
        { //should not happen ?
            persistCurrentScene();
            UBPersistenceManager::persistenceManager()->persistDocumentMetadata(selectedDocument());
        }

        mCleanupDone = true;
    }
}



void UBBoardController::setColorIndex(int pColorIndex)
{
    UBDrawingController::drawingController()->setColorIndex(pColorIndex);

    if (UBDrawingController::drawingController()->stylusTool() != UBStylusTool::Marker &&
            UBDrawingController::drawingController()->stylusTool() != UBStylusTool::Line &&
            UBDrawingController::drawingController()->stylusTool() != UBStylusTool::Text &&
            UBDrawingController::drawingController()->stylusTool() != UBStylusTool::Selector)
    {
        UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Pen);
    }

    if (UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Pen ||
            UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Line ||
            UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Text ||
            UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Selector)
    {
        mPenColorOnDarkBackground = UBSettings::settings()->penColors(true).at(pColorIndex);
        mPenColorOnLightBackground = UBSettings::settings()->penColors(false).at(pColorIndex);

        if (UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Selector)
        {
            // If we are in mode board, then do that
            if(UBApplication::applicationController->displayMode() == UBApplicationController::Board)
            {
                UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Pen);
                mMainWindow->actionPen->setChecked(true);
            }
        }

        emit penColorChanged();
    }
    else if (UBDrawingController::drawingController()->stylusTool() == UBStylusTool::Marker)
    {
        mMarkerColorOnDarkBackground = UBSettings::settings()->markerColors(true).at(pColorIndex);
        mMarkerColorOnLightBackground = UBSettings::settings()->markerColors(false).at(pColorIndex);
    }

    refreshPenVisuals();
}

void UBBoardController::chooseCustomColor()
{
    UBDrawingController *drawingController = UBDrawingController::drawingController();

    if (drawingController->stylusTool() != UBStylusTool::Marker &&
            drawingController->stylusTool() != UBStylusTool::Pen &&
            drawingController->stylusTool() != UBStylusTool::Line)
    {
        drawingController->setStylusTool(UBStylusTool::Pen);
    }

    const int colorIndex = drawingController->currentToolColorIndex();
    QColor initialColor = drawingController->currentToolColor();
    if (!initialColor.isValid())
        initialColor = Qt::black;

    QColorDialog colorDialog(initialColor, mMainWindow);
    colorDialog.setWindowTitle(tr("Choose a color"));
    colorDialog.setOption(QColorDialog::DontUseNativeDialog);

    // QColorDialog uses Qt's own translation catalogue. Deployed Windows
    // builds do not ship the complete Qt catalogue, so translate the visible
    // controls here to keep the Team Edition colour palette fully Chinese.
    const QHash<QString, QString> colorDialogTranslations = {
        {QStringLiteral("Basic colors"), QStringLiteral("基本颜色")},
        {QStringLiteral("Pick Screen Color"), QStringLiteral("拾取屏幕颜色")},
        {QStringLiteral("Custom colors"), QStringLiteral("自定义颜色")},
        {QStringLiteral("Add to Custom Colors"), QStringLiteral("添加到自定义颜色")},
        {QStringLiteral("Hue:"), QStringLiteral("色相：")},
        {QStringLiteral("Sat:"), QStringLiteral("饱和度：")},
        {QStringLiteral("Val:"), QStringLiteral("明度：")},
        {QStringLiteral("Red:"), QStringLiteral("红：")},
        {QStringLiteral("Green:"), QStringLiteral("绿：")},
        {QStringLiteral("Blue:"), QStringLiteral("蓝：")},
        {QStringLiteral("HTML:"), QStringLiteral("HTML：")},
        {QStringLiteral("Alpha channel:"), QStringLiteral("透明度：")},
        {QStringLiteral("OK"), QStringLiteral("确定")},
        {QStringLiteral("Cancel"), QStringLiteral("取消")}
    };

    const auto translatedText = [&colorDialogTranslations](QString text) {
        text.remove(QLatin1Char('&'));
        return colorDialogTranslations.value(text, QString());
    };

    for (QLabel *label : colorDialog.findChildren<QLabel*>()) {
        const QString translation = translatedText(label->text());
        if (!translation.isEmpty())
            label->setText(translation);
    }

    for (QAbstractButton *button : colorDialog.findChildren<QAbstractButton*>()) {
        const QString translation = translatedText(button->text());
        if (!translation.isEmpty())
            button->setText(translation);
    }

    if (colorDialog.exec() != QDialog::Accepted)
        return;

    const QColor selectedColor = colorDialog.selectedColor();

    if (!selectedColor.isValid())
        return;

    if (drawingController->stylusTool() == UBStylusTool::Marker)
    {
        drawingController->setMarkerColor(false, selectedColor, colorIndex);
        drawingController->setMarkerColor(true, selectedColor, colorIndex);
    }
    else
    {
        drawingController->setPenColor(false, selectedColor, colorIndex);
        drawingController->setPenColor(true, selectedColor, colorIndex);
    }

    setColorIndex(colorIndex);
}

void UBBoardController::colorPaletteChanged()
{
    mPenColorOnDarkBackground = UBSettings::settings()->penColor(true);
    mPenColorOnLightBackground = UBSettings::settings()->penColor(false);
    mMarkerColorOnDarkBackground = UBSettings::settings()->markerColor(true);
    mMarkerColorOnLightBackground = UBSettings::settings()->markerColor(false);
    refreshPenVisuals();
}

void UBBoardController::refreshPenVisuals()
{
    UBResources *resources = UBResources::resources();
    resources->updatePenColor(UBSettings::settings()->currentPenColor());
    resources->updateMarkerColor(UBSettings::settings()->currentMarkerColor());

    const bool desktopMode = UBApplication::applicationController
            && UBApplication::applicationController->isShowingDesktop();
    if (mMainWindow && mMainWindow->actionPen)
        mMainWindow->actionPen->setIcon(resources->coloredPenIcon(desktopMode));
    if (mMainWindow && mMainWindow->actionMarker)
        mMainWindow->actionMarker->setIcon(resources->coloredMarkerIcon(desktopMode));

    const int tool = UBDrawingController::drawingController()->stylusTool();
    if (tool == UBStylusTool::Pen || tool == UBStylusTool::Line
            || tool == UBStylusTool::Marker)
        setToolCursor(tool);
}


qreal UBBoardController::currentZoom() const
{
    if (mControlView)
        return mControlView->transform().m11() / mSystemScaleFactor;
    else
        return 1.0;
}

void UBBoardController::removeTool(UBToolWidget* toolWidget)
{
    toolWidget->remove();
}

void UBBoardController::hide()
{
    UBApplication::mainWindow->actionLibrary->setChecked(false);
}

void UBBoardController::show()
{
    UBApplication::mainWindow->actionLibrary->setChecked(false);

    if (mSwitchToSceneIndex >= 0)
    {
        setActiveDocumentScene(mSwitchToSceneIndex);
        mSwitchToSceneIndex = -1;
    }
    else
    {
        setActiveDocumentScene(mActiveSceneIndex);
    }
}

void UBBoardController::persistCurrentScene(bool isAnAutomaticBackup, bool forceImmediateSave)
{
    if(UBPersistenceManager::persistenceManager()
            && selectedDocument() && mActiveScene && mActiveSceneIndex != mDeletingSceneIndex
            && (mActiveSceneIndex >= 0) && mActiveSceneIndex != mMovingSceneIndex)
    {
        mActiveScene->saveWidgetSnapshots();

        if (mActiveScene->isModified())
        {
            auto document = UBDocument::getDocument(selectedDocument());
            document->persistPage(mActiveScene, mActiveSceneIndex, isAnAutomaticBackup, forceImmediateSave);
        }
    }
}

void UBBoardController::updateSystemScaleFactor()
{
    if (mActiveScene)
    {
        qreal newScaleFactor = 1.0;
        QSize pageNominalSize = mActiveScene->nominalSize();
        // disabled: we're going to keep scale factor untouched if the size is custom
        QMap<DocumentSizeRatio::Enum, QSize> sizesMap = UBSettings::settings()->documentSizes;
      //  if(pageNominalSize == sizesMap.value(DocumentSizeRatio::Ratio16_9) || pageNominalSize == sizesMap.value(DocumentSizeRatio::Ratio4_3))
        {
            qreal hFactor = ((qreal)controlView()->size().width()) / ((qreal)pageNominalSize.width());
            qreal vFactor = ((qreal)controlView()->size().height()) / ((qreal)pageNominalSize.height());

            newScaleFactor = qMin(hFactor, vFactor);
        }

        if (mSystemScaleFactor != newScaleFactor)
            mSystemScaleFactor = newScaleFactor;

        restoreViewPositionOnCurrentScene();
        mActiveScene->setBackgroundZoomFactor(mControlView->transform().m11());
    }
    else
    {
        mSystemScaleFactor = 1.0;
    }
}


void UBBoardController::setWidePageSize(bool checked)
{
    Q_UNUSED(checked);
    QSize newSize = UBSettings::settings()->documentSizes.value(DocumentSizeRatio::Ratio16_9);

    if (mActiveScene->nominalSize() != newSize)
    {
        UBPageSizeUndoCommand* uc = new UBPageSizeUndoCommand(mActiveScene, mActiveScene->nominalSize(), newSize);
        UBApplication::undoStack->push(uc);

        setPageSize(newSize);
    }
}


void UBBoardController::setRegularPageSize(bool checked)
{
    Q_UNUSED(checked);
    QSize newSize = UBSettings::settings()->documentSizes.value(DocumentSizeRatio::Ratio4_3);

    if (mActiveScene->nominalSize() != newSize)
    {
        UBPageSizeUndoCommand* uc = new UBPageSizeUndoCommand(mActiveScene, mActiveScene->nominalSize(), newSize);
        UBApplication::undoStack->push(uc);

        setPageSize(newSize);
    }
}


void UBBoardController::setPageSize(QSize newSize)
{
    if (mActiveScene->nominalSize() != newSize)
    {
        mActiveScene->setNominalSize(newSize);

        persistViewPositionOnCurrentScene();

        updateSystemScaleFactor();
        updatePageSizeState();
        adjustDisplayViews();
        QDateTime now = QDateTime::currentDateTime();
        selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));

        UBSettings::settings()->pageSize->set(newSize);
    }
}

void UBBoardController::notifyCache(bool visible)
{
    if(visible)
        emit cacheEnabled();

    mCacheWidgetIsEnabled = visible;
}

void UBBoardController::updatePageSizeState()
{
    if (mActiveScene->nominalSize() == UBSettings::settings()->documentSizes.value(DocumentSizeRatio::Ratio16_9))
    {
        mMainWindow->actionWidePageSize->setChecked(true);
    }
    else if(mActiveScene->nominalSize() == UBSettings::settings()->documentSizes.value(DocumentSizeRatio::Ratio4_3))
    {
        mMainWindow->actionRegularPageSize->setChecked(true);
    }
    else
    {
        mMainWindow->actionCustomPageSize->setChecked(true);
    }
}


void UBBoardController::stylusToolChanged(int tool)
{
    if (UBPlatformUtils::hasVirtualKeyboard() && mPaletteManager->mKeyboardPalette)
    {
        UBStylusTool::Enum eTool = (UBStylusTool::Enum)tool;
        if(eTool != UBStylusTool::Selector && eTool != UBStylusTool::Text)
        {
            if(mPaletteManager->mKeyboardPalette->m_isVisible)
            {
#ifdef Q_OS_OSX
                if (!UBPlatformUtils::errorOpeningVirtualKeyboard)
                    UBApplication::mainWindow->actionVirtualKeyboard->activate(QAction::Trigger);
#else
                UBApplication::mainWindow->actionVirtualKeyboard->activate(QAction::Trigger);
#endif
            }
        }
    }

}


QUrl UBBoardController::expandWidgetToTempDir(const QByteArray& pZipedData, const QString& ext)
{
    QUrl widgetUrl;
    QTemporaryFile tmp;

    if (tmp.open())
    {
        tmp.write(pZipedData);
        tmp.flush();
        tmp.close();

        QString tmpDir = UBFileSystemUtils::createTempDir() + "." + ext;

        if (UBFileSystemUtils::expandZipToDir(tmp, tmpDir))
        {
            widgetUrl = QUrl::fromLocalFile(tmpDir);
        }
    }

    return widgetUrl;
}


void UBBoardController::grabScene(const QRectF& pSceneRect)
{
    if (mActiveScene)
    {
        /*
         * To get the pixel size on screen for the screenshot
         * we use the system scale factor to align to the pixels
         * and use an additional factor of two to provide more details
         */
        const auto scalingFactor = 2. * mSystemScaleFactor;
        const auto pixelSize = pSceneRect.size() * scalingFactor;
        QImage image(pixelSize.toSize(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);

        QRectF targetRect{{0, 0}, pixelSize};
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::LosslessImageRendering);

        mActiveScene->setRenderingContext(UBGraphicsScene::NonScreen);
        mActiveScene->setRenderingQuality(UBItem::RenderingQualityHigh, UBItem::CacheNotAllowed);

        mActiveScene->render(&painter, targetRect, pSceneRect);

        mActiveScene->setRenderingContext(UBGraphicsScene::Screen);
//        mActiveScene->setRenderingQuality(UBItem::RenderingQualityNormal);
        mActiveScene->setRenderingQuality(UBItem::RenderingQualityHigh, UBItem::CacheAllowed);

        mPaletteManager->addItem(QPixmap::fromImage(image), QPointF{}, 1. / scalingFactor);
        QDateTime now = QDateTime::currentDateTime();
        selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));
    }
}

UBGraphicsMediaItem* UBBoardController::addVideo(const QUrl& pSourceUrl, bool startPlay, const QPointF& pos, bool bUseSource)
{
    QUuid uuid = QUuid::createUuid();
    QUrl concreteUrl = pSourceUrl;

    // media file is not in document folder yet
    if (!bUseSource)
    {
        QString destFile;
        bool b = UBPersistenceManager::persistenceManager()->addFileToDocument(selectedDocument(),
                    pSourceUrl.toLocalFile(),
                    UBPersistenceManager::videoDirectory,
                    uuid,
                    destFile);
        if (!b)
        {
            UBApplication::showMessage(tr("Add file operation failed: file copying error"));
            return NULL;
        }
        concreteUrl = QUrl::fromLocalFile(destFile);
    }// else we just use source Url.


    UBGraphicsMediaItem* vi = mActiveScene->addMedia(concreteUrl, startPlay, pos);
    QDateTime now  = QDateTime::currentDateTime();
    selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));

    if (vi) {
        vi->setUuid(uuid);
        vi->setSourceUrl(pSourceUrl);
    }

    return vi;

}

UBGraphicsMediaItem* UBBoardController::addAudio(const QUrl& pSourceUrl, bool startPlay, const QPointF& pos, bool bUseSource)
{
    QUuid uuid = QUuid::createUuid();
    QUrl concreteUrl = pSourceUrl;

    // media file is not in document folder yet
    if (!bUseSource)
    {
        QString destFile;
        bool b = UBPersistenceManager::persistenceManager()->addFileToDocument(selectedDocument(),
            pSourceUrl.toLocalFile(),
            UBPersistenceManager::audioDirectory,
            uuid,
            destFile);
        if (!b)
        {
            UBApplication::showMessage(tr("Add file operation failed: file copying error"));
            return NULL;
        }
        concreteUrl = QUrl::fromLocalFile(destFile);
    }// else we just use source Url.

    UBGraphicsMediaItem* ai = mActiveScene->addMedia(concreteUrl, startPlay, pos);
    QDateTime now = QDateTime::currentDateTime();
    selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));

    if (ai){
        ai->setUuid(uuid);
        ai->setSourceUrl(pSourceUrl);
    }

    return ai;

}

UBGraphicsWidgetItem *UBBoardController::addW3cWidget(const QUrl &pUrl, const QPointF &pos)
{
    UBGraphicsWidgetItem* w3cWidgetItem = 0;

    QUuid uuid = QUuid::createUuid();

    QString destPath;
    if (!UBPersistenceManager::persistenceManager()->addGraphicsWidgetToDocument(selectedDocument(), pUrl.toLocalFile(), uuid, destPath))
        return NULL;
    QUrl newUrl = QUrl::fromLocalFile(destPath);

    w3cWidgetItem = mActiveScene->addW3CWidget(newUrl, pos);

    if (w3cWidgetItem) {
        w3cWidgetItem->setUuid(uuid);
        w3cWidgetItem->setOwnFolder(newUrl);
        w3cWidgetItem->setSourceUrl(pUrl);

        QString struuid = UBStringUtils::toCanonicalUuid(uuid);
        QString snapshotPath = selectedDocument()->persistencePath() +  "/" + UBPersistenceManager::widgetDirectory + "/" + struuid + ".png";
        w3cWidgetItem->setSnapshotPath(QUrl::fromLocalFile(snapshotPath));
    }

    return w3cWidgetItem;
}

void UBBoardController::cut()
{
    //---------------------------------------------------------//

    QList<QGraphicsItem*> selectedItems;
    foreach(QGraphicsItem* gi, mActiveScene->selectedItems())
        selectedItems << gi;

    //---------------------------------------------------------//

    QList<UBItem*> selected;
    foreach(QGraphicsItem* gi, selectedItems)
    {
        gi->setSelected(false);

        UBItem* ubItem = dynamic_cast<UBItem*>(gi);
        UBGraphicsItem *ubGi =  dynamic_cast<UBGraphicsItem*>(gi);

        if (ubItem && ubGi && !mActiveScene->tools().contains(gi))
        {
            selected << ubItem->deepCopy();
            ubGi->remove();
        }
    }

    //---------------------------------------------------------//

    if (selected.size() > 0)
    {
        QClipboard *clipboard = QApplication::clipboard();

        UBMimeDataGraphicsItem*  mimeGi = new UBMimeDataGraphicsItem(selected);

        mimeGi->setData(UBApplication::mimeTypeUniboardPageItem, QByteArray());
        clipboard->setMimeData(mimeGi);

        QDateTime now = QDateTime::currentDateTime();
        selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));
    }

    //---------------------------------------------------------//
}


void UBBoardController::copy()
{
    QList<UBItem*> selected;

    foreach(QGraphicsItem* gi, mActiveScene->selectedItems())
    {
        UBItem* ubItem = dynamic_cast<UBItem*>(gi);

        if (ubItem && !mActiveScene->tools().contains(gi))
            selected << ubItem;
    }

    if (selected.size() > 0)
    {
        QClipboard *clipboard = QApplication::clipboard();

        UBMimeDataGraphicsItem*  mimeGi = new UBMimeDataGraphicsItem(selected);

        mimeGi->setData(UBApplication::mimeTypeUniboardPageItem, QByteArray());
        clipboard->setMimeData(mimeGi);

    }
}


void UBBoardController::paste()
{
    QClipboard *clipboard = QApplication::clipboard();
    qreal xPosition = ((qreal)QRandomGenerator::global()->bounded(RAND_MAX)/(qreal)RAND_MAX) * 400;
    qreal yPosition = ((qreal)QRandomGenerator::global()->bounded(RAND_MAX)/(qreal)RAND_MAX) * 200;
    QPointF randomPos(xPosition -200 , yPosition - 100);
    QRect rect = mControlView->rect();
    QPoint center(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2);
    QPointF viewRelativeCenter = mControlView->mapToScene(center);

    processMimeData(clipboard->mimeData(), viewRelativeCenter + randomPos);

    QDateTime now = QDateTime::currentDateTime();
    selectedDocument()->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));
}


bool zLevelLessThan( UBItem* s1, UBItem* s2)
{
    qreal s1Zvalue = dynamic_cast<QGraphicsItem*>(s1)->data(UBGraphicsItemData::ItemOwnZValue).toReal();
    qreal s2Zvalue = dynamic_cast<QGraphicsItem*>(s2)->data(UBGraphicsItemData::ItemOwnZValue).toReal();
    return s1Zvalue < s2Zvalue;
}

void UBBoardController::processMimeData(const QMimeData* pMimeData, const QPointF& pPos)
{
    if (pMimeData->hasFormat(UBApplication::mimeTypeUniboardPageItem))
    {
        const UBMimeDataGraphicsItem* mimeData = qobject_cast <const UBMimeDataGraphicsItem*>(pMimeData);

        if (mimeData)
        {
            QList<UBItem*> items = mimeData->items();
            std::stable_sort(items.begin(),items.end(),zLevelLessThan);
            foreach(UBItem* item, items)
            {
                QGraphicsItem* pItem = dynamic_cast<QGraphicsItem*>(item);
                if(NULL != pItem){
                    duplicateItem(item);
                }
            }

            return;
        }
    }

    if(pMimeData->hasHtml())
    {
        QString qsHtml = pMimeData->html();
        QString url = UBApplication::urlFromHtml(qsHtml);

        if("" != url)
        {
            downloadURL(url, QString(), pPos);
            return;
        }
    }

    if (pMimeData->hasUrls())
    {
        QList<QUrl> urls = pMimeData->urls();

        int index = 0;

        const UBFeaturesMimeData *internalMimeData = qobject_cast<const UBFeaturesMimeData*>(pMimeData);
        bool internalData = false;
        if (internalMimeData) {
            internalData = true;
        }

        foreach(const QUrl url, urls){
            QPointF pos(pPos + QPointF(index * 15, index * 15));

            downloadURL(url, QString(), pos, QSize(), false,  internalData);
            index++;
        }

        return;
    }

    if (pMimeData->hasImage())
    {
        const QStringList formats = pMimeData->formats();
        QString selectedFormat;
        UBMimeType::Enum ubMimeType{UBMimeType::UNKNOWN};

        for (const QString& format : formats)
        {            
            ubMimeType = UBFileSystemUtils::mimeTypeFromString(format);

            if (ubMimeType == UBMimeType::VectorImage || ubMimeType == UBMimeType::RasterImage)
            {
                selectedFormat = format;
                break;
            }
        }

        QBuffer buffer;

        if (selectedFormat.isEmpty())
        {
            // should never happen, but just in case
            // create an image and fill the buffer with PNG data
            QImage img = qvariant_cast<QImage> (pMimeData->imageData());
            img.save(&buffer, "png");
            ubMimeType = UBMimeType::RasterImage;
        }
        else
        {
            // get data from mime data
            buffer.setData(pMimeData->data(selectedFormat));
        }

        if (ubMimeType == UBMimeType::VectorImage)
        {
            mActiveScene->addSvg({}, pPos, buffer.data());
            return;
        }
        else if (ubMimeType == UBMimeType::RasterImage)
        {
            // validate that the image is really an image, webkit does not fill properly the image mime data
            if (!buffer.data().isEmpty())
            {
                mActiveScene->addImage(buffer.data(), nullptr, pPos, 1.);
                return;
            }
        }
    }

    if (pMimeData->hasText())
    {
        if("" != pMimeData->text()){
            // Sometimes, it is possible to have an URL as text. we check here if it is the case
            QString qsTmp = pMimeData->text().remove(QChar('\0'));
            if(qsTmp.startsWith("http"))
                downloadURL(QUrl(qsTmp), QString(), pPos);
            else{
                if(mActiveScene->selectedItems().count() && mActiveScene->selectedItems().at(0)->type() == UBGraphicsItemType::TextItemType)
                    dynamic_cast<UBGraphicsTextItem*>(mActiveScene->selectedItems().at(0))->setHtml(pMimeData->text());
                else
                    mActiveScene->addTextHtml("", pPos)->setHtml(pMimeData->text());
            }
        }
        else{
#ifdef Q_OS_OSX
                //  With Safari, in 95% of the drops, the mime datas are hidden in Apple Web Archive pasteboard type.
                //  This is due to the way Safari is working so we have to dig into the pasteboard in order to retrieve
                //  the data.
                QString qsUrl = UBPlatformUtils::urlFromClipboard();
                if("" != qsUrl){
                    // We finally got the url of the dropped ressource! Let's import it!
                    downloadURL(qsUrl, qsUrl, pPos);
                    return;
                }
#endif
        }
    }
}


void UBBoardController::togglePodcast(bool checked)
{
    if (UBPodcastController::instance())
        UBPodcastController::instance()->toggleRecordingPalette(checked);
}

void UBBoardController::moveGraphicsWidgetToControlView(UBGraphicsWidgetItem* graphicsWidget)
{
    mActiveScene->setURStackEnable(false);
     UBGraphicsItem *toolW3C = duplicateItem(dynamic_cast<UBItem *>(graphicsWidget));
    UBGraphicsWidgetItem *copyedGraphicsWidget = NULL;

    if (toolW3C)
    {
        if (UBGraphicsWidgetItem::Type == toolW3C->type())
            copyedGraphicsWidget = static_cast<UBGraphicsWidgetItem *>(toolW3C);

        UBToolWidget *toolWidget = new UBToolWidget(copyedGraphicsWidget, mControlView);

        graphicsWidget->remove(false);
        mActiveScene->addItemToDeletion(graphicsWidget);

        mActiveScene->setURStackEnable(true);

        QPoint controlViewPos = mControlView->mapFromScene(graphicsWidget->sceneBoundingRect().center());
        toolWidget->centerOn(mControlView->mapTo(mControlContainer, controlViewPos));
        toolWidget->show();
    }
}


void UBBoardController::moveToolWidgetToScene(UBToolWidget* toolWidget)
{
    UBGraphicsWidgetItem *widgetToScene = toolWidget->toolWidget();

    widgetToScene->resetTransform();

    QPoint mainWindowCenter = toolWidget->mapTo(mMainWindow, QPoint(toolWidget->width(), toolWidget->height()) / 2);
    QPoint controlViewCenter = mControlView->mapFrom(mMainWindow, mainWindowCenter);
    QPointF scenePos = mControlView->mapToScene(controlViewCenter);

    widgetToScene->setWebActive(true);
    mActiveScene->addGraphicsWidget(widgetToScene, scenePos);

    toolWidget->remove();
}


void UBBoardController::updateBackgroundActionsState(bool isDark, UBPageBackground pageBackground)
{
    switch (pageBackground) {

        case UBPageBackground::crossed:
            if (isDark)
                mMainWindow->actionCrossedDarkBackground->setChecked(true);
            else
                mMainWindow->actionCrossedLightBackground->setChecked(true);
        break;

        case UBPageBackground::ruled:
        {
            QAction* actionRuledBackground = nullptr;
            if(UBSettings::settings()->isSeyesRuledBackground())
                if(isDark)
                    actionRuledBackground = mMainWindow->actionSeyesRuledDarkBackground;
                else
                    actionRuledBackground = mMainWindow->actionSeyesRuledLightBackground;
            else
                if(isDark)
                    actionRuledBackground = mMainWindow->actionRuledDarkBackground;
                else
                    actionRuledBackground = mMainWindow->actionRuledLightBackground;
            if(actionRuledBackground)
                actionRuledBackground->setChecked(true);
        }
        break;

        default:
            if (isDark)
                mMainWindow->actionPlainDarkBackground->setChecked(true);
            else
                mMainWindow->actionPlainLightBackground->setChecked(true);
        break;
    }
}


void UBBoardController::addItem()
{
    QString defaultPath = UBSettings::settings()->lastImportToLibraryPath->get().toString();

    QString extensions;
    foreach(QString ext, UBSettings::imageFileExtensions)
    {
        extensions += " *.";
        extensions += ext;
    }

    QString filename = QFileDialog::getOpenFileName(mControlContainer, tr("Add Item"),
                                                    defaultPath,
                                                    tr("All Supported (%1)").arg(extensions), NULL, QFileDialog::DontUseNativeDialog);

    if (filename.length() > 0)
    {
        mPaletteManager->addItem(QUrl::fromLocalFile(filename));
        QFileInfo source(filename);
        UBSettings::settings()->lastImportToLibraryPath->set(QVariant(source.absolutePath()));
    }
}

void UBBoardController::importPage()
{
    int pageCount = selectedDocument()->pageCount();
    if (UBApplication::documentController->addFileToDocument(selectedDocument()))
    {
        setActiveDocumentScene(selectedDocument(), pageCount, true);
    }
}

void UBBoardController::notifyPageChanged()
{
    emit activeSceneChanged();
}

void UBBoardController::onDownloadModalFinished()
{

}

void UBBoardController::displayMetaData(QMap<QString, QString> metadatas)
{
    emit displayMetadata(metadatas);
}

void UBBoardController::freezeW3CWidgets(bool freeze)
{
    if (mActiveSceneIndex >= 0)
    {
        QList<QGraphicsItem *> list = UBApplication::boardController->activeScene()->items();
        foreach(QGraphicsItem *item, list)
        {
            freezeW3CWidget(item, freeze);
        }
    }
}

void UBBoardController::freezeW3CWidget(QGraphicsItem *item, bool freeze)
{
    if (item->type() == UBGraphicsW3CWidgetItem::Type)
    {
        UBGraphicsWidgetItem* widget = qgraphicsitem_cast<UBGraphicsWidgetItem*>(item);
        widget->setWebActive(!freeze);
    }
}
