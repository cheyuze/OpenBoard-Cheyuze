#include <QtTest>
#include <QtWidgets>
#include <memory>
#include "core/UBApplication.h"
#include "core/UBPersistenceManager.h"
#include "core/UBSettings.h"
#include "document/UBDocument.h"
#include "gui/UBThumbnailScene.h"
#include "gui/UBThumbnail.h"
#include "frameworks/UBFileSystemUtils.h"
#include "adaptors/UBSvgSubsetAdaptor.h"
#include "quazip.h"
#include "quazipfile.h"
#include "quazipnewinfo.h"
#include "adaptors/UBImportDocument.h"

namespace
{
bool writeFile(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString& path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}
bool makeArchive(const QString& path, const QList<QPair<QString,QByteArray>>& entries)
{
    QuaZip zip(path);
    zip.setFileNameCodec("UTF-8");
    if (!zip.open(QuaZip::mdCreate)) return false;
    for (const auto& entry : entries)
    {
        QuaZipFile file(&zip);
        if (!file.open(QIODevice::WriteOnly, QuaZipNewInfo(entry.first))) return false;
        if (file.write(entry.second) != entry.second.size()) return false;
        file.close();
        if (file.getZipError() != UNZ_OK) return false;
    }
    zip.close();
    return zip.getZipError() == UNZ_OK;
}
QString namesJson(const QStringList& names)
{
    QJsonArray pages; for (const auto& name:names) pages.append(name);
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"version",1},{"pages",pages}}).toJson());
}
std::shared_ptr<UBDocumentProxy> makeDocument(const QString& path, const QStringList& names)
{
    QDir().mkpath(path);
    for (int index=0; index<names.size(); ++index)
        writeFile(path+QString("/page%1.svg").arg(index,3,10,QChar('0')), QByteArray("original ")+QByteArray::number(index));
    writeFile(path+"/pageNames.json",namesJson(names).toUtf8());
    return std::make_shared<UBDocumentProxy>(path,names.size());
}
}

class UBImportDocumentTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        UBApplication::lastMessage.clear();
        UBPersistenceManager::copyFails = false;
        UBFileSystemUtils::forceDependencyFailure = false;
        UBSvgSubsetAdaptor::afterUuid = {};
    }
    void productionMethodIsExact()
    {
        const QString sourceRoot=qEnvironmentVariable("DOCUMENT_TEST_SOURCE");
        const QString testDirectory=qEnvironmentVariable("DOCUMENT_TEST_DIRECTORY");
        QString source=QString::fromUtf8(readFile(sourceRoot+"/src/core/UBPersistenceManager.cpp"));
        source.replace("\r","");
        const int begin=source.indexOf("bool UBPersistenceManager::addDirectoryContentToDocument");
        const int end=source.indexOf("bool UBPersistenceManager::isEmpty",begin);
        QVERIFY(begin>=0 && end>begin);
        QString extracted=QString::fromUtf8(readFile(QDir(testDirectory).filePath("persistence-method.inc")));
        extracted.replace("\r","");
        QCOMPARE(source.mid(begin,end-begin).trimmed(),extracted.trimmed());
    }
    void archiveRejectsUnsafeMembers_data()
    {
        QTest::addColumn<QString>("member");
        for (const auto& path:QStringList{"../escape.svg","a/../../escape.svg","..\\escape.svg","a\\..\\..\\escape.svg","/absolute.svg","\\absolute.svg","C:/escape.svg","C:\\escape.svg","C:escape.svg","\\\\server\\share\\escape.svg","//server/share/escape.svg","image.svg:stream","NUL","dir/COM1.txt","dir/trailing. ","dir/.. /escape.svg"})
            QTest::newRow(path.toUtf8().constData())<<path;
    }
    void archiveRejectsUnsafeMembers()
    {
        QFETCH(QString,member);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString input=temp.path()+"/input.ubz";
        QVERIFY(makeArchive(input,{{"page000.svg","legitimate first entry"},{member,"unsafe second entry"}}));
        const QString output=temp.path()+"/documents";
        QVERIFY(QDir().mkpath(output));
        UBImportDocument importer;
        QString extracted;
        QVERIFY(!importer.extractFileToDir(QFile(input),output,extracted));
        QVERIFY(extracted.isEmpty());
        QVERIFY(QDir(output).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(!QFile::exists(temp.path()+"/escape.svg"));
    }
    void archiveDirectoriesUnicodeAndStreaming()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QByteArray large(2*1024*1024+137,'Q');
        const QString input=temp.path()+"/input.ubz";
        QVERIFY(makeArchive(input,{{"./",{}},{"images/",{}},{"images/",{}},{"images/题目.png",large},{"objects\\",{}},{"objects\\lesson.dat","asset"},{"page000.svg","page"}}));
        UBImportDocument importer;
        QString extracted;
        QVERIFY(importer.extractFileToDir(QFile(input),temp.path(),extracted));
        QCOMPARE(readFile(extracted+"/images/题目.png"),large);
        QCOMPARE(readFile(extracted+"/objects/lesson.dat"),QByteArray("asset"));
        QCOMPARE(readFile(extracted+"/page000.svg"),QByteArray("page"));
        // Two imports in the same millisecond must not share an extraction root.
        QString second;
        QVERIFY(importer.extractFileToDir(QFile(input),temp.path(),second));
        QVERIFY(extracted!=second);
    }
    void archiveRejectsCollisions_data()
    {
        QTest::addColumn<QString>("first"); QTest::addColumn<QString>("second");
        QTest::newRow("duplicate")<<QString("page000.svg")<<QString("page000.svg");
        QTest::newRow("case alias")<<QString("Page000.svg")<<QString("page000.svg");
        QTest::newRow("slash alias")<<QString("images/a.png")<<QString("images\\a.png");
        QTest::newRow("file versus directory")<<QString("images")<<QString("images/a.png");
        QTest::newRow("directory versus file")<<QString("images/a.png")<<QString("images");
    }
    void archiveRejectsCollisions()
    {
        QFETCH(QString,first); QFETCH(QString,second);
        QTemporaryDir temp;
        const QString input=temp.path()+"/input.ubz";
        QVERIFY(makeArchive(input,{{first,"first"},{second,"second"}}));
        const QString output=temp.path()+"/output";
        QVERIFY(QDir().mkpath(output));
        QString extracted; UBImportDocument importer;
        QVERIFY(!importer.extractFileToDir(QFile(input),output,extracted));
        QVERIFY(QDir(output).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty());
    }
    void archiveCrcFailureCleansPartialOutput()
    {
        QTemporaryDir temp;
        const QString input=temp.path()+"/input.ubz";
        QVERIFY(makeArchive(input,{{"page000.svg","page"},{"images/fault.dat",QByteArray(256*1024,'F')}}));
        QByteArray damaged=readFile(input);
        const int central=damaged.indexOf(QByteArray("PK\1\2",4));
        QVERIFY(central>=0);
        damaged[central+16]=char(damaged.at(central+16)^0x7f); // bad first entry CRC in central directory
        QVERIFY(writeFile(input,damaged));
        const QString output=temp.path()+"/output";
        QVERIFY(QDir().mkpath(output));
        QString extracted; UBImportDocument importer;
        QVERIFY(!importer.extractFileToDir(QFile(input),output,extracted));
        QVERIFY(QDir(output).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty());
    }
    void renamePersistsAndRejectsEmpty()
    {
        QTemporaryDir temp;
        auto proxy=makeDocument(temp.path()+"/document",{"first","second"});
        auto doc=UBDocument::getDocument(proxy);
        doc->renamePage(0,"  新课  ");
        QCOMPARE(doc->pageName(0),QString("新课"));
        QCOMPARE(doc->thumbnailScene()->thumbnailAt(0)->label,QString("新课"));
        doc->renamePage(0,"   "); doc->renamePage(-1,"bad"); doc->renamePage(8,"bad");
        QCOMPARE(doc->pageName(0),QString("新课"));
        doc.reset();
        QCOMPARE(UBDocument::getDocument(proxy)->pageName(0),QString("新课"));
    }
    void renameFailureDoesNotPretendSuccess()
    {
        QTemporaryDir temp;
        auto proxy=makeDocument(temp.path()+"/document",{"original"});
        auto doc=UBDocument::getDocument(proxy);
        auto* thumbnail=doc->thumbnailScene()->thumbnailAt(0);
        const QString path=proxy->path+"/pageNames.json";
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkpath(path)); // deterministic save failure without changing ACLs
        doc->renamePage(0,"must not be displayed");
        QCOMPARE(doc->pageName(0),QString("original"));
        QCOMPARE(thumbnail->label,QString("original"));
        QVERIFY(!UBApplication::lastMessage.isEmpty());
    }
    void copyToColdDocumentPreservesAllNames()
    {
        QTemporaryDir temp;
        auto source=makeDocument(temp.path()+"/source",{"source name"});
        auto target=makeDocument(temp.path()+"/target",{"first","second"});
        auto sourceDoc=UBDocument::getDocument(source);
        sourceDoc->copyPage(0,target,1);
        QCOMPARE(target->pageCount(),3);
        auto targetDoc=UBDocument::getDocument(target);
        QCOMPARE(targetDoc->pageName(0),QString("first"));
        QCOMPARE(targetDoc->pageName(1),QString("source name"));
        QCOMPARE(targetDoc->pageName(2),QString("second"));
        QCOMPARE(targetDoc->thumbnailScene()->thumbnailCount(),3);
        targetDoc.reset();
        QCOMPARE(UBDocument::getDocument(target)->pageName(1),QString("source name"));
    }
    void failedCopyDoesNotChangeLabelsOrThumbnails()
    {
        QTemporaryDir temp;
        auto source=makeDocument(temp.path()+"/source",{"source"});
        auto target=makeDocument(temp.path()+"/target",{"first","second"});
        auto sourceDoc=UBDocument::getDocument(source);
        auto targetDoc=UBDocument::getDocument(target);
        UBPersistenceManager::copyFails=true;
        sourceDoc->copyPage(0,target,1);
        QCOMPARE(target->pageCount(),2);
        QCOMPARE(targetDoc->pageName(1),QString("second"));
        QCOMPARE(targetDoc->thumbnailScene()->thumbnailCount(),2);
        sourceDoc->copyPage(0,source,0);
        QCOMPARE(source->pageCount(),1);
    }
    void duplicateMoveDeleteAndNamedInsertKeepLabels()
    {
        QTemporaryDir temp;
        auto proxy=makeDocument(temp.path()+"/document",{"A","B","C"});
        auto doc=UBDocument::getDocument(proxy);
        doc->duplicatePage(1);
        QCOMPARE(doc->pageName(2),QString("B"));
        doc->movePage(3,0);
        QCOMPARE(doc->pageName(0),QString("C"));
        doc->deletePages({1});
        QCOMPARE(proxy->pageCount(),3);
        QCOMPARE(doc->pageName(0),QString("C"));
        QCOMPARE(doc->pageName(1),QString("B"));
        doc->insertPage(std::make_shared<UBGraphicsScene>(),1,true,false,"imported");
        QCOMPARE(doc->pageName(1),QString("imported"));
        QCOMPARE(doc->pageName(2),QString("B"));
    }
    void sameDocumentCopyBeforeSourceLeavesEverythingUnchanged()
    {
        QTemporaryDir temp;
        auto proxy=makeDocument(temp.path()+"/document",{"A","B","C"});
        auto doc=UBDocument::getDocument(proxy);
        const QByteArray originalNames=readFile(proxy->path+"/pageNames.json");
        QList<UBThumbnail*> originalThumbnails;
        for (int index=0;index<3;++index)
            originalThumbnails.append(doc->thumbnailScene()->thumbnailAt(index));

        // Both cases are already unsupported by copyDocumentScene: insertion
        // before the source and insertion at the source's own index.
        doc->copyPage(2,proxy,0);
        doc->copyPage(1,proxy,1);

        QCOMPARE(proxy->pageCount(),3);
        QCOMPARE(doc->thumbnailScene()->thumbnailCount(),3);
        QCOMPARE(readFile(proxy->path+"/pageNames.json"),originalNames);
        const QStringList expectedNames{"A","B","C"};
        for (int index=0;index<3;++index)
        {
            QCOMPARE(doc->pageName(index),expectedNames.at(index));
            QCOMPARE(doc->thumbnailScene()->thumbnailAt(index),originalThumbnails.at(index));
            QCOMPARE(originalThumbnails.at(index)->sceneIndex(),index);
            QCOMPARE(originalThumbnails.at(index)->label,expectedNames.at(index));
            QCOMPARE(readFile(proxy->path+QString("/page%1.svg").arg(index,3,10,QChar('0'))),
                     QByteArray("original ")+QByteArray::number(index));
        }
    }
    void appendArchiveAdvancesCountAndPreservesNames()
    {
        QTemporaryDir temp;
        auto target=makeDocument(temp.path()+"/target",{"existing"});
        auto doc=UBDocument::getDocument(target);
        const QString input=temp.path()+"/input.ubz";
        QVERIFY(makeArchive(input,{{"page000.svg","imported A"},{"page001.svg","imported B"},{"pageNames.json",namesJson({"PPT page A","PPT page B"}).toUtf8()},{"images/",{}},{"images/one.png","asset"}}));
        UBImportDocument importer;
        QVERIFY(importer.addFileToDocument(target,QFile(input)));
        QCOMPARE(target->pageCount(),3);
        QCOMPARE(doc->thumbnailScene()->thumbnailCount(),3);
        QCOMPARE(doc->pageName(0),QString("existing"));
        QCOMPARE(doc->pageName(1),QString("PPT page A"));
        QCOMPARE(doc->pageName(2),QString("PPT page B"));
        QCOMPARE(readFile(target->path+"/page000.svg"),QByteArray("original 0"));
        QCOMPARE(readFile(target->path+"/page001.svg"),QByteArray("imported A"));
        QCOMPARE(readFile(target->path+"/page002.svg"),QByteArray("imported B"));
        QTest::qWait(120); // allow actual background loader to index the appended slots
        QCOMPARE(doc->thumbnailScene()->thumbnailAt(2)->label,QString("PPT page B"));
        doc->createPage(target->pageCount());
        QCOMPARE(target->pageCount(),4);
        QCOMPARE(readFile(target->path+"/page002.svg"),QByteArray("imported B"));
    }
    void appendLegacyArchiveWithoutNames()
    {
        QTemporaryDir temp;
        auto target=makeDocument(temp.path()+"/target",{"existing"});
        const QString source=temp.path()+"/source";
        QVERIFY(writeFile(source+"/page000.svg","legacy"));
        QVERIFY(UBPersistenceManager::persistenceManager()->addDirectoryContentToDocument(source,target));
        QCOMPARE(target->pageCount(),2);
        QCOMPARE(UBDocument::getDocument(target)->pageName(0),QString("existing"));
        QVERIFY(UBDocument::getDocument(target)->pageName(1).isEmpty());
    }
    void appendRefusesExistingUncountedPage()
    {
        QTemporaryDir temp;
        auto target=makeDocument(temp.path()+"/target",{"existing"});
        const QString source=temp.path()+"/source";
        QVERIFY(writeFile(source+"/page000.svg","new"));
        QVERIFY(writeFile(target->path+"/page001.svg","preserve orphan"));
        QVERIFY(!UBPersistenceManager::persistenceManager()->addDirectoryContentToDocument(source,target));
        QCOMPARE(target->pageCount(),1);
        QCOMPARE(readFile(target->path+"/page001.svg"),QByteArray("preserve orphan"));
    }
    void dependencyFailureLeavesOriginalPagesUnchanged()
    {
        QTemporaryDir temp;
        auto target=makeDocument(temp.path()+"/target",{"existing"});
        const QString source=temp.path()+"/source";
        QVERIFY(writeFile(source+"/page000.svg","new"));
        QVERIFY(writeFile(source+"/images/image.png","asset"));
        UBFileSystemUtils::forceDependencyFailure=true;
        QVERIFY(!UBPersistenceManager::persistenceManager()->addDirectoryContentToDocument(source,target));
        QCOMPARE(target->pageCount(),1);
        QVERIFY(!QFile::exists(target->path+"/page001.svg"));
        QCOMPARE(readFile(target->path+"/page000.svg"),QByteArray("original 0"));
    }
    void partialPageCopyFailureRollsBackNewPages()
    {
        QTemporaryDir temp;
        auto target=makeDocument(temp.path()+"/target",{"existing"});
        const QString source=temp.path()+"/source";
        QVERIFY(writeFile(source+"/page000.svg","new first"));
        QVERIFY(writeFile(source+"/page001.svg","new second"));
        QVERIFY(writeFile(source+"/page000.thumbnail.jpg","new thumbnail"));
        UBSvgSubsetAdaptor::afterUuid=[source]() { QFile::remove(source+"/page001.svg"); };
        QVERIFY(!UBPersistenceManager::persistenceManager()->addDirectoryContentToDocument(source,target));
        QCOMPARE(target->pageCount(),1);
        QVERIFY(!QFile::exists(target->path+"/page001.svg"));
        QVERIFY(!QFile::exists(target->path+"/page001.thumbnail.jpg"));
        QCOMPARE(readFile(target->path+"/page000.svg"),QByteArray("original 0"));
    }
};
QTEST_MAIN(UBImportDocumentTest)
#include "document-regression.moc"
