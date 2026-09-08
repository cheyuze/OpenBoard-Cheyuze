/*
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 */

#include "UBImportPowerPoint.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <oleauto.h>
#endif

#include "UBImportPDF.h"
#include "UBImportImage.h"

#include "core/UBApplication.h"
#include "core/UBPersistenceManager.h"
#include "core/UBSettings.h"

#include "document/UBDocument.h"
#include "document/UBDocumentProxy.h"

#include "gui/UBThumbnailScene.h"

namespace
{
bool isUsablePdf(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.size() > 0;
}

QString libreOfficeExecutable()
{
    QString executable = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!executable.isEmpty())
        return executable;

#ifdef Q_OS_WIN
    const QStringList roots = {
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("ProgramFiles(x86)"),
        QCoreApplication::applicationDirPath()
    };

    for (const QString& root : roots)
    {
        if (root.isEmpty())
            continue;

        const QStringList candidates = {
            QDir(root).filePath(QStringLiteral("LibreOffice/program/soffice.exe")),
            QDir(root).filePath(QStringLiteral("libreoffice/program/soffice.exe"))
        };

        for (const QString& candidate : candidates)
        {
            if (QFileInfo::exists(candidate))
                return candidate;
        }
    }
#endif

    return QString();
}

#ifdef Q_OS_WIN
HRESULT invokeAutomation(IDispatch* object,
                         const wchar_t* memberName,
                         WORD flags,
                         VARIANT* arguments,
                         UINT argumentCount,
                         VARIANT* result)
{
    if (!object)
        return E_POINTER;

    LPOLESTR name = const_cast<LPOLESTR>(memberName);
    DISPID memberId = DISPID_UNKNOWN;
    HRESULT status = object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &memberId);
    if (FAILED(status))
        return status;

    DISPPARAMS parameters = {};
    parameters.rgvarg = arguments;
    parameters.cArgs = argumentCount;

    EXCEPINFO exception = {};
    UINT argumentError = 0;
    return object->Invoke(memberId,
                          IID_NULL,
                          LOCALE_USER_DEFAULT,
                          flags,
                          &parameters,
                          result,
                          &exception,
                          &argumentError);
}

bool automationMethod(IDispatch* object, const wchar_t* memberName, VARIANT* arguments = nullptr, UINT argumentCount = 0)
{
    VARIANT result;
    VariantInit(&result);
    const HRESULT status = invokeAutomation(object, memberName, DISPATCH_METHOD, arguments, argumentCount, &result);
    VariantClear(&result);
    return SUCCEEDED(status);
}
#endif
}

UBImportPowerPoint::UBImportPowerPoint(QObject* parent)
    : UBDocumentBasedImportAdaptor(parent)
{
}

UBImportPowerPoint::~UBImportPowerPoint() = default;

QStringList UBImportPowerPoint::supportedExtentions()
{
    return {QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("pps"), QStringLiteral("ppsx")};
}

QString UBImportPowerPoint::importFileFilter()
{
    return tr("PowerPoint presentations (*.ppt *.pptx *.pps *.ppsx)");
}

std::shared_ptr<UBDocumentProxy> UBImportPowerPoint::importFile(const QFile& file, const QString& group)
{
    const QFileInfo sourceInfo(file);
    QTemporaryDir temporaryDirectory(QDir::tempPath() + QStringLiteral("/OpenBoard-PowerPoint-XXXXXX"));
    if (!temporaryDirectory.isValid())
    {
        showConversionError();
        return nullptr;
    }

    std::shared_ptr<UBDocumentProxy> document = UBPersistenceManager::persistenceManager()->createDocument(
        group, sourceInfo.completeBaseName(), false, QString(), 0, true);
    if (!document)
        return nullptr;

    if (!appendPresentationPages(document, sourceInfo.absoluteFilePath(), temporaryDirectory.path()))
    {
        UBPersistenceManager::persistenceManager()->deleteDocument(document);
        showConversionError();
        return nullptr;
    }

    UBApplication::showMessage(tr("PowerPoint import successful."));
    return document;
}

bool UBImportPowerPoint::addFileToDocument(std::shared_ptr<UBDocumentProxy> document, const QFile& file)
{
    if (!document)
        return false;

    const QFileInfo sourceInfo(file);
    QTemporaryDir temporaryDirectory(QDir::tempPath() + QStringLiteral("/OpenBoard-PowerPoint-XXXXXX"));
    if (!temporaryDirectory.isValid())
    {
        showConversionError();
        return false;
    }

    if (!appendPresentationPages(document, sourceInfo.absoluteFilePath(), temporaryDirectory.path()))
    {
        showConversionError();
        return false;
    }

    UBApplication::showMessage(tr("PowerPoint import successful."));
    return true;
}

bool UBImportPowerPoint::appendPresentationPages(std::shared_ptr<UBDocumentProxy> document,
                                                  const QString& sourcePath,
                                                  const QString& temporaryPath)
{
#ifdef Q_OS_WIN
    // Exporting slides straight to images avoids both PDF generation quirks in
    // Office automation and failures in the PDF parser. It is the preferred
    // Windows path when PowerPoint is installed.
    const QString imageDirectory = QDir(temporaryPath).filePath(QStringLiteral("slides"));
    QStringList imagePaths;
    UBApplication::showMessage(tr("Importing PowerPoint slides as images. Please wait..."), true);
    if (exportImagesWithPowerShell(sourcePath, imageDirectory, imagePaths))
        return appendImagePages(document, imagePaths);

    UBApplication::showMessage(tr("Trying the compatible PowerPoint converter..."), true);
#endif

    const QString pdfPath = QDir(temporaryPath).filePath(
        QFileInfo(sourcePath).completeBaseName() + QStringLiteral(".pdf"));
    if (!convertToPdf(sourcePath, pdfPath))
        return false;

    return appendPdfPages(document, pdfPath);
}

bool UBImportPowerPoint::appendImagePages(std::shared_ptr<UBDocumentProxy> document,
                                          const QStringList& imagePaths)
{
    if (!document || imagePaths.isEmpty())
        return false;

    const int firstPage = document->pageCount();
    int pageIndex = firstPage;
    const auto openBoardDocument = UBDocument::getDocument(document);
    UBImportImage imageImporter;

    for (const QString& imagePath : imagePaths)
    {
        UBApplication::showMessage(
            tr("Importing slide %1 of %2...").arg(pageIndex - firstPage + 1).arg(imagePaths.size()), true);

        QList<UBGraphicsItem*> importedItems = imageImporter.import(QUuid::createUuid(), imagePath);
        if (importedItems.isEmpty())
            return false;

        std::shared_ptr<UBGraphicsScene> scene = openBoardDocument->createPage(pageIndex);
        imageImporter.placeImportedItemToScene(scene, importedItems.first());
        openBoardDocument->persistPage(scene, pageIndex);
        ++pageIndex;
    }

    UBPersistenceManager::persistenceManager()->persistDocumentMetadata(document);
    openBoardDocument->thumbnailScene()->createThumbnails(firstPage);
    return true;
}

bool UBImportPowerPoint::convertToPdf(const QString& sourcePath, const QString& pdfPath)
{
#ifdef Q_OS_WIN
    if (convertWithPowerPoint(sourcePath, pdfPath))
        return true;

    // Office automation can fail when it runs inside a large GUI process even
    // though the same presentation opens normally. Retry from a clean STA
    // PowerShell process before falling back to LibreOffice.
    UBApplication::showMessage(tr("Trying the compatible PowerPoint converter..."), true);
    if (convertWithPowerShell(sourcePath, pdfPath))
        return true;
#endif

    return convertWithLibreOffice(sourcePath, pdfPath);
}

#ifdef Q_OS_WIN
bool UBImportPowerPoint::exportImagesWithPowerShell(const QString& sourcePath,
                                                     const QString& outputDirectory,
                                                     QStringList& imagePaths)
{
    imagePaths.clear();

    QString executable = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (executable.isEmpty())
    {
        const QString windowsDirectory = qEnvironmentVariable("SystemRoot");
        const QString candidate = QDir(windowsDirectory).filePath(
            QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
        if (QFileInfo::exists(candidate))
            executable = candidate;
    }

    QDir outputDir(outputDirectory);
    if (executable.isEmpty() || (!outputDir.exists() && !QDir().mkpath(outputDirectory)))
        return false;

    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "$sourcePath=[Environment]::GetEnvironmentVariable('OPENBOARD_PPT_SOURCE');"
        "$outputDir=[Environment]::GetEnvironmentVariable('OPENBOARD_PPT_IMAGE_DIR');"
        "$powerPoint=$null;$presentation=$null;"
        "try {"
        " $powerPoint=New-Object -ComObject PowerPoint.Application;"
        " $powerPoint.DisplayAlerts=1;"
        " for($attempt=1;$attempt -le 3 -and $null -eq $presentation;$attempt++){"
        "  try{$presentation=$powerPoint.Presentations.Open($sourcePath,$true,$false,$false)}"
        "  catch{if($attempt -eq 3){throw};Start-Sleep -Milliseconds 800}"
        " }"
        " $slideWidth=[double]$presentation.PageSetup.SlideWidth;"
        " $slideHeight=[double]$presentation.PageSetup.SlideHeight;"
        " $imageWidth=1920;"
        " $imageHeight=[Math]::Max(1,[int][Math]::Round($imageWidth*$slideHeight/$slideWidth));"
        " $count=[int]$presentation.Slides.Count;"
        " if($count -lt 1){throw 'The presentation contains no slides.'}"
        " for($index=1;$index -le $count;$index++){"
        "  $slide=$null;"
        "  try{"
        "   $slide=$presentation.Slides.Item($index);"
        "   $file=Join-Path $outputDir ('slide_{0:D6}.png' -f $index);"
        "   for($attempt=1;$attempt -le 3;$attempt++){"
        "    try{$slide.Export($file,'PNG',$imageWidth,$imageHeight);break}"
        "    catch{if($attempt -eq 3){throw};Start-Sleep -Milliseconds 500}"
        "   }"
        "   if(!(Test-Path -LiteralPath $file) -or (Get-Item -LiteralPath $file).Length -le 0){"
        "    throw ('Slide export failed: '+$index)"
        "   }"
        "  } finally {"
        "   if($null -ne $slide){[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($slide)}"
        "  }"
        " }"
        "} catch {Write-Error $_;exit 1} finally {"
        " if($null -ne $presentation){"
        "  $presentation.Close();"
        "  [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($presentation)"
        " }"
        " if($null -ne $powerPoint){"
        "  $powerPoint.Quit();"
        "  [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($powerPoint)"
        " }"
        " [GC]::Collect();[GC]::WaitForPendingFinalizers()"
        "}");

    const QByteArray encodedScript(
        reinterpret_cast<const char*>(script.utf16()), script.size() * static_cast<int>(sizeof(char16_t)));

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OPENBOARD_PPT_SOURCE"), sourcePath);
    environment.insert(QStringLiteral("OPENBOARD_PPT_IMAGE_DIR"), outputDirectory);
    process.setProcessEnvironment(environment);
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-NoLogo"),
                          QStringLiteral("-NoProfile"),
                          QStringLiteral("-NonInteractive"),
                          QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                          QStringLiteral("-EncodedCommand"), QString::fromLatin1(encodedScript.toBase64())});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    process.start();
    if (!process.waitForStarted(5000))
        return false;

    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 600000)
    {
        process.waitForFinished(100);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    if (process.state() != QProcess::NotRunning)
    {
        process.kill();
        process.waitForFinished(3000);
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return false;

    const QStringList fileNames = outputDir.entryList(
        {QStringLiteral("slide_*.png")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QString& fileName : fileNames)
    {
        const QString path = outputDir.absoluteFilePath(fileName);
        QImageReader reader(path);
        if (!reader.canRead())
        {
            imagePaths.clear();
            return false;
        }
        imagePaths.append(path);
    }

    return !imagePaths.isEmpty();
}

bool UBImportPowerPoint::convertWithPowerPoint(const QString& sourcePath, const QString& pdfPath)
{
    QFile::remove(pdfPath);

    const HRESULT initializeStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitializeWhenDone = SUCCEEDED(initializeStatus);
    if (FAILED(initializeStatus) && initializeStatus != RPC_E_CHANGED_MODE)
        return false;

    CLSID powerPointClass;
    IDispatch* powerPoint = nullptr;
    IDispatch* presentations = nullptr;
    IDispatch* presentation = nullptr;
    bool exported = false;

    if (SUCCEEDED(CLSIDFromProgID(L"PowerPoint.Application", &powerPointClass))
        && SUCCEEDED(CoCreateInstance(powerPointClass,
                                      nullptr,
                                      CLSCTX_LOCAL_SERVER,
                                      IID_IDispatch,
                                      reinterpret_cast<void**>(&powerPoint))))
    {
        VARIANT presentationsResult;
        VariantInit(&presentationsResult);
        if (SUCCEEDED(invokeAutomation(powerPoint,
                                       L"Presentations",
                                       DISPATCH_PROPERTYGET,
                                       nullptr,
                                       0,
                                       &presentationsResult))
            && presentationsResult.vt == VT_DISPATCH)
        {
            presentations = presentationsResult.pdispVal;
            presentationsResult.vt = VT_EMPTY;

            VARIANT openArguments[4];
            for (VARIANT& argument : openArguments)
                VariantInit(&argument);
            // IDispatch receives arguments in reverse order: WithWindow, Untitled, ReadOnly, FileName.
            openArguments[0].vt = VT_BOOL;
            openArguments[0].boolVal = VARIANT_FALSE;
            openArguments[1].vt = VT_BOOL;
            openArguments[1].boolVal = VARIANT_FALSE;
            openArguments[2].vt = VT_BOOL;
            openArguments[2].boolVal = VARIANT_TRUE;
            openArguments[3].vt = VT_BSTR;
            openArguments[3].bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(sourcePath.utf16()));

            VARIANT presentationResult;
            VariantInit(&presentationResult);
            const HRESULT openStatus = invokeAutomation(presentations,
                                                        L"Open",
                                                        DISPATCH_METHOD,
                                                        openArguments,
                                                        4,
                                                        &presentationResult);
            for (VARIANT& argument : openArguments)
                VariantClear(&argument);

            if (SUCCEEDED(openStatus) && presentationResult.vt == VT_DISPATCH)
            {
                presentation = presentationResult.pdispVal;
                presentationResult.vt = VT_EMPTY;

                VARIANT saveAsArguments[2];
                for (VARIANT& argument : saveAsArguments)
                    VariantInit(&argument);
                // PowerPoint's ExportAsFixedFormat dispatch signature varies between
                // Office builds. SaveAs with ppSaveAsPDF is the most compatible path.
                saveAsArguments[0].vt = VT_I4;
                saveAsArguments[0].lVal = 32; // ppSaveAsPDF
                saveAsArguments[1].vt = VT_BSTR;
                saveAsArguments[1].bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(pdfPath.utf16()));
                exported = automationMethod(presentation, L"SaveAs", saveAsArguments, 2);
                for (VARIANT& argument : saveAsArguments)
                    VariantClear(&argument);

                if (!exported || !isUsablePdf(pdfPath))
                {
                    QFile::remove(pdfPath);
                    VARIANT exportArguments[2];
                    for (VARIANT& argument : exportArguments)
                        VariantInit(&argument);
                    exportArguments[0].vt = VT_I4;
                    exportArguments[0].lVal = 2; // ppFixedFormatTypePDF
                    exportArguments[1].vt = VT_BSTR;
                    exportArguments[1].bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(pdfPath.utf16()));
                    exported = automationMethod(presentation, L"ExportAsFixedFormat", exportArguments, 2);
                    for (VARIANT& argument : exportArguments)
                        VariantClear(&argument);
                }
            }
            VariantClear(&presentationResult);
        }
        VariantClear(&presentationsResult);
    }

    if (presentation)
    {
        automationMethod(presentation, L"Close");
        presentation->Release();
    }
    if (presentations)
        presentations->Release();
    if (powerPoint)
    {
        automationMethod(powerPoint, L"Quit");
        powerPoint->Release();
    }
    if (uninitializeWhenDone)
        CoUninitialize();

    return exported && isUsablePdf(pdfPath);
}

bool UBImportPowerPoint::convertWithPowerShell(const QString& sourcePath, const QString& pdfPath)
{
    QString executable = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (executable.isEmpty())
    {
        const QString windowsDirectory = qEnvironmentVariable("SystemRoot");
        const QString candidate = QDir(windowsDirectory).filePath(
            QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
        if (QFileInfo::exists(candidate))
            executable = candidate;
    }

    if (executable.isEmpty())
        return false;

    QFile::remove(pdfPath);

    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "$sourcePath=[Environment]::GetEnvironmentVariable('OPENBOARD_PPT_SOURCE');"
        "$pdfPath=[Environment]::GetEnvironmentVariable('OPENBOARD_PPT_PDF');"
        "$powerPoint=New-Object -ComObject PowerPoint.Application;"
        "$presentation=$null;"
        "try {"
        " $presentation=$powerPoint.Presentations.Open($sourcePath,$true,$false,$false);"
        " $presentation.SaveAs($pdfPath,32);"
        "} finally {"
        " if($null -ne $presentation){"
        "  $presentation.Close();"
        "  [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($presentation);"
        " }"
        " $powerPoint.Quit();"
        " [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($powerPoint);"
        "}");

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OPENBOARD_PPT_SOURCE"), sourcePath);
    environment.insert(QStringLiteral("OPENBOARD_PPT_PDF"), pdfPath);
    process.setProcessEnvironment(environment);
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-NoLogo"),
                          QStringLiteral("-NoProfile"),
                          QStringLiteral("-NonInteractive"),
                          QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                          QStringLiteral("-Command"), script});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    process.start();
    if (!process.waitForStarted(5000))
        return false;

    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 300000)
    {
        process.waitForFinished(100);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    if (process.state() != QProcess::NotRunning)
    {
        process.kill();
        process.waitForFinished(3000);
        return false;
    }

    return process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0
        && isUsablePdf(pdfPath);
}
#endif

bool UBImportPowerPoint::convertWithLibreOffice(const QString& sourcePath, const QString& pdfPath)
{
    const QString executable = libreOfficeExecutable();
    if (executable.isEmpty())
        return false;

    QFile::remove(pdfPath);

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("--headless"),
                          QStringLiteral("--convert-to"), QStringLiteral("pdf"),
                          QStringLiteral("--outdir"), QFileInfo(pdfPath).absolutePath(),
                          sourcePath});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(5000))
        return false;

    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 300000)
    {
        process.waitForFinished(100);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    if (process.state() != QProcess::NotRunning)
    {
        process.kill();
        process.waitForFinished(3000);
        return false;
    }

    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && isUsablePdf(pdfPath);
}

bool UBImportPowerPoint::appendPdfPages(std::shared_ptr<UBDocumentProxy> document, const QString& pdfPath)
{
    QUuid uuid = QUuid::createUuid();
    QString storedPdfPath = pdfPath;
    if (!UBPersistenceManager::persistenceManager()->addFileToDocument(
            document, pdfPath, UBPersistenceManager::objectDirectory, uuid, storedPdfPath))
    {
        return false;
    }

    UBImportPDF pdfImporter;
    QList<UBGraphicsItem*> pages = pdfImporter.import(uuid, storedPdfPath);
    if (pages.isEmpty())
        return false;

    const int firstPage = document->pageCount();
    int pageIndex = firstPage;
    const auto openBoardDocument = UBDocument::getDocument(document);
    for (UBGraphicsItem* page : pages)
    {
        UBApplication::showMessage(
            tr("Importing slide %1 of %2...").arg(pageIndex - firstPage + 1).arg(pages.size()), true);
        std::shared_ptr<UBGraphicsScene> scene = openBoardDocument->createPage(pageIndex);
        pdfImporter.placeImportedItemToScene(scene, page);
        openBoardDocument->persistPage(scene, pageIndex);
        ++pageIndex;
    }

    UBPersistenceManager::persistenceManager()->persistDocumentMetadata(document);
    openBoardDocument->thumbnailScene()->createThumbnails(firstPage);
    return true;
}

void UBImportPowerPoint::showConversionError() const
{
    UBApplication::showMessage(tr("PowerPoint import failed."));
    QMessageBox::warning(nullptr,
                         tr("PowerPoint import failed"),
                         tr("The presentation could not be converted. Please install Microsoft PowerPoint or LibreOffice and try again."));
}
