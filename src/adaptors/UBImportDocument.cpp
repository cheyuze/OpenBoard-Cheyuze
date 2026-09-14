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




#include "UBImportDocument.h"
#include "document/UBDocumentProxy.h"
#include <QTemporaryDir>

#include "frameworks/UBFileSystemUtils.h"

#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "core/UBPersistenceManager.h"

#include "globals/UBGlobals.h"

#ifdef Q_OS_WIN
    #include <quazip.h>
    #include <quazipfile.h>
    #include <quazipfileinfo.h>
#else
    #include "quazip.h"
    #include "quazipfile.h"
    #include "quazipfileinfo.h"
#endif

#include "core/memcheck.h"

namespace
{
// ZIP names use '/', but also reject Windows separators and drive/stream names.
// Validate the whole archive before creating an extraction directory or files.
bool safeArchivePath(QString name, QString& relativePath, bool& isDirectory)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (name.isEmpty() || name.contains(QChar::Null) || name.startsWith(QLatin1Char('/'))
            || name.contains(QLatin1Char(':')) || QDir::isAbsolutePath(name))
        return false;

    isDirectory = name.endsWith(QLatin1Char('/'));
    const QStringList components = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& component : components)
    {
        if (component == QLatin1String(".."))
            return false;
#ifdef Q_OS_WIN
        if (component != QLatin1String(".")
                && (component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' '))))
            return false;
        const QString device = component.section(QLatin1Char('.'), 0, 0).toUpper();
        if (device == QLatin1String("CON") || device == QLatin1String("PRN")
                || device == QLatin1String("AUX") || device == QLatin1String("NUL")
                || (device.size() == 4 && (device.startsWith(QLatin1String("COM"))
                                           || device.startsWith(QLatin1String("LPT")))
                    && device.at(3) >= QLatin1Char('1') && device.at(3) <= QLatin1Char('9')))
            return false;
#endif
    }
    relativePath = QDir::cleanPath(name);
    return relativePath != QLatin1String(".") || isDirectory;
}
}

UBImportDocument::UBImportDocument(QObject *parent)
    :UBDocumentBasedImportAdaptor(parent)
{
    // NOOP
}

UBImportDocument::~UBImportDocument()
{
    // NOOP
}


QStringList UBImportDocument::supportedExtentions()
{
    return QStringList("ubz");
}


QString UBImportDocument::importFileFilter()
{
    return tr("OpenBoard (*.ubz)");
}


bool UBImportDocument::extractFileToDir(const QFile& pZipFile, const QString& pDir, QString& documentRoot)
{

    QuaZip zip(pZipFile.fileName());

    if(!zip.open(QuaZip::mdUnzip))
    {
        qWarning() << "Import failed. Cause zip.open(): " << zip.getZipError();
        return false;
    }

    zip.setFileNameCodec("UTF-8");
    QuaZipFileInfo info;
    QSet<QString> filePaths;
    QSet<QString> directoryPaths;
    for(bool more=zip.goToFirstFile(); more; more=zip.goToNextFile())
    {
        if(!zip.getCurrentFileInfo(&info))
        {
            //TOD UB 4.3 O display error to user or use crash reporter
            qWarning() << "Import failed. Cause: getCurrentFileInfo(): " << zip.getZipError();
            return false;
        }

        QString path;
        bool directory = false;
        if (!safeArchivePath(info.name, path, directory))
        {
            qWarning() << "Import failed: unsafe archive path" << info.name;
            return false;
        }
#ifdef Q_OS_WIN
        path = path.toCaseFolded();
#endif
        if (filePaths.contains(path) || (!directory && directoryPaths.contains(path)))
            return false;
        if (directory)
            directoryPaths.insert(path);
        else
            filePaths.insert(path);
        QString parent = path;
        while (parent.contains(QLatin1Char('/')))
        {
            parent = parent.left(parent.lastIndexOf(QLatin1Char('/')));
            directoryPaths.insert(parent);
        }
    }
    if (zip.getZipError() != UNZ_OK || filePaths.isEmpty())
        return false;
    for (const QString& path : filePaths)
        if (directoryPaths.contains(path))
            return false;

    // A fresh temporary root prevents collisions with an existing document and
    // automatically removes a partially extracted archive on any read/write error.
    QTemporaryDir extractedRoot(QDir(pDir).absoluteFilePath(QStringLiteral("OpenBoard Import-XXXXXX")));
    if (!extractedRoot.isValid())
        return false;
    QDir rootDir(extractedRoot.path());
    QuaZipFile file(&zip);
    QByteArray buffer(64 * 1024, '\0');
    for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile())
    {
        QString relativePath;
        bool directory = false;
        if (!zip.getCurrentFileInfo(&info) || !safeArchivePath(info.name, relativePath, directory))
            return false;
        const QString targetPath = rootDir.absoluteFilePath(relativePath);
        if (directory)
        {
            if (!rootDir.mkpath(targetPath))
                return false;
            continue;
        }
        if (!rootDir.mkpath(QFileInfo(targetPath).absolutePath()) || !file.open(QIODevice::ReadOnly))
            return false;
        QFile out(targetPath);
        if (!out.open(QIODevice::WriteOnly))
            return false;
        qint64 bytes = 0;
        while ((bytes = file.read(buffer.data(), buffer.size())) > 0)
            if (out.write(buffer.constData(), bytes) != bytes)
                return false;
        if (bytes < 0 || !file.atEnd() || !out.flush())
            return false;
        out.close();
        file.close();
        if (file.getZipError() != UNZ_OK)
            return false;
    }
    if (zip.getZipError() != UNZ_OK)
        return false;
    zip.close();

    if(zip.getZipError()!=UNZ_OK)
    {
      qWarning() << "Import failed. Cause: zip.close(): " << zip.getZipError();
      return false;
    }

    documentRoot = extractedRoot.path();
    extractedRoot.setAutoRemove(false);
    return true;
}

std::shared_ptr<UBDocumentProxy> UBImportDocument::importFile(const QFile& pFile, const QString& pGroup)
{
    Q_UNUSED(pGroup); // group is defined in the imported file

    QFileInfo fi(pFile);
    UBApplication::showMessage(tr("Importing file %1...").arg(fi.baseName()), true);

    // first unzip the file to the correct place
    QString path = UBSettings::userDocumentDirectory();

    QString documentRootFolder;

    if(!extractFileToDir(pFile, path, documentRootFolder)){
        UBApplication::showMessage(tr("Import of file %1 failed.").arg(fi.baseName()));
        return NULL;
    }

    std::shared_ptr<UBDocumentProxy> newDocument = UBPersistenceManager::persistenceManager()->createDocumentFromDir(documentRootFolder, pGroup, "", false, false, true);

    UBApplication::showMessage(tr("Import successful."));

    return newDocument;
}

bool UBImportDocument::addFileToDocument(std::shared_ptr<UBDocumentProxy> pDocument, const QFile& pFile)
{
    QFileInfo fi(pFile);
    UBApplication::showMessage(tr("Importing file %1...").arg(fi.baseName()), true);

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid())
        return false;
    const QString path = temporaryDirectory.path();

    QString documentRootFolder;
    if (!extractFileToDir(pFile, path, documentRootFolder))
    {
        UBApplication::showMessage(tr("Import of file %1 failed.").arg(fi.baseName()));
        return false;
    }

    if (!UBPersistenceManager::persistenceManager()->addDirectoryContentToDocument(documentRootFolder, pDocument))
    {
        UBApplication::showMessage(tr("Import of file %1 failed.").arg(fi.baseName()));
        return false;
    }

    UBApplication::showMessage(tr("Import successful."));

    return true;
}

