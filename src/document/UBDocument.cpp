/*
 * Copyright (C) 2015-2024 Département de l'Instruction Publique (DIP-SEM)
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


#include "UBDocument.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "adaptors/UBThumbnailAdaptor.h"
#include "core/UBApplication.h"
#include "core/UBPersistenceManager.h"
#include "document/UBDocumentController.h"
#include "gui/UBMainWindow.h"
#include "gui/UBThumbnailScene.h"

QList<std::weak_ptr<UBDocument>> UBDocument::sDocuments;

namespace
{
const QString pageNamesFileName{QStringLiteral("pageNames.json")};
}


UBDocument::UBDocument(std::shared_ptr<UBDocumentProxy> proxy)
    : mProxy(proxy)
    , mThumbnailScene(new UBThumbnailScene(this))
{
    loadPageNames();
    mThumbnailScene->createThumbnails();
}

UBDocument::~UBDocument()
{
    delete mThumbnailScene;
}

std::shared_ptr<UBDocumentProxy> UBDocument::proxy() const
{
    return mProxy;
}

void UBDocument::deletePages(QList<int> indexes)
{
    if (indexes.isEmpty())
    {
        return;
    }

    bool accepted{false};

    if (indexes.size() > 1)
    {
        accepted = UBApplication::mainWindow->yesNoQuestion(
            UBDocumentController::tr("Moving %1 pages of the document \"%2\" to trash")
                .arg(QString::number(indexes.size()), mProxy->name()),
            UBDocumentController::tr("You are about to move %1 pages of the document \"%2\" to trash. Are you sure ?")
                .arg(QString::number(indexes.size()), mProxy->name()),
            QPixmap(":/images/trash-document-page.png"));
    }
    else
    {
        accepted = UBApplication::mainWindow->yesNoQuestion(
            UBDocumentController::tr("Remove page %1").arg(indexes.at(0) + 1),
            UBDocumentController::tr("You are about to remove page %1 of the document \"%2\". Are you sure ?")
                .arg(indexes.at(0) + 1)
                .arg(mProxy->name()),
            QPixmap(":/images/trash-document-page.png"));
    }

    if (!accepted)
    {
        return;
    }

    std::sort(indexes.begin(), indexes.end());
    UBPersistenceManager::persistenceManager()->deleteDocumentScenes(mProxy, indexes);

    for (int i = indexes.size() - 1; i >= 0; --i)
    {
        if (indexes.at(i) >= 0 && indexes.at(i) < mPageNames.size())
        {
            mPageNames.removeAt(indexes.at(i));
        }
        mThumbnailScene->deleteThumbnail(indexes.at(i), false);
        emit UBPersistenceManager::persistenceManager()->documentSceneDeleted(mProxy, indexes.at(i));
    }

    normalizePageNames();
    savePageNames();

    mThumbnailScene->renumberThumbnails(indexes.first());
    mThumbnailScene->arrangeThumbnails(indexes.first());

    QDateTime now = QDateTime::currentDateTime();
    mProxy->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));
}

void UBDocument::duplicatePage(int index)
{
    UBPersistenceManager::persistenceManager()->duplicateDocumentScene(mProxy, index);

    insertPageName(index + 1, pageName(index));
    mThumbnailScene->insertThumbnail(index + 1);

    QDateTime now = QDateTime::currentDateTime();
    mProxy->setMetaData(UBSettings::documentUpdatedAt, UBStringUtils::toUtcIsoDateTime(now));

    emit UBPersistenceManager::persistenceManager()->documentSceneDuplicated(mProxy, index + 1);
}

void UBDocument::movePage(int fromIndex, int toIndex)
{
    UBPersistenceManager::persistenceManager()->moveSceneToIndex(mProxy, fromIndex, toIndex);

    normalizePageNames();
    if (fromIndex >= 0 && fromIndex < mPageNames.size()
            && toIndex >= 0 && toIndex < mPageNames.size())
    {
        mPageNames.move(fromIndex, toIndex);
        savePageNames();
    }

    mThumbnailScene->moveThumbnail(fromIndex, toIndex);
    emit UBPersistenceManager::persistenceManager()->documentSceneMoved(mProxy, fromIndex, toIndex);
}

void UBDocument::copyPage(int fromIndex, std::shared_ptr<UBDocumentProxy> to, int toIndex)
{
    UBPersistenceManager::persistenceManager()->copyDocumentScene(mProxy, fromIndex, to, toIndex);

    const auto toDocument = findDocument(to);

    if (toDocument)
    {
        toDocument->insertPageName(toIndex, pageName(fromIndex));
        toDocument->mThumbnailScene->insertThumbnail(toIndex);
    }
}

void UBDocument::insertPage(std::shared_ptr<UBGraphicsScene> scene, int index, bool persist, bool deleting)
{
    UBPersistenceManager::persistenceManager()->insertDocumentSceneAt(mProxy, scene, index, persist, deleting);

    insertPageName(index);
    mThumbnailScene->insertThumbnail(index);
}

std::shared_ptr<UBGraphicsScene> UBDocument::createPage(int index, bool useUndoRedoStack)
{
    auto scene = UBPersistenceManager::persistenceManager()->createDocumentSceneAt(mProxy, index, useUndoRedoStack);

    insertPageName(index);
    mThumbnailScene->insertThumbnail(index, scene);

    return scene;
}

void UBDocument::persistPage(std::shared_ptr<UBGraphicsScene> scene, const int index, bool isAutomaticBackup,
                             bool forceImmediateSaving)
{
    UBPersistenceManager::persistenceManager()->persistDocumentScene(mProxy, scene, index, isAutomaticBackup,
                                                                     forceImmediateSaving);
    mThumbnailScene->reloadThumbnail(index);
}

QString UBDocument::pageName(int index) const
{
    return index >= 0 && index < mPageNames.size() ? mPageNames.at(index) : QString();
}

void UBDocument::renamePage(int index, const QString& name)
{
    normalizePageNames();
    if (index < 0 || index >= mPageNames.size())
    {
        return;
    }

    const QString normalizedName = name.trimmed();
    if (normalizedName.isEmpty() || mPageNames.at(index) == normalizedName)
    {
        return;
    }

    mPageNames[index] = normalizedName;
    savePageNames();
    mThumbnailScene->renameThumbnail(index, normalizedName);
}

UBThumbnailScene* UBDocument::thumbnailScene() const
{
    return mThumbnailScene;
}

void UBDocument::loadPageNames()
{
    mPageNames.clear();

    QFile file{mProxy->persistencePath() + QLatin1Char('/') + pageNamesFileName};
    if (file.open(QFile::ReadOnly))
    {
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        const QJsonArray pages = document.object().value(QStringLiteral("pages")).toArray();
        for (const QJsonValue& value : pages)
        {
            mPageNames.append(value.toString());
        }
    }

    normalizePageNames();
}

void UBDocument::savePageNames() const
{
    if (mProxy->persistencePath().isEmpty())
    {
        return;
    }

    QDir().mkpath(mProxy->persistencePath());
    QJsonArray pages;
    for (const QString& name : mPageNames)
    {
        pages.append(name);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("pages"), pages);

    QSaveFile file{mProxy->persistencePath() + QLatin1Char('/') + pageNamesFileName};
    if (file.open(QFile::WriteOnly))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void UBDocument::normalizePageNames()
{
    const int pageCount = mProxy ? mProxy->pageCount() : 0;
    while (mPageNames.size() < pageCount)
    {
        mPageNames.append(QString());
    }
    while (mPageNames.size() > pageCount)
    {
        mPageNames.removeLast();
    }
}

void UBDocument::insertPageName(int index, const QString& name)
{
    normalizePageNames();
    index = qBound(0, index, mPageNames.size());
    mPageNames.insert(index, name.trimmed());

    while (mPageNames.size() > mProxy->pageCount())
    {
        mPageNames.removeLast();
    }
    savePageNames();
}

/**
 * @brief Get document for proxy.
 *
 * Retrieves UBDocument instance for a document proxy from the list of known instances.
 * If the UBDocument object does not exist, then a new one is created and added to the list.
 * At the same time expired instances are removed from the list.
 *
 * @note If UBDocument instances should be cached, then you have to make sure that a shared
 * pointer to the instance is kept somewhere. Not-referenced instances are automatically
 * deleted.
 *
 * @param proxy The document proxy of the document.
 * @return Not-null shared pointer to the UBDocument instance related to the document proxy
 * or nullptr if (and only if) proxy is nullptr.
 */
std::shared_ptr<UBDocument> UBDocument::getDocument(std::shared_ptr<UBDocumentProxy> proxy)
{
    if (!proxy)
    {
        return nullptr;
    }

    auto document = findDocument(proxy);

    if (!document)
    {
        document = std::shared_ptr<UBDocument>(new UBDocument(proxy));
        sDocuments << document;
    }

    return document;
}

std::shared_ptr<UBDocument> UBDocument::findDocument(std::shared_ptr<UBDocumentProxy> proxy)
{
    if (!proxy)
    {
        return nullptr;
    }

    for (int i = 0; i < sDocuments.size();)
    {
        const auto document = sDocuments.at(i).lock();

        if (!document)
        {
            // weak pointer expired, clean up list
            sDocuments.removeAt(i);
        }
        else if (document->mProxy == proxy)
        {
            // document found
            return document;
        }
        else
        {
            // check next list entry
            ++i;
        }
    }

    return nullptr;
}
