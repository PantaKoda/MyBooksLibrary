// A1 + A2 (M10): backups of a live library and their restoration, on real
// files. A backup holds a consistent catalog (with what is only in the WAL)
// and every referenced file, verified; restoring fills a new folder and keeps
// corrections, contents, chapter search, Trash and collections. Damage,
// tampering, cancelling and wrong targets leave nothing half-made.
#include "catalog/catalog.h"
#include "catalog/collections.h"
#include "catalog/exports.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "search/searchindex.h"
#include "storage/backup.h"
#include "storage/filecopy.h"
#include "storage/importservice.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::domain;
using mbl::catalog::Library;
namespace catalog = mbl::catalog;
namespace storage = mbl::storage;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

QStringList entriesOf(const QString& folder)
{
    return QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
}

} // namespace

class TestBackup : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void backupAndRestoreKeepTheLibrary();
    void theLatestChangesAreInTheBackup();
    void damageAndTamperingAreRefused();
    void cancelledAndRefusedLeaveNothing();
    void missingReportsAreReportedNotFatal();
    void neverRestoredInsideTheLibraryInUse();
    void waitingExportsAreClosedByARestore();
    void aBackupMissingANeededFileIsRefused();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    BookId import(const char* name);
    QStringList chapterHits(Library& library, const QString& text);

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_root;
    QString m_backups;
    std::unique_ptr<Library> m_library;
    BookId m_book;     // Corrected, with contents and a report, in a collection.
    BookId m_trashed;  // In Trash.
    QString m_report;  // Relative path of m_book's report.
};

void TestBackup::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_root = m_dir->filePath(QStringLiteral("Library"));
    m_backups = m_dir->filePath(QStringLiteral("Backups ü"));
    QVERIFY(QDir().mkpath(m_backups));
    auto opened = Library::open(m_root);
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
    m_book = import("contents-book.pdf");
    m_trashed = import("title-page.pdf");

    // A metadata run with its report, a correction, contents, a collection, Trash.
    m_report = QStringLiteral("reports/%1.json").arg(RunId::create().toString());
    QFile report(QDir(m_root).filePath(m_report));
    QVERIFY(report.open(QIODevice::WriteOnly) && report.write("{\"kind\":\"test\"}") > 0);
    report.close();
    auto done = db([book = m_book, trashed = m_trashed, reportPath = m_report](QSqlDatabase& d) -> Status {
        auto ticket = catalog::requestMetadataRun(d, book);
        if (!ticket)
            return ticket.error();
        ExtractedMetadata metadata;
        metadata.titleStatus = FieldStatus::Resolved;
        metadata.title = QStringLiteral("Extracted Title");
        const RunIdentity run{ticket.value().sourceSha256, QStringLiteral("test"), QString(), QStringLiteral("{}"),
                              QStringLiteral("completed"), reportPath};
        if (auto r = catalog::publishMetadata(d, ticket.value(), run, metadata); !r)
            return r.error();
        if (auto s = catalog::setOverride(d, book, MetadataField::Title,
                                          MetadataOverride::withText(QStringLiteral("Corrected Τίτλος")));
            !s)
            return s;
        auto tocTicket = catalog::requestTocRun(d, book);
        if (!tocTicket)
            return tocTicket.error();
        TocEntry e;
        e.sdkEntryId = QStringLiteral("e1");
        e.title = QStringLiteral("Networking with TCP/IP");
        e.hierarchy = HierarchyState::Root;
        e.destinationState = DestinationState::Resolved;
        e.destinationPage = 3;
        TocAnalysis toc{QStringLiteral("plan_ready"), true, {e}};
        const RunIdentity tocRun{tocTicket.value().sourceSha256, QStringLiteral("test"), QString(), QStringLiteral("{}"),
                                 toc.outcome, std::nullopt};
        if (auto r = catalog::publishToc(d, tocTicket.value(), tocRun, toc); !r)
            return r.error();
        auto collection = catalog::createCollection(d, QStringLiteral("Networks"));
        if (!collection)
            return collection.error();
        if (auto s = catalog::addToCollection(d, collection.value(), {book}); !s)
            return s;
        return catalog::trashBook(d, trashed);
    });
    QVERIFY2(done, done ? "" : qPrintable(done.error().message));
}

void TestBackup::cleanup()
{
    m_library.reset();
    m_dir.reset();
}

BookId TestBackup::import(const char* name)
{
    storage::ImportService importer(*m_library);
    const auto r = importer.importFile(fixture(name));
    if (!r.book)
        qFatal("import failed: %s", qPrintable(r.error));
    return *r.book;
}

QStringList TestBackup::chapterHits(Library& library, const QString& text)
{
    SearchRequest request;
    request.text = text;
    request.scope = SearchScope::Contents;
    auto response = library.run([request](QSqlDatabase& d) { return mbl::search::search(d, request); }).result();
    QStringList titles;
    if (response) {
        for (const BookHit& b : response.value().books) {
            for (const ChapterHit& c : b.chapters)
                titles << c.title;
        }
    }
    return titles;
}

void TestBackup::backupAndRestoreKeepTheLibrary()
{
    QList<int> progress;
    auto backup = storage::createBackup(*m_library, m_backups, nullptr, [&](int done, int) { progress << done; });
    QVERIFY2(backup, backup ? "" : qPrintable(backup.error().message));
    const storage::BackupInfo& info = backup.value();
    QVERIFY(QFileInfo(info.folder).fileName().startsWith(QStringLiteral("MyBooksLibrary backup ")));
    QCOMPARE(entriesOf(m_backups), QStringList{QFileInfo(info.folder).fileName()});  // No partial folder left.
    QCOMPARE(info.books, 2);
    QCOMPARE(info.sources, 2);  // The trashed book's file too.
    QCOMPARE(info.reports, 1);
    QVERIFY(info.missingReports.isEmpty());
    QCOMPARE(info.schemaVersion, m_library->schemaVersion());
    QVERIFY(!progress.isEmpty() && progress.last() == 4);
    QVERIFY(QFile::exists(QDir(info.folder).filePath(QStringLiteral("backup.json"))));
    QVERIFY(QFile::exists(QDir(info.folder).filePath(m_report)));
    QVERIFY(!QFile::exists(QDir(info.folder).filePath(QStringLiteral("library.sqlite-wal"))));
    QVERIFY(storage::verifyBackup(info.folder));

    // A second backup gets its own name.
    auto second = storage::createBackup(*m_library, m_backups);
    QVERIFY(second);
    QVERIFY(second.value().folder != info.folder);

    // Restored as a new library, while the original stays open and unchanged.
    const QString restored = m_dir->filePath(QStringLiteral("Restored library"));
    auto restoredInfo = storage::restoreBackup(info.folder, restored, {m_root});
    QVERIFY2(restoredInfo, restoredInfo ? "" : qPrintable(restoredInfo.error().message));
    QCOMPARE(QDir::cleanPath(restoredInfo.value().libraryFolder), QDir::cleanPath(restored));
    QCOMPARE(restoredInfo.value().schemaVersion, m_library->schemaVersion());
    QCOMPARE(entriesOf(m_dir->path()).filter(QStringLiteral(".restoring")), QStringList{});

    auto reopened = Library::open(restored);
    QVERIFY2(reopened, reopened ? "" : qPrintable(reopened.error().message));
    Library& copy = *reopened.value();
    auto details = copy.run([book = m_book](QSqlDatabase& d) { return catalog::bookDetails(d, book); }).result();
    QVERIFY(details);
    QCOMPARE(details.value().summary.displayTitle, QStringLiteral("Corrected Τίτλος"));  // The correction.
    QVERIFY(details.value().toc.has_value());
    QCOMPARE(chapterHits(copy, QStringLiteral("TCP/IP")), QStringList{QStringLiteral("Networking with TCP/IP")});
    auto trashed = copy.run([](QSqlDatabase& d) { return catalog::listBooks(d, Lifecycle::Trashed); }).result();
    QVERIFY(trashed && trashed.value().size() == 1 && trashed.value().first().id == m_trashed);
    auto collections = copy.run([](QSqlDatabase& d) { return catalog::listCollections(d); }).result();
    QVERIFY(collections && collections.value().size() == 1);
    QCOMPARE(collections.value().first().name, QStringLiteral("Networks"));
    // Every managed file is there with its recorded digest.
    const auto sources = details.value().asset;
    QCOMPARE(storage::checkDigest(QDir(restored).filePath(sources.managedPath), sources.sha256),
             storage::DigestCheck::Match);
    QVERIFY(QFile::exists(QDir(restored).filePath(m_report)));
}

// The catalog runs in WAL mode: a change only in the WAL is in the backup,
// which a copy of library.sqlite alone could miss.
void TestBackup::theLatestChangesAreInTheBackup()
{
    QVERIFY(db([book = m_book](QSqlDatabase& d) {
        return catalog::setOverride(d, book, MetadataField::Title, MetadataOverride::withText(QStringLiteral("Latest")));
    }));
    QVERIFY(QFileInfo(QDir(m_root).filePath(QStringLiteral("library.sqlite-wal"))).size() > 0);
    auto backup = storage::createBackup(*m_library, m_backups);
    QVERIFY(backup);
    const QString restored = m_dir->filePath(QStringLiteral("Restored"));
    QVERIFY(storage::restoreBackup(backup.value().folder, restored, {m_root}));
    auto reopened = Library::open(restored);
    QVERIFY(reopened);
    auto details = reopened.value()->run([book = m_book](QSqlDatabase& d) { return catalog::bookDetails(d, book); }).result();
    QVERIFY(details);
    QCOMPARE(details.value().summary.displayTitle, QStringLiteral("Latest"));
}

void TestBackup::damageAndTamperingAreRefused()
{
    // A managed source whose bytes changed: the backup stops, nothing is left.
    auto summary = db([book = m_book](QSqlDatabase& d) { return catalog::bookDetails(d, book); });
    QVERIFY(summary);
    const QString source = QDir(m_root).filePath(summary.value().asset.managedPath);
    const QByteArray original = [&] {
        QFile f(source);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }();
    {
        QFile f(source);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(10);
        f.write("X");
    }
    auto damaged = storage::createBackup(*m_library, m_backups);
    QVERIFY(!damaged);
    QVERIFY(damaged.error().message.contains(QStringLiteral("does not match")));
    QVERIFY(entriesOf(m_backups).isEmpty());
    {
        QFile f(source);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(original);
    }

    // A backup changed after it was saved: verify and restore refuse it, and
    // restore writes nothing.
    auto backup = storage::createBackup(*m_library, m_backups);
    QVERIFY(backup);
    {
        QFile f(QDir(backup.value().folder).filePath(m_report));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"kind\":\"edited\"}");
    }
    QVERIFY(!storage::verifyBackup(backup.value().folder));
    const QString target = m_dir->filePath(QStringLiteral("Not restored"));
    auto refused = storage::restoreBackup(backup.value().folder, target, {m_root});
    QVERIFY(!refused);
    QVERIFY2(refused.error().message.contains(m_report), qPrintable(refused.error().message));  // Names the changed file.

    // Same size, other bytes: the digest catches it.
    {
        QFile f(QDir(backup.value().folder).filePath(m_report));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"kind\":\"tast\"}");
    }
    auto sameSize = storage::restoreBackup(backup.value().folder, target, {m_root});
    QVERIFY(!sameSize);
    QVERIFY2(sameSize.error().message.contains(QStringLiteral("changed since it was saved")),
             qPrintable(sameSize.error().message));
    QVERIFY(!QFileInfo::exists(target));

    // A manifest naming a file outside the backup is refused before any copy.
    auto clean = storage::createBackup(*m_library, m_backups);
    QVERIFY(clean);
    const QString manifestPath = QDir(clean.value().folder).filePath(QStringLiteral("backup.json"));
    QFile manifest(manifestPath);
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QByteArray json = manifest.readAll();
    manifest.close();
    json.replace("\"files/", "\"../../escape/");
    QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(json);
    manifest.close();
    auto escaping = storage::restoreBackup(clean.value().folder, target, {m_root});
    QVERIFY(!escaping);
    QVERIFY(escaping.error().message.contains(QStringLiteral("invalid file")));
    QVERIFY(!QFileInfo::exists(target));
    QVERIFY(!QFileInfo::exists(m_dir->filePath(QStringLiteral("escape"))));

    QVERIFY(!storage::verifyBackup(m_dir->filePath(QStringLiteral("Library"))));  // Not a backup.
}

void TestBackup::cancelledAndRefusedLeaveNothing()
{
    std::atomic_bool cancel{false};
    auto cancelledBackup = storage::createBackup(*m_library, m_backups, &cancel, [&](int done, int) {
        if (done >= 2)
            cancel = true;
    });
    QVERIFY(!cancelledBackup);
    QCOMPARE(cancelledBackup.error().message, QStringLiteral("cancelled"));
    QVERIFY(entriesOf(m_backups).isEmpty());

    // Where a backup may go.
    QVERIFY(!storage::createBackup(*m_library, m_root));  // Inside the library.
    QVERIFY(!storage::createBackup(*m_library, QDir(m_root).filePath(QStringLiteral("files"))));
    QVERIFY(!storage::createBackup(*m_library, m_dir->filePath(QStringLiteral("missing"))));
    QVERIFY(!storage::createBackup(*m_library, QStringLiteral("relative")));

    auto backup = storage::createBackup(*m_library, m_backups);
    QVERIFY(backup);
    // Where a library may be restored: never into a non-empty folder (the
    // library in use included), nor inside the backup.
    auto intoLibrary = storage::restoreBackup(backup.value().folder, m_root, {m_root});
    QVERIFY(!intoLibrary);
    QVERIFY(intoLibrary.error().message.contains(QStringLiteral("not empty")));
    QVERIFY(!storage::restoreBackup(backup.value().folder, QDir(backup.value().folder).filePath(QStringLiteral("x")), {m_root}));
    QVERIFY(!storage::restoreBackup(backup.value().folder, QStringLiteral("relative"), {m_root}));
    // An empty folder is fine.
    const QString empty = m_dir->filePath(QStringLiteral("Empty"));
    QVERIFY(QDir().mkpath(empty));
    std::atomic_bool stop{false};
    auto cancelledRestore = storage::restoreBackup(backup.value().folder, empty, {m_root}, &stop, [&](int done, int) {
        if (done >= 1)
            stop = true;
    });
    QVERIFY(!cancelledRestore);
    QVERIFY(QFileInfo(empty).isDir() && entriesOf(empty).isEmpty());  // As it was.
    QCOMPARE(entriesOf(m_dir->path()).filter(QStringLiteral(".restoring")), QStringList{});
    QVERIFY(storage::restoreBackup(backup.value().folder, empty, {m_root}));
}

void TestBackup::missingReportsAreReportedNotFatal()
{
    QVERIFY(QFile::remove(QDir(m_root).filePath(m_report)));
    auto backup = storage::createBackup(*m_library, m_backups);
    QVERIFY2(backup, backup ? "" : qPrintable(backup.error().message));
    QCOMPARE(backup.value().missingReports, QStringList{m_report});
    QCOMPARE(backup.value().reports, 0);
    auto verified = storage::verifyBackup(backup.value().folder);
    QVERIFY(verified);
    QCOMPARE(verified.value().missingReports, QStringList{m_report});
}

// Restoring inside the open library would mix a second library into its
// folders, and its start-up recovery removes unknown staging folders: refused
// before anything is written.
void TestBackup::neverRestoredInsideTheLibraryInUse()
{
    auto backup = storage::createBackup(*m_library, m_backups);
    QVERIFY(backup);
    for (const QString& inside : {QStringLiteral("restored"), QStringLiteral("files/restored"),
                                  QStringLiteral("staging/restored"), QStringLiteral("reports/restored")}) {
        const QString target = QDir(m_root).filePath(inside);
        auto refused = storage::restoreBackup(backup.value().folder, target, {m_root});
        QVERIFY2(!refused, qPrintable(inside));
        QVERIFY(refused.error().message.contains(QStringLiteral("library in use")));
        QVERIFY(!QFileInfo::exists(target));
    }
    // Another spelling of the same folder is the same folder.
    const QString spelled = QDir(m_root).filePath(QStringLiteral("files/../restored"));
    QVERIFY(!storage::restoreBackup(backup.value().folder, spelled, {m_root}));
    QVERIFY(!QFileInfo::exists(QDir(m_root).filePath(QStringLiteral("restored"))));
}

// An export waiting when the backup was made is not written by the restored
// library on its own: it is closed as not written, and the user asks again.
void TestBackup::waitingExportsAreClosedByARestore()
{
    const QString destination = m_dir->filePath(QStringLiteral("Exports/Book (bookmarked).pdf"));
    QVERIFY(QDir().mkpath(QFileInfo(destination).absolutePath()));
    auto queued = db([book = m_book, destination](QSqlDatabase& d) -> Result<ExportRecord> {
        QSqlQuery q(d);
        if (!q.exec(QStringLiteral("UPDATE assets SET page_count = 27")))  // Known after an analysis.
            return makeError(ErrorCode::Database, QStringLiteral("setup"));
        return catalog::enqueueExport(d, book, destination, false);
    });
    QVERIFY2(queued, queued ? "" : qPrintable(queued.error().message));
    auto backup = storage::createBackup(*m_library, m_backups);
    QVERIFY(backup);

    const QString restored = m_dir->filePath(QStringLiteral("Restored"));
    auto info = storage::restoreBackup(backup.value().folder, restored, {m_root});
    QVERIFY2(info, info ? "" : qPrintable(info.error().message));
    QCOMPARE(info.value().exportsClosed, 1);
    auto reopened = Library::open(restored);
    QVERIFY(reopened);
    Library& copy = *reopened.value();
    QVERIFY(copy.run([](QSqlDatabase& d) { return catalog::recoverJobs(d); }).result());  // As at start-up.
    auto open = copy.run([](QSqlDatabase& d) { return catalog::listJobs(d, true, -1); }).result();
    QVERIFY(open);
    for (const JobRecord& j : open.value())
        QVERIFY(j.kind != JobKind::Export);
    auto job = copy.run([id = queued.value().job](QSqlDatabase& d) { return catalog::job(d, id); }).result();
    QVERIFY(job);
    QCOMPARE(job.value().state, JobState::Cancelled);
    QCOMPARE(job.value().outcome, QStringLiteral("restored"));
    auto record = copy.run([id = queued.value().job](QSqlDatabase& d) { return catalog::exportRecord(d, id); }).result();
    QVERIFY(record);
    QCOMPARE(record.value().committed, std::optional<bool>(false));
    // The library in use keeps its own request.
    auto original = db([id = queued.value().job](QSqlDatabase& d) { return catalog::job(d, id); });
    QCOMPARE(original.value().state, JobState::Queued);
    QVERIFY(!QFileInfo::exists(destination));
}

// "Verified" means everything needed to restore is there: a source the
// catalog needs, left out of the backup and of its manifest, is caught.
void TestBackup::aBackupMissingANeededFileIsRefused()
{
    auto backup = storage::createBackup(*m_library, m_backups);
    QVERIFY(backup);
    auto details = db([book = m_book](QSqlDatabase& d) { return catalog::bookDetails(d, book); });
    QVERIFY(details);
    const QString managed = details.value().asset.managedPath;
    QVERIFY(QFile::remove(QDir(backup.value().folder).filePath(managed)));
    const QString manifestPath = QDir(backup.value().folder).filePath(QStringLiteral("backup.json"));
    QFile manifest(manifestPath);
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    QJsonArray files;
    for (const QJsonValue& v : root.value(QStringLiteral("files")).toArray()) {
        if (v.toObject().value(QStringLiteral("path")).toString() != managed)
            files.append(v);
    }
    root.insert(QStringLiteral("files"), files);
    QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(QJsonDocument(root).toJson());
    manifest.close();

    auto verified = storage::verifyBackup(backup.value().folder);
    QVERIFY(!verified);
    QVERIFY2(verified.error().message.contains(managed), qPrintable(verified.error().message));
    const QString target = m_dir->filePath(QStringLiteral("Incomplete"));
    QVERIFY(!storage::restoreBackup(backup.value().folder, target, {m_root}));
    QVERIFY(!QFileInfo::exists(target));
}

QTEST_GUILESS_MAIN(TestBackup)
#include "tst_backup.moc"
