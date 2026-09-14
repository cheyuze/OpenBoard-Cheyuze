#pragma once
#include <QtCore>
#include <memory>
#include "document/UBDocumentProxy.h"
class UBPersistenceManager
{
public:
    static UBPersistenceManager* persistenceManager() { static UBPersistenceManager manager; return &manager; }
    inline static bool copyFails = false;
    int metadataWrites = 0;
    QStringList mDocumentSubDirectories{QStringLiteral("images"), QStringLiteral("objects")};
    void deleteDocumentScenes(std::shared_ptr<UBDocumentProxy> proxy, const QList<int>& indexes) { proxy->count -= indexes.size(); }
    void duplicateDocumentScene(std::shared_ptr<UBDocumentProxy> proxy, int) { ++proxy->count; }
    void moveSceneToIndex(std::shared_ptr<UBDocumentProxy>, int, int) {}
    void copyDocumentScene(std::shared_ptr<UBDocumentProxy>, int, std::shared_ptr<UBDocumentProxy> to, int) { if (!copyFails) ++to->count; }
    void insertDocumentSceneAt(std::shared_ptr<UBDocumentProxy> proxy, std::shared_ptr<UBGraphicsScene>, int, bool, bool) { ++proxy->count; }
    std::shared_ptr<UBGraphicsScene> createDocumentSceneAt(std::shared_ptr<UBDocumentProxy> proxy, int, bool) { ++proxy->count; return std::make_shared<UBGraphicsScene>(); }
    void persistDocumentScene(std::shared_ptr<UBDocumentProxy>, std::shared_ptr<UBGraphicsScene>, int, bool, bool) {}
    void documentSceneDeleted(std::shared_ptr<UBDocumentProxy>, int) {}
    void documentSceneDuplicated(std::shared_ptr<UBDocumentProxy>, int) {}
    void documentSceneMoved(std::shared_ptr<UBDocumentProxy>, int, int) {}
    std::shared_ptr<UBDocumentProxy> persistDocumentMetadata(std::shared_ptr<UBDocumentProxy> proxy, bool = false) { ++metadataWrites; return proxy; }
    std::shared_ptr<UBDocumentProxy> createDocumentFromDir(const QString& path, const QString&, const QString&, bool, bool, bool) { return std::make_shared<UBDocumentProxy>(path, getSceneFileNames(path).size()); }
    QStringList getSceneFileNames(const QString& folder) { return QDir(folder, QStringLiteral("page???.svg"), QDir::Name, QDir::Files).entryList(); }
    bool addDirectoryContentToDocument(const QString& documentRootFolder, std::shared_ptr<UBDocumentProxy> pDocument);
};
