#include <QtGui>
#include "adaptors/UBImportAdaptor.h"
#include "core/UBPersistenceManager.h"
#include "document/UBDocument.h"
#include "gui/UBThumbnailScene.h"
#include "frameworks/UBFileSystemUtils.h"
#include "adaptors/UBSvgSubsetAdaptor.h"
UBImportAdaptor::UBImportAdaptor(bool value, QObject* parent) : QObject(parent), documentBased(value) {}
UBImportAdaptor::~UBImportAdaptor() = default;
UBDocumentBasedImportAdaptor::UBDocumentBasedImportAdaptor(QObject* parent) : UBImportAdaptor(true,parent) {}
UBPageBasedImportAdaptor::UBPageBasedImportAdaptor(QObject* parent) : UBImportAdaptor(false,parent) {}
// Exact production method, extracted without edits and checked by the test;
// only the application-level collaborators above are replaced with test stubs.
#include "persistence-method.inc"
