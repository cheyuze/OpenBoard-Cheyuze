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
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <oleauto.h>
#endif

#include "UBImportPDF.h"

#include "core/UBApplication.h"
#include "core/UBPersistenceManager.h"
#include "core/UBSettings.h"

#include "document/UBDocument.h"
#include "document/UBDocumentProxy.h"

#include "gui/UBThumbnailScene.h"
#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsPixmapItem.h"

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
QPointer<QProcess> pendingPowerPointCleanup;

void waitForPowerPointCleanup()
{
    // PowerPoint may reuse the same COM server. Do not open the next import
    // while a previous worker is asking that server to quit.
    QElapsedTimer timer;
    timer.start();
    while (pendingPowerPointCleanup
           && pendingPowerPointCleanup->state() != QProcess::NotRunning
           && timer.elapsed() < 30000) {
        pendingPowerPointCleanup->waitForFinished(50);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    if (pendingPowerPointCleanup && pendingPowerPointCleanup->state() != QProcess::NotRunning) {
        // Only our hidden PowerShell client, never a user's POWERPNT process.
        pendingPowerPointCleanup->kill();
        pendingPowerPointCleanup->waitForFinished(1000);
    }
}

void finishPowerPointInBackground(std::unique_ptr<QProcess>& process)
{
    if (process->state() == QProcess::NotRunning) return;
    auto* worker = process.release();
    worker->setParent(QCoreApplication::instance());
    worker->setObjectName(QStringLiteral("OpenBoardPowerPointCleanup"));
    pendingPowerPointCleanup = worker;
    QObject::connect(worker, &QProcess::finished, worker, &QObject::deleteLater);
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, worker, [worker] {
        if (worker->state() != QProcess::NotRunning) {
            worker->kill();
            worker->waitForFinished(1000);
        }
    });
    QTimer::singleShot(30000, worker, [worker] {
        if (worker->state() != QProcess::NotRunning) worker->kill();
    });
    if (worker->state() == QProcess::NotRunning) worker->deleteLater();
}

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
    DISPID propertyPut = DISPID_PROPERTYPUT;
    if (flags & DISPATCH_PROPERTYPUT) {
        parameters.rgdispidNamedArgs = &propertyPut;
        parameters.cNamedArgs = 1;
    }

    EXCEPINFO exception = {};
    UINT argumentError = 0;
    const HRESULT invokeStatus = object->Invoke(memberId,
                          IID_NULL,
                          LOCALE_USER_DEFAULT,
                          flags,
                          &parameters,
                          result,
                          &exception,
                          &argumentError);
    SysFreeString(exception.bstrSource);
    SysFreeString(exception.bstrDescription);
    SysFreeString(exception.bstrHelpFile);
    return invokeStatus;
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

    for (const QString& imagePath : imagePaths)
    {
        UBApplication::showMessage(
            tr("Importing slide %1 of %2...").arg(pageIndex - firstPage + 1).arg(imagePaths.size()), true);

        QFile imageFile(imagePath);
        if (!imageFile.open(QIODevice::ReadOnly)) return false;
        const QByteArray imageData = imageFile.readAll();
        if (imageData.isEmpty()) return false;
        auto scene = std::make_shared<UBGraphicsScene>(document, false);
        // createPage previously initialized nominalSize while saving the blank
        // page. Initialize it before fitting the image when batching imports.
        scene->setNominalSize(document->defaultDocumentSize());
        scene->setBackground(UBSettings::settings()->isDarkBackground(), UBSettings::settings()->pageBackground());
        scene->setBackgroundGridSize(UBSettings::settings()->crossSize);
        // Decode the exported PNG only once. UBImportImage used to decode a
        // disposable pixmap and then decode the same image again in addImage.
        auto* background = scene->addImage(imageData, nullptr, QPointF(0,0), 1.0, false, true);
        if (!background || background->pixmap().isNull()) return false;
        scene->setAsBackgroundObject(background, true);
        openBoardDocument->appendImportedPage(scene);
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
    waitForPowerPointCleanup();
    QString executable = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (executable.isEmpty()) {
        const QString candidate = QDir(qEnvironmentVariable("SystemRoot")).filePath(
            QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
        if (QFileInfo::exists(candidate)) executable = candidate;
    }
    QDir outputDir(outputDirectory);
    if (executable.isEmpty() || (!outputDir.exists() && !QDir().mkpath(outputDirectory))) return false;

    const QString script = QStringLiteral(R"powershell(
$ErrorActionPreference='Stop'
$sourcePath=[IO.Path]::GetFullPath([Environment]::GetEnvironmentVariable('OPENBOARD_PPT_SOURCE'))
$outputDir=[IO.Path]::GetFullPath([Environment]::GetEnvironmentVariable('OPENBOARD_PPT_IMAGE_DIR'))
$powerPoint=$null;$presentations=$null;$presentation=$null;$slides=$null;$pageSetup=$null
$initialCount=-1;$oldAlerts=$null;$oldSecurity=$null;$initialVisible=$null;$openedByUs=$false
function Release-Com($value) {
    if($null -ne $value) { try { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($value) } catch {} }
}
try {
    $powerPoint=New-Object -ComObject PowerPoint.Application
    $presentations=$powerPoint.Presentations
    $initialCount=[int]$presentations.Count
    $initialVisible=[int]$powerPoint.Visible
    $oldAlerts=$powerPoint.DisplayAlerts
    $oldSecurity=$powerPoint.AutomationSecurity
    $powerPoint.DisplayAlerts=1
    $powerPoint.AutomationSecurity=3
    $normalizedSource=[IO.Path]::GetFullPath($sourcePath)
    for($existing=1;$existing -le $initialCount;$existing++) {
        $candidate=$presentations.Item($existing)
        try {
            if([String]::Equals([string]$candidate.FullName,$normalizedSource,[StringComparison]::OrdinalIgnoreCase)) {
                $presentation=$candidate;$candidate=$null;break
            }
        } finally { Release-Com $candidate }
    }
    for($attempt=1;$attempt -le 3 -and $null -eq $presentation;$attempt++) {
        try { $presentation=$presentations.Open($sourcePath,$true,$false,$false);$openedByUs=$true }
        catch { if($attempt -eq 3) { throw }; Start-Sleep -Milliseconds 800 }
    }
    $pageSetup=$presentation.PageSetup
    $slideWidth=[double]$pageSetup.SlideWidth
    $slideHeight=[double]$pageSetup.SlideHeight
    if($slideWidth -le 0 -or $slideHeight -le 0) { throw 'Invalid slide dimensions.' }
    $imageWidth=1920
    $imageHeight=[Math]::Max(1,[int][Math]::Round($imageWidth*$slideHeight/$slideWidth))
    $slides=$presentation.Slides
    $count=[int]$slides.Count
    if($count -lt 1) { throw 'The presentation contains no slides.' }
    for($index=1;$index -le $count;$index++) {
        $slide=$null
        try {
            $slide=$slides.Item($index)
            $file=Join-Path $outputDir ('slide_{0:D6}.png' -f $index)
            for($attempt=1;$attempt -le 3;$attempt++) {
                try { $slide.Export($file,'PNG',$imageWidth,$imageHeight); break }
                catch { if($attempt -eq 3) { throw }; Start-Sleep -Milliseconds 500 }
            }
            if(!(Test-Path -LiteralPath $file) -or (Get-Item -LiteralPath $file).Length -le 0) {
                throw ('Slide export failed: '+$index)
            }
        } finally { Release-Com $slide }
        [Console]::Out.WriteLine(('OPENBOARD_PROGRESS {0} {1}' -f $index,$count))
        [Console]::Out.Flush()
    }
    Release-Com $slides; $slides=$null
    Release-Com $pageSetup; $pageSetup=$null
    if($openedByUs) { $presentation.Close() }
    Release-Com $presentation; $presentation=$null
    $powerPoint.AutomationSecurity=$oldSecurity
    $powerPoint.DisplayAlerts=$oldAlerts
    # Our source is closed (an already-open user document is left untouched)
    # and every PNG is complete before acknowledging it.
    # Office Quit/finalizer latency must not block importing those ready pages.
    [Console]::Out.WriteLine(('OPENBOARD_DONE {0}' -f $count))
    [Console]::Out.Flush()
} catch { Write-Error $_; exit 1 }
finally {
    Release-Com $slides
    Release-Com $pageSetup
    if($null -ne $presentation) {
        if($openedByUs) { try { $presentation.Close() } catch {} }
        Release-Com $presentation
    }
    $quit=$false
    if($null -ne $powerPoint) {
        try {
            if($null -ne $oldSecurity) { $powerPoint.AutomationSecurity=$oldSecurity }
            if($null -ne $oldAlerts) { $powerPoint.DisplayAlerts=$oldAlerts }
            $quit=($initialCount -eq 0 -and $initialVisible -eq 0 -and [int]$presentations.Count -eq 0 -and [int]$powerPoint.Visible -eq 0)
        } catch {}
    }
    Release-Com $presentations
    if($quit) { try { $powerPoint.Quit() } catch {} }
    Release-Com $powerPoint
}
)powershell");
    const QByteArray encodedScript(reinterpret_cast<const char*>(script.utf16()),
                                   script.size()*static_cast<int>(sizeof(char16_t)));
    auto process = std::make_unique<QProcess>();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OPENBOARD_PPT_SOURCE"), sourcePath);
    environment.insert(QStringLiteral("OPENBOARD_PPT_IMAGE_DIR"), outputDirectory);
    process->setProcessEnvironment(environment);
    process->setProgram(executable);
    process->setArguments({QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"), QStringLiteral("-Sta"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-EncodedCommand"), QString::fromLatin1(encodedScript.toBase64())});
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    process->start();
    if (!process->waitForStarted(5000)) return false;

    int expectedPages = 0;
    QByteArray diagnostics;
    const auto readProgress = [&] {
        while (process->canReadLine()) {
            const QByteArray line = process->readLine().trimmed();
            const auto fields = line.split(' ');
            if (fields.size() == 3 && fields[0] == "OPENBOARD_PROGRESS") {
                bool currentOk = false, countOk = false;
                const int current = fields[1].toInt(&currentOk), count = fields[2].toInt(&countOk);
                if (currentOk && countOk && current > 0 && current <= count)
                    UBApplication::showMessage(tr("Importing slide %1 of %2...").arg(current).arg(count), true);
            } else if (fields.size() == 2 && fields[0] == "OPENBOARD_DONE") {
                bool ok = false;
                const int count = fields[1].toInt(&ok);
                if (ok && count > 0) expectedPages = count;
            } else {
                diagnostics.append(line + '\n');
                if (diagnostics.size() > 32768) diagnostics = diagnostics.right(32768);
            }
        }
    };
    QElapsedTimer timer;
    timer.start();
    while (expectedPages == 0 && process->state() != QProcess::NotRunning && timer.elapsed() < 600000) {
        process->waitForReadyRead(50);
        readProgress();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    readProgress();
    if (expectedPages == 0) {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
        qWarning() << "PowerPoint image conversion failed:" << diagnostics.right(4096);
        return false;
    }
    // Check the acknowledged count and exact numeric order, not just whatever
    // files happen to exist after a partial export or process failure.
    for (int index=1; index<=expectedPages; ++index) {
        const QString path = outputDir.filePath(QStringLiteral("slide_%1.png").arg(index,6,10,QLatin1Char('0')));
        QImageReader reader(path);
        if (!reader.canRead() || !reader.size().isValid()) {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
                process->waitForFinished(1000);
            }
            imagePaths.clear();
            return false;
        }
        imagePaths.append(path);
    }
    qInfo() << "PowerPoint slides ready:" << expectedPages << "in" << timer.elapsed() << "ms";
    finishPowerPointInBackground(process);
    return true;
}

bool UBImportPowerPoint::convertWithPowerPoint(const QString& sourcePath, const QString& pdfPath)
{
    QFile::remove(pdfPath);
    // Office SaveAs rejects some otherwise valid Qt paths with '/' separators.
    const QString nativeSource = QDir::toNativeSeparators(QFileInfo(sourcePath).absoluteFilePath());
    const QString nativePdf = QDir::toNativeSeparators(QFileInfo(pdfPath).absoluteFilePath());

    const HRESULT initializeStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitializeWhenDone = SUCCEEDED(initializeStatus);
    if (FAILED(initializeStatus) && initializeStatus != RPC_E_CHANGED_MODE)
        return false;

    CLSID powerPointClass;
    IDispatch* powerPoint = nullptr;
    IDispatch* presentations = nullptr;
    IDispatch* presentation = nullptr;
    bool exported = false;
    bool mayQuit = false;
    VARIANT previousSecurity;
    VariantInit(&previousSecurity);
    bool securityChanged = false;

    if (SUCCEEDED(CLSIDFromProgID(L"PowerPoint.Application", &powerPointClass))
        && SUCCEEDED(CoCreateInstance(powerPointClass,
                                      nullptr,
                                      CLSCTX_LOCAL_SERVER,
                                      IID_IDispatch,
                                      reinterpret_cast<void**>(&powerPoint))))
    {
        VARIANT disabledMacros;
        VariantInit(&disabledMacros);
        disabledMacros.vt = VT_I4;
        disabledMacros.lVal = 3; // msoAutomationSecurityForceDisable
        securityChanged = SUCCEEDED(invokeAutomation(powerPoint, L"AutomationSecurity", DISPATCH_PROPERTYGET,
                                                     nullptr, 0, &previousSecurity))
            && SUCCEEDED(invokeAutomation(powerPoint, L"AutomationSecurity", DISPATCH_PROPERTYPUT,
                                          &disabledMacros, 1, nullptr));
        VARIANT presentationsResult;
        VariantInit(&presentationsResult);
        if (securityChanged && SUCCEEDED(invokeAutomation(powerPoint,
                                       L"Presentations",
                                       DISPATCH_PROPERTYGET,
                                       nullptr,
                                       0,
                                       &presentationsResult))
            && presentationsResult.vt == VT_DISPATCH)
        {
            presentations = presentationsResult.pdispVal;
            presentationsResult.vt = VT_EMPTY;
            VARIANT count;
            VariantInit(&count);
            mayQuit = SUCCEEDED(invokeAutomation(presentations, L"Count", DISPATCH_PROPERTYGET, nullptr, 0, &count))
                && count.vt == VT_I4 && count.lVal == 0;
            VariantClear(&count);
            VARIANT visible;
            VariantInit(&visible);
            mayQuit = mayQuit && SUCCEEDED(invokeAutomation(powerPoint, L"Visible", DISPATCH_PROPERTYGET, nullptr, 0, &visible))
                && ((visible.vt == VT_I4 && visible.lVal == 0)
                    || (visible.vt == VT_BOOL && visible.boolVal == VARIANT_FALSE));
            VariantClear(&visible);

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
            openArguments[3].bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(nativeSource.utf16()));

            VARIANT presentationResult;
            VariantInit(&presentationResult);
            // Shared-user sessions are handled by the isolated worker below;
            // never reopen/SaveAs/Close a user's existing presentation here.
            const HRESULT openStatus = mayQuit ? invokeAutomation(presentations,
                                                        L"Open",
                                                        DISPATCH_METHOD,
                                                        openArguments,
                                                        4,
                                                        &presentationResult) : E_ACCESSDENIED;
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
                saveAsArguments[1].bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(nativePdf.utf16()));
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
                    exportArguments[0].lVal = 32; // ppSaveAsPDF
                    exportArguments[1].vt = VT_BSTR;
                    exportArguments[1].bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(nativePdf.utf16()));
                    exported = automationMethod(presentation, L"SaveCopyAs", exportArguments, 2);
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
    if (powerPoint)
    {
        if (securityChanged)
            invokeAutomation(powerPoint, L"AutomationSecurity", DISPATCH_PROPERTYPUT, &previousSecurity, 1, nullptr);
        if (mayQuit && presentations) {
            VARIANT count;
            VariantInit(&count);
            const bool noOpenDocuments = SUCCEEDED(invokeAutomation(presentations, L"Count", DISPATCH_PROPERTYGET, nullptr, 0, &count))
                && count.vt == VT_I4 && count.lVal == 0;
            VariantClear(&count);
            if (noOpenDocuments) automationMethod(powerPoint, L"Quit");
        }
        powerPoint->Release();
    }
    if (presentations) presentations->Release();
    VariantClear(&previousSecurity);
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

    const QString script = QStringLiteral(R"powershell(
$ErrorActionPreference='Stop'
$sourcePath=[IO.Path]::GetFullPath([Environment]::GetEnvironmentVariable('OPENBOARD_PPT_SOURCE'))
$pdfPath=[IO.Path]::GetFullPath([Environment]::GetEnvironmentVariable('OPENBOARD_PPT_PDF'))
$powerPoint=$null;$presentations=$null;$presentation=$null
$initialCount=-1;$initialVisible=$null;$oldSecurity=$null;$openedByUs=$false
function Release-Com($value) {
    if($null -ne $value) { try { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($value) } catch {} }
}
try {
    $powerPoint=New-Object -ComObject PowerPoint.Application
    $presentations=$powerPoint.Presentations
    $initialCount=[int]$presentations.Count
    $initialVisible=[int]$powerPoint.Visible
    $oldSecurity=$powerPoint.AutomationSecurity
    $powerPoint.AutomationSecurity=3
    $normalizedSource=[IO.Path]::GetFullPath($sourcePath)
    for($existing=1;$existing -le $initialCount;$existing++) {
        $candidate=$presentations.Item($existing)
        try {
            if([String]::Equals([string]$candidate.FullName,$normalizedSource,[StringComparison]::OrdinalIgnoreCase)) {
                $presentation=$candidate;$candidate=$null;break
            }
        } finally { Release-Com $candidate }
    }
    if($null -eq $presentation) {
        $presentation=$presentations.Open($sourcePath,$true,$false,$false);$openedByUs=$true
        $presentation.SaveAs($pdfPath,32)
    } else {
        # Export a copy without renaming or changing the user's open document.
        $presentation.SaveCopyAs($pdfPath,32)
    }
} finally {
    if($null -ne $presentation) {
        if($openedByUs) { try { $presentation.Close() } catch {} }
        Release-Com $presentation
    }
    $quit=$false
    if($null -ne $powerPoint) {
        try {
            if($null -ne $oldSecurity) { $powerPoint.AutomationSecurity=$oldSecurity }
            $quit=($initialCount -eq 0 -and $initialVisible -eq 0 -and [int]$presentations.Count -eq 0 -and [int]$powerPoint.Visible -eq 0)
        } catch {}
    }
    Release-Com $presentations
    if($quit) { try { $powerPoint.Quit() } catch {} }
    Release-Com $powerPoint
}
)powershell");

    const QByteArray encodedScript(reinterpret_cast<const char*>(script.utf16()),
                                   script.size()*static_cast<int>(sizeof(char16_t)));
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OPENBOARD_PPT_SOURCE"), sourcePath);
    environment.insert(QStringLiteral("OPENBOARD_PPT_PDF"), pdfPath);
    process.setProcessEnvironment(environment);
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-NoLogo"),
                          QStringLiteral("-NoProfile"),
                          QStringLiteral("-NonInteractive"),
                          QStringLiteral("-Sta"),
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

    const bool success = process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0 && isUsablePdf(pdfPath);
    if (!success)
        qWarning() << "PowerPoint PDF conversion failed:" << process.readAll().right(4096);
    return success;
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
        auto scene = std::make_shared<UBGraphicsScene>(document, false);
        scene->setNominalSize(document->defaultDocumentSize());
        scene->setBackground(UBSettings::settings()->isDarkBackground(), UBSettings::settings()->pageBackground());
        scene->setBackgroundGridSize(UBSettings::settings()->crossSize);
        pdfImporter.placeImportedItemToScene(scene, page);
        openBoardDocument->appendImportedPage(scene);
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
