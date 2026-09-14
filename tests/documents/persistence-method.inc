bool UBPersistenceManager::addDirectoryContentToDocument(const QString& documentRootFolder, std::shared_ptr<UBDocumentProxy> pDocument)
{
    if (!pDocument)
        return false;
    QStringList sourceScenes = getSceneFileNames(documentRootFolder);
    if (sourceScenes.empty())
        return false;

    const int targetPageCount = pDocument->pageCount();
    // Never overwrite a page left on disk by a previous failed import.
    for (int sourceIndex = 0; sourceIndex < sourceScenes.size(); ++sourceIndex)
        if (QFile::exists(pDocument->persistencePath()
                         + UBFileSystemUtils::digitFileFormat("/page%1.svg", targetPageCount + sourceIndex)))
            return false;

    // Copy dependencies first. Failure must not leave new page files outside the
    // proxy's page count, where the next new-page operation could overwrite them.
    foreach (QString dir, mDocumentSubDirectories)
    {
        const QString source = documentRootFolder + "/" + dir;
        if (QDir(source).exists()
                && !UBFileSystemUtils::copyDir(source, pDocument->persistencePath() + "/" + dir))
            return false;
    }
    QStringList addedPageFiles;

    for(int sourceIndex = 0 ; sourceIndex < sourceScenes.size(); sourceIndex++)
    {
        int targetIndex = targetPageCount + sourceIndex;

        QFile svg(documentRootFolder + "/" + sourceScenes[sourceIndex]);
        const QString targetSvg = pDocument->persistencePath()
                + UBFileSystemUtils::digitFileFormat("/page%1.svg", targetIndex);
        if (!svg.copy(targetSvg))
        {
            for (const QString& addedFile : addedPageFiles)
                QFile::remove(addedFile);
            return false;
        }
        addedPageFiles.append(targetSvg);

        UBSvgSubsetAdaptor::setSceneUuid(pDocument, targetIndex, QUuid::createUuid());

        QFile thumb(documentRootFolder + UBFileSystemUtils::digitFileFormat("/page%1.thumbnail.jpg", sourceIndex));
        // We can ignore error in this case, thumbnail will be genarated
        const QString targetThumbnail = pDocument->persistencePath()
                + UBFileSystemUtils::digitFileFormat("/page%1.thumbnail.jpg", targetIndex);
        if (thumb.copy(targetThumbnail))
            addedPageFiles.append(targetThumbnail);
    }

    // Keep the cached document alive before changing its page count, so existing
    // labels and thumbnails can be extended instead of being replaced.
    const auto document = UBDocument::getDocument(pDocument);
    pDocument->setPageCount(targetPageCount + sourceScenes.size());
    QFile namesFile(documentRootFolder + QStringLiteral("/pageNames.json"));
    QJsonArray importedNames;
    if (namesFile.open(QIODevice::ReadOnly))
        importedNames = QJsonDocument::fromJson(namesFile.readAll()).object().value(QStringLiteral("pages")).toArray();
    for (int sourceIndex = 0; sourceIndex < sourceScenes.size(); ++sourceIndex)
        document->renamePage(targetPageCount + sourceIndex,
                             sourceIndex < importedNames.size() ? importedNames.at(sourceIndex).toString() : QString());
    document->thumbnailScene()->createThumbnails(targetPageCount);
    persistDocumentMetadata(pDocument, true);

    //issue NC - NNE - 20131213 : At this point, all is well done.
    return true;
}
