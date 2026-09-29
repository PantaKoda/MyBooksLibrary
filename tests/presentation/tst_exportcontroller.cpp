// Presentation (M09): the Export dialog's session, end to end with the real
// SDK. A PDF is imported and analyzed, the preview shows what the copy gets,
// the copy is written through the job queue, an existing file is replaced only
// after confirmation, the library folder is refused, and the real
// ExportDialog.qml shows the preview and saves through the same session.
#include "presentation/exportcontroller.h"
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "presentation/booklistmodel.h"
#include "presentation/joblistmodel.h"
#include "presentation/librarycontroller.h"
#include "processing/sdk/sdkbookexporter.h"
#include "processing/sdk/sdkcontentsanalyzer.h"
#include "processing/sdk/sdkmetadataextractor.h"
#include "storage/filecopy.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

Q_IMPORT_QML_PLUGIN(MyBooksLibrary_PresentationPlugin)

using mbl::presentation::ExportController;
using mbl::presentation::LibraryController;
using Phase = ExportController::Phase;
using namespace mbl::domain;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

QQuickItem* findItem(QQuickItem* root, const QString& name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItem(child, name))
            return found;
    }
    return nullptr;
}

} // namespace

class TestExportController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void previewShowsWhatTheCopyGets();
    void savesACopyAndReplacesOnlyAfterConfirming();
    void refusalsAreExplained();
    void theDialogPreviewsAndSaves();
    void theDialogFitsASmallWindow();
    void reopeningFollowsACopyBeingSaved();  // Stops processing: keep it after the tests that save.
    void partialCoverageIsSpelledOut();

private:
    void waitForIdle();
    void prepared(const QString& bookId);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QTemporaryDir> m_out;
    std::unique_ptr<LibraryController> m_controller;
    QString m_book;
    QString m_managedSha;
};

void TestExportController::waitForIdle()
{
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->ready() && !m_controller->busy() && m_controller->jobs()->pendingCount() == 0,
                             60000);
}

void TestExportController::prepared(const QString& bookId)
{
    m_controller->exporter()->prepare(bookId);
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->exporter()->loading(), 10000);
}

void TestExportController::initTestCase()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_out = std::make_unique<QTemporaryDir>();
    m_controller = std::make_unique<LibraryController>();
    m_controller->setProcessors(std::make_shared<mbl::sdk::SdkMetadataExtractor>(),
                                std::make_shared<mbl::sdk::SdkContentsAnalyzer>(), false);
    m_controller->setExporter(std::make_shared<mbl::sdk::SdkBookExporter>());
    m_controller->open(m_dir->filePath(QStringLiteral("Library")));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->ready(), 10000);
    // Imported under a non-ASCII name, then analyzed by the real SDK.
    const QString source = m_out->filePath(QStringLiteral("Βιβλίο.pdf"));
    QVERIFY(QFile::copy(fixture("contents-book.pdf"), source));
    m_controller->importFiles({source});
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->books()->rowCount(), 1, 20000);
    waitForIdle();
    m_book = m_controller->books()->bookIdAt(0);
    QVERIFY(!m_book.isEmpty());
    m_managedSha = mbl::storage::sha256OfFile(source).value_or(QString());
}

void TestExportController::cleanupTestCase()
{
    if (m_controller) {
        m_controller->prepareToClose();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), 30000);
    }
    m_controller.reset();
}

void TestExportController::previewShowsWhatTheCopyGets()
{
    prepared(m_book);
    ExportController* e = m_controller->exporter();
    QVERIFY2(e->canExport(), qPrintable(e->problem()));
    QVERIFY(e->problem().isEmpty());
    QVERIFY(e->bookmarkCount() > 0);
    QVERIFY(e->summary().contains(QStringLiteral("bookmark")));
    QVERIFY(e->summary().contains(QStringLiteral("not changed")));
    // Partial coverage is spelled out entry by entry, never hidden.
    QCOMPARE(e->complete(), e->notes().isEmpty());
    for (const QString& note : e->notes())
        QVERIFY(note.startsWith(QStringLiteral("Left out:")) || note.startsWith(QStringLiteral("Moved:"))
                || note.startsWith(QStringLiteral("Top level:")));
    QVERIFY(e->suggestedPath().endsWith(QStringLiteral(" (bookmarked).pdf")));
    QVERIFY(e->suggestedFolder().isLocalFile());
    QCOMPARE(e->phase(), Phase::Idle);
    QVERIFY(e->lastExportText().isEmpty());
}

void TestExportController::savesACopyAndReplacesOnlyAfterConfirming()
{
    prepared(m_book);
    ExportController* e = m_controller->exporter();
    const QString output = m_out->filePath(QStringLiteral("Βιβλίο (bookmarked).pdf"));
    e->exportTo(QUrl::fromLocalFile(output).toString());  // A file dialog's URL works too.
    QVERIFY(e->running());
    QTRY_COMPARE_WITH_TIMEOUT(e->phase(), Phase::Saved, 30000);
    QVERIFY(QFile::exists(output));
    QCOMPARE(QDir::cleanPath(e->resultPath()), QDir::cleanPath(output));
    QVERIFY(e->phaseText().contains(QStringLiteral("bookmark")));
    QVERIFY(e->resultFolder().isLocalFile());
    const QString written = mbl::storage::sha256OfFile(output).value_or(QString());
    QVERIFY(!written.isEmpty());
    QVERIFY(written != m_managedSha);  // A new file, with bookmarks.

    // Again to the same name: asks first, replaces only when confirmed.
    e->exportTo(output);
    QTRY_COMPARE_WITH_TIMEOUT(e->phase(), Phase::NeedsReplace, 10000);
    QCOMPARE(QDir::cleanPath(e->pendingPath()), QDir::cleanPath(output));
    QVERIFY(e->phaseText().contains(QStringLiteral("already exists")));
    e->declineReplace();
    QCOMPARE(e->phase(), Phase::Idle);
    QCOMPARE(mbl::storage::sha256OfFile(output).value_or(QString()), written);
    e->exportTo(output);
    QTRY_COMPARE_WITH_TIMEOUT(e->phase(), Phase::NeedsReplace, 10000);
    e->confirmReplace();
    QTRY_COMPARE_WITH_TIMEOUT(e->phase(), Phase::Saved, 30000);

    // The next preview names the last copy.
    prepared(m_book);
    QVERIFY(e->lastExportText().contains(QStringLiteral("Last copy: saved")));
    QVERIFY(e->lastExportText().contains(QDir::toNativeSeparators(output)));
}

void TestExportController::refusalsAreExplained()
{
    prepared(m_book);
    ExportController* e = m_controller->exporter();
    e->exportTo(m_dir->filePath(QStringLiteral("Library/inside.pdf")));
    QTRY_COMPARE_WITH_TIMEOUT(e->phase(), Phase::NotSaved, 10000);
    QVERIFY(e->phaseText().contains(QStringLiteral("inside the library folder")));
    e->exportTo(QString());
    QCOMPARE(e->phase(), Phase::NotSaved);

    // A book in Trash: no export, and the reason.
    m_controller->moveToTrash(m_book);
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->trashCount() == 1, 10000);
    prepared(m_book);
    QVERIFY(!e->canExport());
    QVERIFY(e->problem().contains(QStringLiteral("Trash")));
    e->exportTo(m_out->filePath(QStringLiteral("trashed.pdf")));
    QCOMPARE(e->phase(), Phase::Idle);  // Ignored.
    m_controller->restoreFromTrash(m_book);
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->trashCount() == 0, 10000);
    waitForIdle();

    prepared(QStringLiteral("not-a-book"));
    QVERIFY(!e->canExport());
    QVERIFY(!e->problem().isEmpty());
}

void TestExportController::theDialogPreviewsAndSaves()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/export/ExportDialog.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(700, 700);
    std::unique_ptr<QObject> dialog(component.createWithInitialProperties(
        {{QStringLiteral("exporter"), QVariant::fromValue<QObject*>(m_controller->exporter())},
         {QStringLiteral("parent"), QVariant::fromValue<QObject*>(window.contentItem())}}));
    QVERIFY2(dialog, qPrintable(component.errorString()));
    window.show();

    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "openFor", Q_ARG(QVariant, m_book)));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    auto* content = qvariant_cast<QQuickItem*>(dialog->property("contentItem"));
    QVERIFY(content);
    QQuickItem* pathField = findItem(content, QStringLiteral("exportPathField"));
    QVERIFY(pathField);
    // The suggested name arrives with the preview.
    QTRY_VERIFY_WITH_TIMEOUT(pathField->property("text").toString().endsWith(QStringLiteral("(bookmarked).pdf")), 10000);
    QQuickItem* summary = findItem(content, QStringLiteral("exportSummary"));
    QVERIFY(summary && summary->property("text").toString().contains(QStringLiteral("bookmark")));

    // For the PR: MBL_SCREENSHOT_DIR=<folder> saves the preview, with a
    // neutral example path (the test's own folder names the machine's user).
    if (const QString shots = qEnvironmentVariable("MBL_SCREENSHOT_DIR"); !shots.isEmpty()) {
        pathField->setProperty("text", QDir::toNativeSeparators(QStringLiteral("D:/Books/Βιβλίο (bookmarked).pdf")));
        QTest::qWait(200);
        window.grabWindow().save(QDir(shots).filePath(QStringLiteral("export-dialog.png")));
    }
    const QString output = m_out->filePath(QStringLiteral("from the dialog.pdf"));
    pathField->setProperty("text", output);
    QQuickItem* save = findItem(content, QStringLiteral("saveCopyButton"));
    QVERIFY(save && save->isVisible() && save->isEnabled());

    // Enter in the path field does only what Save allows: nothing while the
    // window closes (enabledForUse false)...
    pathField->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(pathField->hasActiveFocus(), 5000);
    dialog->setProperty("enabledForUse", false);
    QVERIFY(!save->isEnabled());
    QTest::keyClick(&window, Qt::Key_Return);
    QTest::qWait(200);
    QCOMPARE(m_controller->exporter()->phase(), Phase::Idle);
    QVERIFY(!QFile::exists(output));
    // ...and saves when Save is enabled (the key does reach the field).
    dialog->setProperty("enabledForUse", true);
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->exporter()->phase(), Phase::Saved, 30000);
    QVERIFY(QFile::exists(output));
    QQuickItem* phase = findItem(content, QStringLiteral("exportPhase"));
    QVERIFY(phase && phase->property("text").toString().startsWith(QStringLiteral("Saved as")));
}

// At the window's minimum size, asking to replace (the tallest state), the
// preview scrolls and every button stays inside the dialog and the window.
void TestExportController::theDialogFitsASmallWindow()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/export/ExportDialog.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(480, 360);  // Main.qml's minimum.
    std::unique_ptr<QObject> dialog(component.createWithInitialProperties(
        {{QStringLiteral("exporter"), QVariant::fromValue<QObject*>(m_controller->exporter())},
         {QStringLiteral("parent"), QVariant::fromValue<QObject*>(window.contentItem())}}));
    QVERIFY2(dialog, qPrintable(component.errorString()));
    window.show();
    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "openFor", Q_ARG(QVariant, m_book)));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool() && !m_controller->exporter()->loading(), 10000);
    auto* content = qvariant_cast<QQuickItem*>(dialog->property("contentItem"));
    QQuickItem* pathField = findItem(content, QStringLiteral("exportPathField"));
    QVERIFY(pathField);
    const QString existing = m_out->filePath(QStringLiteral("from the dialog.pdf"));  // Saved by the test before.
    QVERIFY(QFile::exists(existing));
    pathField->setProperty("text", existing);
    QVERIFY(QMetaObject::invokeMethod(findItem(content, QStringLiteral("saveCopyButton")), "clicked"));
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->exporter()->phase(), Phase::NeedsReplace, 10000);
    QTest::qWait(100);  // Layout.

    const qreal dialogBottom = dialog->property("y").toReal() + dialog->property("height").toReal();
    QVERIFY(dialogBottom <= window.height());
    for (const char* name : {"replaceButton", "saveCopyButton", "closeExportButton"}) {
        QQuickItem* button = findItem(content, QLatin1StringView(name));
        QVERIFY2(button && button->isVisible(), name);
        const qreal bottom = button->mapToScene(QPointF(0, button->height())).y();
        QVERIFY2(bottom <= dialogBottom, qPrintable(QStringLiteral("%1 ends at %2, the dialog at %3")
                                                        .arg(QLatin1StringView(name)).arg(bottom).arg(dialogBottom)));
    }
    m_controller->exporter()->declineReplace();
}

// The dialog is closed while a copy waits (processing stopped, as when the
// window closes); opened again for that book, it follows the same copy and
// can cancel it.
void TestExportController::reopeningFollowsACopyBeingSaved()
{
    ExportController* e = m_controller->exporter();
    prepared(m_book);
    m_controller->prepareToClose();  // Nothing more runs: the copy stays queued.
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), 30000);
    const QString output = m_out->filePath(QStringLiteral("waiting.pdf"));
    e->exportTo(output);
    QTRY_COMPARE_WITH_TIMEOUT(e->phase(), Phase::Waiting, 10000);

    prepared(m_book);  // The dialog opened again.
    QTRY_COMPARE_WITH_TIMEOUT(e->phase(), Phase::Waiting, 10000);
    QVERIFY(e->running());
    QCOMPARE(QDir::cleanPath(e->resultPath()), QDir::cleanPath(output));
    QVERIFY(e->lastExportText().isEmpty());  // Followed, not a still line.
    e->cancel();
    QTRY_COMPARE_WITH_TIMEOUT(e->phase(), Phase::Cancelled, 10000);
    QVERIFY(!QFile::exists(output));
    prepared(m_book);
    QCOMPARE(e->phase(), Phase::Idle);
    QVERIFY(e->lastExportText().contains(QStringLiteral("not saved")));
}

// A book whose contents are partly mapped: the preview names every entry
// left out or placed at another level, with the reason. Without SDK
// processors the copy cannot be written, and the dialog says why.
void TestExportController::partialCoverageIsSpelledOut()
{
    QTemporaryDir dir;
    BookId book;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        book = library.value()
                   ->run([](QSqlDatabase& db) {
                       NewBook b;
                       b.asset.id = AssetId::create();
                       b.asset.sha256 = QString(64, u'c');
                       b.asset.byteSize = 1;
                       b.asset.pageCount = 20;
                       b.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(b.asset.id.toString());
                       b.originalFileName = QStringLiteral("Partial.pdf");
                       b.originalPath = b.originalFileName;
                       const BookId id = mbl::catalog::registerBook(db, b).value();
                       const PublishTicket ticket = mbl::catalog::requestTocRun(db, id).value();
                       const auto entry = [](const char* key, int order, const char* title, std::optional<int> page,
                                             HierarchyState level) {
                           TocEntry e;
                           e.sdkEntryId = QString::fromLatin1(key);
                           e.order = order;
                           e.title = QString::fromUtf8(title);
                           e.hierarchy = level;
                           if (page) {
                               e.destinationState = DestinationState::Resolved;
                               e.destinationPage = page;
                           }
                           return e;
                       };
                       TocAnalysis toc{QStringLiteral("analysis_partial"), false,
                                       {entry("a", 0, "1 Introduction", 0, HierarchyState::Root),
                                        entry("b", 1, "Appendix Ω", 12, HierarchyState::Unknown),
                                        entry("c", 2, "Index", std::nullopt, HierarchyState::Root)}};
                       RunIdentity run;
                       run.sourceSha256 = QString(64, u'c');
                       run.sdkVersion = QStringLiteral("test");
                       run.optionsJson = QStringLiteral("{}");
                       run.outcome = toc.outcome;
                       mbl::catalog::publishToc(db, ticket, run, toc).value();
                       return id;
                   })
                   .result();
    }
    LibraryController controller;  // No SDK processors: exports are not available.
    controller.open(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(controller.ready() && !controller.busy(), 10000);
    ExportController* e = controller.exporter();
    e->prepare(book.toString());
    QTRY_VERIFY_WITH_TIMEOUT(!e->loading(), 10000);
    QCOMPARE(e->bookmarkCount(), 2);
    QVERIFY(!e->complete());
    QVERIFY(e->summary().contains(QStringLiteral("left out")));
    QVERIFY(e->summary().contains(QStringLiteral("uncertain level")));
    QCOMPARE(e->notes(), (QStringList{QStringLiteral("Left out: “Index”. No page was found for it."),
                                      QStringLiteral("Top level: “Appendix Ω”. Its level in the contents is uncertain; "
                                                     "it is placed at the top level.")}));
    QVERIFY(!e->canExport());
    QVERIFY(e->problem().contains(QStringLiteral("not available")));
    controller.prepareToClose();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 10000);
}

int main(int argc, char** argv)
{
    // Offscreen, the native Windows style floods theme warnings (see tst_inspectorpane).
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    TestExportController test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_exportcontroller.moc"
