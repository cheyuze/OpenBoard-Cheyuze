// Full-client default-page migration, creation and saved-document regression.
#define main oldPasteMain
#define runTests oldPasteTests
#define runWhenReady oldPasteReady
#include "paste_position_native.cpp"
#undef main
#undef runTests
#undef runWhenReady
#include "adaptors/UBSvgSubsetAdaptor.h"
#include "adaptors/UBImportPDF.h"
#include <QPdfWriter>

static void tests()
{
    auto* settings = UBSettings::settings();
    auto* board = UBApplication::boardController;
    auto* main = UBApplication::mainWindow;
    auto* persistence = UBPersistenceManager::persistenceManager();
    const QString testCase = qEnvironmentVariable("OPENBOARD_PAGE_DEFAULT_CASE");
    const bool expectedBorderless = !testCase.startsWith("remember-");
    const QSize expectedSize = testCase == "fresh" || testCase == "remember-wide"
        ? QSize(1600, 900) : QSize(1280, 960);
    check(settings->pageBorderless->get().toBool() == expectedBorderless,
          "fresh/upgrade defaults become borderless while later explicit choices remain intact");
    check(settings->pageSize->get().toSize() == expectedSize,
          "initialization does not overwrite saved nominal dimensions");
    settings->save();
    QSettings user(UBSettings::userDataDirectory() + "/OpenBoardUser.config", QSettings::IniFormat);
    check(user.value("TeamEdition/BorderlessCanvasDefaultApplied").toBool(),
          "one-time upgrade marker is persisted");
    check(user.value("Board/DefaultPageBorderless").toBool() == expectedBorderless,
          "page-mode preference is persisted independently of nominal size");

    UBApplication::applicationController->showBoard();
    board->paletteManager()->tipsPalette()->hide();
    main->setAnimated(false);
    main->resize(1440, 900);
    auto document = persistence->createDocument("Page-default-test", QUuid::createUuid().toString(),
                                               false, QString(), 0, false);
    auto doc = UBDocument::getDocument(document);
    doc->createPage(0);
    board->setActiveDocumentScene(document, 0, true, true);
    board->zoomRestore();
    settle();
    auto scene = board->activeScene();
    check(scene->property("borderlessPage").toBool() == expectedBorderless,
          "new document and its first page inherit the current default mode");
    auto* borderlessAction = main->findChild<QAction*>("actionBorderlessPageSize");
    check(borderlessAction && borderlessAction->isChecked() == expectedBorderless,
          "actual page-size menu agrees with the created page");
    if (testCase == "fresh") {
        check(main->grab().save(qEnvironmentVariable("OPENBOARD_FILL_TEST_OUTPUT") + "/default-borderless-1440.png"),
              "default borderless whiteboard captured without a paper outline");
    }
    board->addScene();
    check(board->activeScene()->property("borderlessPage").toBool() == expectedBorderless,
          "additional page inherits the document mode");

    board->setBorderlessPageSize(true);
    auto* stack = UBApplication::undoStack.data();
    const int history = stack->index();
    board->setWidePageSize(true);
    check(!board->activeScene()->property("borderlessPage").toBool()
              && !settings->pageBorderless->get().toBool() && stack->index() == history + 1,
          "manual wide mode persists as one normal undoable change");
    board->undo();
    check(board->activeScene()->property("borderlessPage").toBool()
              && settings->pageBorderless->get().toBool(), "undo restores the borderless mode and default");
    board->redo();
    check(!board->activeScene()->property("borderlessPage").toBool()
              && !settings->pageBorderless->get().toBool(), "redo restores fixed mode and default");
    board->setRegularPageSize(true);
    const int noOpHistory = stack->index();
    settings->pageBorderless->setBool(true);
    board->setRegularPageSize(true);
    check(!settings->pageBorderless->get().toBool() && stack->index() == noOpHistory,
          "explicitly reselecting an already-active fixed format updates the default without undo noise");
    auto nextDocument = persistence->createDocument("Page-default-test", QUuid::createUuid().toString(),
                                                   false, QString(), 0, false);
    auto nextPage = UBDocument::getDocument(nextDocument)->createPage(0);
    check(!nextPage->property("borderlessPage").toBool()
              && nextPage->nominalSize() == QSize(1280, 960),
          "next new document remembers an explicitly selected 4:3 mode");
    board->setBorderlessPageSize(true);
    check(settings->pageBorderless->get().toBool(), "explicit borderless selection can become the default again");

    // Saved page attributes, not today's default, remain authoritative.
    for (bool borderless : {false, true}) {
        nextPage->setNominalSize(1280, 960);
        nextPage->setProperty("borderlessPage", borderless);
        UBSvgSubsetAdaptor::persistScene(nextDocument, nextPage, 0);
        auto restored = UBSvgSubsetAdaptor::loadScene(nextDocument, 0);
        check(restored && restored->property("borderlessPage").toBool() == borderless
                  && restored->nominalSize() == QSize(1280, 960),
              "save/reopen keeps each existing page's mode and original dimensions");
    }
    UBSvgSubsetAdaptor::persistScene(nextDocument, nextPage, 0);
    const QString svgPath = nextDocument->persistencePath() + "/page000.svg";
    QFile legacy(svgPath);
    check(legacy.open(QIODevice::ReadOnly), "isolated saved page is available for legacy-attribute fixture");
    QByteArray oldSvg = legacy.readAll();
    legacy.close();
    check(oldSvg.contains("ub:borderless-page=\"true\""), "legacy fixture starts from an explicitly saved borderless page");
    oldSvg.replace("ub:borderless-page=\"true\"", "");
    check(legacy.open(QIODevice::WriteOnly | QIODevice::Truncate), "legacy fixture write stays in the isolated profile");
    check(legacy.write(oldSvg) == oldSvg.size(), "legacy page fixture written completely");
    legacy.close();
    auto restoredLegacy = UBSvgSubsetAdaptor::loadScene(nextDocument, 0);
    check(restoredLegacy && !restoredLegacy->property("borderlessPage").toBool()
              && restoredLegacy->nominalSize() == QSize(1280, 960),
          "old pages without a borderless attribute remain fixed rather than being migrated");

    // PDF/PPT imports keep their source format despite the new blank-page default.
    const QString pdfPath = UBSettings::userDataDirectory() + "/page-default-fixture.pdf";
    {
        QPdfWriter pdf(pdfPath);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&pdf);
        painter.drawText(100, 100, "Isolated import regression");
    }
    UBImportPDF importer;
    const auto pdfItems = importer.import(QUuid::createUuid(), pdfPath);
    check(pdfItems.size() == 1, "synthetic one-page PDF uses the real import adaptor");
    auto imported = std::make_shared<UBGraphicsScene>(document, true);
    check(imported->property("borderlessPage").toBool(), "blank import destination initially uses borderless default");
    importer.placeImportedItemToScene(imported, pdfItems.first());
    check(!imported->property("borderlessPage").toBool()
              && imported->nominalSize().height() > imported->nominalSize().width(),
          "PDF/PPT page placement restores source paper format without changing the global preference");
    check(settings->pageBorderless->get().toBool(), "opening/importing saved formats does not reset the user's default");
    std::cout << "PASS " << checks << " page-default checks (" << testCase.toStdString() << ")" << std::endl;
}

static void ready()
{
    if (QThread::currentThread()->loopLevel() == 0 || !UBApplication::boardController
            || !UBApplication::boardController->activeScene()) {
        QTimer::singleShot(100, qApp, ready);
        return;
    }
    try { tests(); std::_Exit(0); }
    catch (const std::exception& error) { std::cerr << error.what() << std::endl; std::_Exit(1); }
}

int main(int argc, char** argv)
{
    const QString profile = qEnvironmentVariable("OPENBOARD_PASTE_TEST_PROFILE");
    const QString testCase = qEnvironmentVariable("OPENBOARD_PAGE_DEFAULT_CASE");
    if (qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen" || profile.isEmpty()
            || !QFileInfo(profile).fileName().startsWith("paste-position-profile-")
            || !QStringList({"fresh", "upgrade", "remember-wide", "remember-regular"}).contains(testCase)) return 2;
    if (testCase != "fresh") {
        QDir().mkpath(profile);
        QSettings seed(profile + "/OpenBoardUser.config", QSettings::IniFormat);
        seed.setValue("Board/DefaultPageBorderless", false);
        seed.setValue("Board/DefaultPageSize", testCase == "remember-wide" ? QSize(1600, 900) : QSize(1280, 960));
        seed.setValue("TeamEdition/WideCanvasDefaultApplied", true);
        if (testCase.startsWith("remember-")) seed.setValue("TeamEdition/BorderlessCanvasDefaultApplied", true);
        seed.sync();
        if (seed.status() != QSettings::NoError) return 2;
    }
    Q_INIT_RESOURCE(OpenBoard);
    UBApplication app("OpenBoard-page-default-tests", argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QApplication::setFont(QFont("Microsoft YaHei UI", 9));
    if (QDir::cleanPath(UBSettings::userDataDirectory()) != QDir::cleanPath(profile)) return 2;
    UBSettings::settings()->appStartupHintsEnabled->setBool(false);
    UBSettings::settings()->appStartupBehavior->set(UBSettings::ShowDocumentHistory);
    QTimer::singleShot(500, qApp, ready);
    QTimer::singleShot(120000, qApp, [] { std::_Exit(4); });
    return app.exec("");
}
