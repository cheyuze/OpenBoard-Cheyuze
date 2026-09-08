/*
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 */

#ifndef UBIMPORTPOWERPOINT_H_
#define UBIMPORTPOWERPOINT_H_

#include <QtGui>

#include "UBImportAdaptor.h"

class UBImportPowerPoint : public UBDocumentBasedImportAdaptor
{
    Q_OBJECT

public:
    explicit UBImportPowerPoint(QObject* parent = nullptr);
    ~UBImportPowerPoint() override;

    QStringList supportedExtentions() override;
    QString importFileFilter() override;

    std::shared_ptr<UBDocumentProxy> importFile(const QFile& file, const QString& group) override;
    bool addFileToDocument(std::shared_ptr<UBDocumentProxy> document, const QFile& file) override;

private:
    bool appendPresentationPages(std::shared_ptr<UBDocumentProxy> document,
                                 const QString& sourcePath,
                                 const QString& temporaryPath);
    bool appendImagePages(std::shared_ptr<UBDocumentProxy> document, const QStringList& imagePaths);
    bool convertToPdf(const QString& sourcePath, const QString& pdfPath);
    bool convertWithLibreOffice(const QString& sourcePath, const QString& pdfPath);
#ifdef Q_OS_WIN
    bool exportImagesWithPowerShell(const QString& sourcePath,
                                    const QString& outputDirectory,
                                    QStringList& imagePaths);
    bool convertWithPowerPoint(const QString& sourcePath, const QString& pdfPath);
    bool convertWithPowerShell(const QString& sourcePath, const QString& pdfPath);
#endif
    bool appendPdfPages(std::shared_ptr<UBDocumentProxy> document, const QString& pdfPath);
    void showConversionError() const;
};

#endif // UBIMPORTPOWERPOINT_H_
