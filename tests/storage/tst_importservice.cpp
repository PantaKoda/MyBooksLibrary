// A1/A2 import: verified managed copies, unchanged originals, duplicates,
// source changes, cancellation, and crash recovery at every phase.
#include "catalog/catalog.h"
#include "catalog/imports.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "storage/filecopy.h"
#include "storage/importservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

using namespace mbl::domain;
using namespace mbl::storage;
using mbl::catalog::Library;
namespace catalog = mbl::catalog;
using Outcome = ImportResult::Outcome;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

QString shaOf(const QString& path)
{
    return sha256OfFile(path).value_or(QString());
}

// Everything the import must leave unchanged about an external original.
struct Original {
    QString sha;
    FileStamp stamp;
};

Original snapshot(const QString& path)
{
    return {shaOf(path), stampOf(path)};
}

// Holds a file open so that other opens for reading fail with a sharing
// violation while deletion stays allowed (FILE_SHARE_DELETE only): a
// transient "unreadable, not damaged" state.
class ReadDenyingLock {
public:
    explicit ReadDenyingLock(const QString& path)
    {
#ifdef Q_OS_WIN
        m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ,
                               FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
        Q_UNUSED(path);
#endif
    }
    ~ReadDenyingLock() { release(); }
    bool held() const
    {
#ifdef Q_OS_WIN
        return m_handle != INVALID_HANDLE_VALUE;
#else
        return false;
#endif
    }
    void release()
    {
#ifdef Q_OS_WIN
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
#endif
    }

private:
#ifdef Q_OS_WIN
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#endif
};

QStringList filesUnder(const QString& dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(dir).relativeFilePath(it.next());
    out.sort();
    return out;
}

} // namespace

class TestImportService : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void importsVerifiedCopyAndLeavesOriginalUnchanged();
    void largeFileStreamsWithProgress();
    void duplicateBytesUnderAnotherNameReuseTheBook();
    void duplicateOfTrashedBookIsReportedNotRestored();
    void sourceChangedDuringCopyFails();
    void missingOrNonPdfSourceFails();
    void cancelledImportLeavesNothing();
    void crashRecovery_data();
    void crashRecovery();
    void recoveryDropsDuplicateInstalledCopyButKeepsReferencedFile();
    void recoveryReportsButKeepsOrphans();
    void recoveryKeepsVerifiedStageWhenInstallFails();
    void recoveryReplacesWrongDigestDestinationWithGoodStage();
    void commitMoveNeverOverwrites();
    void checkDigestDistinguishesUnreadable();
    void recoveryDefersUnreadableCopy_data();
    void recoveryDefersUnreadableCopy();
    void importQueuesOneMetadataJob();

private:
    QString external(const QString& name, const QString& from = fixture("title-page.pdf"));
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    void restart();
    int bookCount();
    void expectStoredDuplicate(const ImportResult& r, const BookId& existing);
    ImportOperation interruptedVerified(const QString& fileName);

    std::unique_ptr<QTemporaryDir> m_root;      // Library folder.
    std::unique_ptr<QTemporaryDir> m_outside;   // "User's" folder with the originals.
    std::unique_ptr<Library> m_library;
    std::unique_ptr<ImportService> m_service;
};

void TestImportService::init()
{
    m_root = std::make_unique<QTemporaryDir>();
    m_outside = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_root->path());
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
    m_service = std::make_unique<ImportService>(*m_library);
}

void TestImportService::cleanup()
{
    m_service.reset();
    m_library.reset();
    m_outside.reset();
    m_root.reset();
}

QString TestImportService::external(const QString& name, const QString& from)
{
    const QString path = QDir(m_outside->path()).filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile::remove(path);
    if (!QFile::copy(from, path))
        qFatal("cannot copy fixture");
    QFile(path).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return path;
}

void TestImportService::restart()
{
    m_service.reset();
    m_library.reset();
    auto opened = Library::open(m_root->path());
    QVERIFY(opened);
    m_library = std::move(opened.value());
    m_service = std::make_unique<ImportService>(*m_library);
}

// The duplicate outcome must be persisted, not just returned, and survive a restart.
void TestImportService::expectStoredDuplicate(const ImportResult& r, const BookId& existing)
{
    QVERIFY(r.operation);
    auto op = db([id = *r.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QVERIFY(op);
    QCOMPARE(op.value().phase, ImportPhase::Duplicate);
    QCOMPARE(*op.value().book, existing);
    QCOMPARE(*op.value().sha256, r.sha256);
    QCOMPARE(*op.value().byteSize, r.bytes);
    auto open = db([](QSqlDatabase& d) { return catalog::openImports(d); });
    QVERIFY(open.value().isEmpty());

    restart();
    const RecoveryReport report = m_service->recover();
    QCOMPARE(report.abandoned + report.registered + report.duplicates + report.failed + report.deferred, 0);
    op = db([id = *r.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QCOMPARE(op.value().phase, ImportPhase::Duplicate);
    QCOMPARE(*op.value().book, existing);
}

// Crashes an import right after it was recorded as Verified (file still in
// staging), restarts, and returns the open operation.
ImportOperation TestImportService::interruptedVerified(const QString& fileName)
{
    m_service->setCrashHook([](ImportStage s) { return s == ImportStage::VerifiedRecorded; });
    const ImportResult r = m_service->importFile(external(fileName));
    if (r.outcome != Outcome::Interrupted)
        qFatal("expected an interrupted import");
    restart();
    return db([id = *r.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); }).value();
}

int TestImportService::bookCount()
{
    return db([](QSqlDatabase& d) {
        QSqlQuery q(d);
        q.exec(QStringLiteral("SELECT count(*) FROM books"));
        q.next();
        return q.value(0).toInt();
    });
}

void TestImportService::importsVerifiedCopyAndLeavesOriginalUnchanged()
{
    const QString source = external(QStringLiteral("Βιβλία ü/Poorly named scan (1).pdf"));
    const Original before = snapshot(source);

    const ImportResult r = m_service->importFile(source);
    QCOMPARE(outcomeName(r.outcome), QStringLiteral("imported"));
    QVERIFY(r.book);
    QCOMPARE(r.sha256, before.sha);

    const Original after = snapshot(source);
    QCOMPARE(after.sha, before.sha);             // Bytes unchanged...
    QVERIFY(after.stamp == before.stamp);         // ...and size and modification time.

    auto details = db([id = *r.book](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QVERIFY(details);
    const BookDetails& b = details.value();
    QCOMPARE(b.asset.sha256, before.sha);
    QCOMPARE(b.asset.managedPath, LibraryLayout::managedSourcePath(b.asset.id));
    QVERIFY(!b.asset.pageCount);                  // Unknown at import is valid.
    QCOMPARE(b.originalFileName, QStringLiteral("Poorly named scan (1).pdf"));
    QCOMPARE(b.originalPath, QFileInfo(source).absoluteFilePath());
    QCOMPARE(b.summary.displayTitle, QStringLiteral("Poorly named scan (1)"));
    const QString managed = m_service->layout().absolute(b.asset.managedPath);
    QCOMPARE(shaOf(managed), before.sha);
    QVERIFY(filesUnder(m_service->layout().absolute(QStringLiteral("staging"))).isEmpty());

    auto op = db([id = *r.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QCOMPARE(op.value().phase, ImportPhase::Registered);
    QCOMPARE(*op.value().book, *r.book);

    // Reading works after the external original disappears.
    QVERIFY(QFile::remove(source));
    QCOMPARE(shaOf(managed), before.sha);
}

void TestImportService::largeFileStreamsWithProgress()
{
    // 3.5 MiB: a PDF header followed by deterministic bytes (several 1 MiB chunks).
    const QString source = QDir(m_outside->path()).filePath(QStringLiteral("large.pdf"));
    {
        QFile f(source);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("%PDF-1.4\n");
        QByteArray block(4096, '\0');
        for (int i = 0; i < 896; ++i) {
            for (int j = 0; j < block.size(); ++j)
                block[j] = char((i * 31 + j * 7) & 0xff);
            f.write(block);
        }
    }
    qint64 last = 0, total = 0;
    int calls = 0;
    const ImportResult r = m_service->importFile(source, nullptr, [&](qint64 done, qint64 all) {
        QVERIFY(done > last);
        last = done;
        total = all;
        ++calls;
    });
    QCOMPARE(r.outcome, Outcome::Imported);
    QCOMPARE(r.bytes, QFileInfo(source).size());
    QCOMPARE(last, total);
    QVERIFY(calls >= 4);
    QCOMPARE(r.sha256, shaOf(source));
}

void TestImportService::duplicateBytesUnderAnotherNameReuseTheBook()
{
    const ImportResult first = m_service->importFile(external(QStringLiteral("a.pdf")));
    QCOMPARE(first.outcome, Outcome::Imported);
    // A correction on the existing book must survive the duplicate import.
    QVERIFY(db([id = *first.book](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::Title, MetadataOverride::withText(QStringLiteral("Mine")));
    }));

    const ImportResult second = m_service->importFile(external(QStringLiteral("renamed copy.pdf")));
    QCOMPARE(second.outcome, Outcome::Duplicate);
    QCOMPARE(*second.book, *first.book);
    expectStoredDuplicate(second, *first.book);
    QCOMPARE(bookCount(), 1);
    QCOMPARE(filesUnder(m_service->layout().absolute(QStringLiteral("files"))).size(), 1);
    QVERIFY(filesUnder(m_service->layout().absolute(QStringLiteral("staging"))).isEmpty());
    auto details = db([id = *first.book](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QCOMPARE(*details.value().summary.metadata.title, QStringLiteral("Mine"));

    // A different PDF is not a duplicate, even with the same file name.
    const ImportResult other = m_service->importFile(external(QStringLiteral("a.pdf"), fixture("contents-book.pdf")));
    QCOMPARE(other.outcome, Outcome::Imported);
    QCOMPARE(bookCount(), 2);
}

void TestImportService::duplicateOfTrashedBookIsReportedNotRestored()
{
    const ImportResult first = m_service->importFile(external(QStringLiteral("t.pdf")));
    QVERIFY(db([id = *first.book](QSqlDatabase& d) { return catalog::trashBook(d, id); }));
    const ImportResult again = m_service->importFile(external(QStringLiteral("t again.pdf")));
    QCOMPARE(again.outcome, Outcome::DuplicateInTrash);
    QCOMPARE(*again.book, *first.book);
    expectStoredDuplicate(again, *first.book);
    auto details = db([id = *first.book](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QCOMPARE(details.value().summary.lifecycle, Lifecycle::Trashed);  // Restoring is the user's choice.
}

void TestImportService::sourceChangedDuringCopyFails()
{
    const QString source = external(QStringLiteral("changing.pdf"));
    m_service->setAfterFirstChunkHook([source] {
        QFile f(source);
        if (f.open(QIODevice::Append))
            f.write("appended while importing\n");
    });
    const ImportResult r = m_service->importFile(source);
    QCOMPARE(r.outcome, Outcome::Failed);
    QVERIFY2(r.error.contains(QStringLiteral("changed")), qPrintable(r.error));
    QCOMPARE(bookCount(), 0);
    QVERIFY(filesUnder(m_service->layout().absolute(QStringLiteral("staging"))).isEmpty());
    QVERIFY(filesUnder(m_service->layout().absolute(QStringLiteral("files"))).isEmpty());
    auto op = db([id = *r.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QCOMPARE(op.value().phase, ImportPhase::Failed);
}

void TestImportService::missingOrNonPdfSourceFails()
{
    ImportResult r = m_service->importFile(QDir(m_outside->path()).filePath(QStringLiteral("nope.pdf")));
    QCOMPARE(r.outcome, Outcome::Failed);
    QVERIFY(!r.operation);  // Nothing recorded.

    const QString text = QDir(m_outside->path()).filePath(QStringLiteral("notes.pdf"));
    QFile f(text);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("just some text");
    f.close();
    r = m_service->importFile(text);
    QCOMPARE(r.outcome, Outcome::Failed);
    QVERIFY(r.error.contains(QStringLiteral("not a PDF")));
    QCOMPARE(bookCount(), 0);
}

void TestImportService::cancelledImportLeavesNothing()
{
    std::atomic_bool cancel{true};
    const QString source = external(QStringLiteral("c.pdf"));
    const Original before = snapshot(source);
    const ImportResult r = m_service->importFile(source, &cancel);
    QCOMPARE(r.outcome, Outcome::Cancelled);
    QCOMPARE(bookCount(), 0);
    QVERIFY(filesUnder(m_service->layout().absolute(QStringLiteral("staging"))).isEmpty());
    auto op = db([id = *r.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QCOMPARE(op.value().phase, ImportPhase::Cancelled);
    QCOMPARE(snapshot(source).sha, before.sha);
}

void TestImportService::crashRecovery_data()
{
    QTest::addColumn<int>("stage");
    QTest::addColumn<int>("expectedBooks");
    QTest::addColumn<int>("expectedPhase");

    QTest::newRow("after begin") << int(ImportStage::Begun) << 0 << int(ImportPhase::Abandoned);
    QTest::newRow("after staging, before verified recorded") << int(ImportStage::Staged) << 0
                                                             << int(ImportPhase::Abandoned);
    QTest::newRow("verified, still in staging") << int(ImportStage::VerifiedRecorded) << 1
                                                << int(ImportPhase::Registered);
    QTest::newRow("installed, not registered") << int(ImportStage::Installed) << 1 << int(ImportPhase::Registered);
}

void TestImportService::crashRecovery()
{
    QFETCH(int, stage);
    QFETCH(int, expectedBooks);
    QFETCH(int, expectedPhase);

    const QString source = external(QStringLiteral("crash.pdf"));
    const Original before = snapshot(source);
    m_service->setCrashHook([stage](ImportStage s) { return int(s) == stage; });
    const ImportResult r = m_service->importFile(source);
    QCOMPARE(r.outcome, Outcome::Interrupted);
    QCOMPARE(bookCount(), 0);

    restart();  // The process "died"; a new one starts and recovers.
    const RecoveryReport report = m_service->recover();
    QCOMPARE(report.registered + report.abandoned, 1);
    QCOMPARE(bookCount(), expectedBooks);
    auto op = db([id = *r.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QCOMPARE(int(op.value().phase), expectedPhase);
    QVERIFY(filesUnder(m_service->layout().absolute(QStringLiteral("staging"))).isEmpty());
    QCOMPARE(filesUnder(m_service->layout().absolute(QStringLiteral("files"))).size(), expectedBooks);
    if (expectedBooks == 1) {
        auto details = db([id = *op.value().book](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
        QCOMPARE(shaOf(m_service->layout().absolute(details.value().asset.managedPath)), before.sha);
        QCOMPARE(details.value().originalFileName, QStringLiteral("crash.pdf"));
    }
    QCOMPARE(snapshot(source).sha, before.sha);
    QVERIFY(snapshot(source).stamp == before.stamp);

    // Recovery is idempotent.
    const RecoveryReport again = m_service->recover();
    QCOMPARE(again.registered + again.abandoned + again.failed + again.duplicates, 0);
    QCOMPARE(bookCount(), expectedBooks);

    // Importing the same file afterwards behaves normally.
    m_service->setCrashHook({});
    const ImportResult retry = m_service->importFile(source);
    QCOMPARE(retry.outcome, expectedBooks == 1 ? Outcome::Duplicate : Outcome::Imported);
    QCOMPARE(bookCount(), 1);
}

void TestImportService::recoveryDropsDuplicateInstalledCopyButKeepsReferencedFile()
{
    // First import crashes after installing; a second import of the same bytes completes.
    const QString source = external(QStringLiteral("race.pdf"));
    m_service->setCrashHook([](ImportStage s) { return s == ImportStage::Installed; });
    const ImportResult crashed = m_service->importFile(source);
    QCOMPARE(crashed.outcome, Outcome::Interrupted);
    m_service->setCrashHook({});
    const ImportResult done = m_service->importFile(external(QStringLiteral("race copy.pdf")));
    QCOMPARE(done.outcome, Outcome::Imported);
    QCOMPARE(filesUnder(m_service->layout().absolute(QStringLiteral("files"))).size(), 2);

    restart();
    const RecoveryReport report = m_service->recover();
    QCOMPARE(report.duplicates, 1);
    QCOMPARE(report.removedUnreferencedFiles, 1);
    QCOMPARE(bookCount(), 1);
    const QStringList files = filesUnder(m_service->layout().absolute(QStringLiteral("files")));
    QCOMPARE(files.size(), 1);
    auto details = db([id = *done.book](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QCOMPARE(QStringLiteral("files/") + files.first(), details.value().asset.managedPath);  // The referenced copy stays.
    auto op = db([id = *crashed.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QCOMPARE(op.value().phase, ImportPhase::Duplicate);
    QCOMPARE(*op.value().book, *done.book);
}

void TestImportService::recoveryReportsButKeepsOrphans()
{
    const QString orphan = m_service->layout().absolute(QStringLiteral("files/unknown-asset/source.pdf"));
    QDir().mkpath(QFileInfo(orphan).absolutePath());
    QVERIFY(QFile::copy(fixture("title-page.pdf"), orphan));
    const QString stray = m_service->layout().absolute(QStringLiteral("staging/leftover"));
    QDir().mkpath(stray);
    QFile partial(stray + QStringLiteral("/partial.pdf"));
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.close();

    const RecoveryReport report = m_service->recover();
    QCOMPARE(report.orphanedManagedFiles, QStringList{QStringLiteral("files/unknown-asset/source.pdf")});
    QVERIFY(QFile::exists(orphan));  // Never deleted automatically.
    QCOMPARE(report.removedStagingDirectories, 1);
    QVERIFY(!QDir(stray).exists());
}

// Recovery must not trade a verified stage for a terminal failure when only
// installation failed: once through a real I/O obstacle, once through the hook.
void TestImportService::recoveryKeepsVerifiedStageWhenInstallFails()
{
    const QString source = external(QStringLiteral("keep.pdf"));
    const QString sha = shaOf(source);
    const ImportOperation op = interruptedVerified(QStringLiteral("keep.pdf"));
    const QString staged = m_service->layout().absolute(LibraryLayout::stagedSourcePath(op.id));
    QCOMPARE(shaOf(staged), sha);

    // (a) A file where the asset folder should be: the folder cannot be created.
    const QString blocker = m_service->layout().absolute(QStringLiteral("files/") + op.assetId->toString());
    {
        QFile f(blocker);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    RecoveryReport report = m_service->recover();
    QCOMPARE(report.deferred, 1);
    QCOMPARE(report.failed, 0);
    QCOMPARE(bookCount(), 0);
    QCOMPARE(shaOf(staged), sha);  // The good bytes survive.
    auto stored = db([id = op.id](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QCOMPARE(stored.value().phase, ImportPhase::Verified);
    report = m_service->recover();  // Still blocked: still kept.
    QCOMPARE(report.deferred, 1);
    QCOMPARE(shaOf(staged), sha);

    // (b) The move itself fails.
    QVERIFY(QFile::remove(blocker));
    m_service->setInstallHook([] { return false; });
    report = m_service->recover();
    QCOMPARE(report.deferred, 1);
    QCOMPARE(shaOf(staged), sha);

    // The obstacle is gone: the next recovery completes the import.
    m_service->setInstallHook({});
    report = m_service->recover();
    QCOMPARE(report.registered, 1);
    QCOMPARE(report.deferred, 0);
    QCOMPARE(bookCount(), 1);
    QVERIFY(!QFile::exists(staged));
    stored = db([id = op.id](QSqlDatabase& d) { return catalog::importOperation(d, id); });
    QCOMPARE(stored.value().phase, ImportPhase::Registered);
    QCOMPARE(shaOf(m_service->layout().absolute(LibraryLayout::managedSourcePath(*op.assetId))), sha);
}

void TestImportService::recoveryReplacesWrongDigestDestinationWithGoodStage()
{
    const QString source = external(QStringLiteral("damaged dest.pdf"));
    const QString sha = shaOf(source);
    const ImportOperation op = interruptedVerified(QStringLiteral("damaged dest.pdf"));
    const QString managed = m_service->layout().absolute(LibraryLayout::managedSourcePath(*op.assetId));
    QDir().mkpath(QFileInfo(managed).absolutePath());
    {
        QFile f(managed);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("%PDF-1.4 truncated garbage");
    }
    const RecoveryReport report = m_service->recover();
    QCOMPARE(report.registered, 1);
    QCOMPARE(report.removedUnreferencedFiles, 1);
    QCOMPARE(shaOf(managed), sha);  // The verified bytes, not the damaged ones.
    QVERIFY(filesUnder(m_service->layout().absolute(QStringLiteral("staging"))).isEmpty());
}

void TestImportService::commitMoveNeverOverwrites()
{
    const QString a = QDir(m_outside->path()).filePath(QStringLiteral("a.bin"));
    const QString b = QDir(m_outside->path()).filePath(QStringLiteral("b.bin"));
    for (const QString& path : {a, b}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(path.toUtf8());
    }
    QString error;
    QVERIFY(!commitMove(a, b, &error));  // Existing target: refused.
    QVERIFY(!error.isEmpty());
    QVERIFY(QFile::exists(a));
    QVERIFY(QFile::remove(b));
    QVERIFY2(commitMove(a, b, &error), qPrintable(error));
    QVERIFY(!QFile::exists(a));
    QVERIFY(QFile::exists(b));
}

void TestImportService::checkDigestDistinguishesUnreadable()
{
    const QString path = external(QStringLiteral("digest.pdf"));
    const QString sha = shaOf(path);
    QCOMPARE(checkDigest(path, sha), DigestCheck::Match);
    QCOMPARE(checkDigest(path, QString(64, u'0')), DigestCheck::Mismatch);
    QCOMPARE(checkDigest(path + QStringLiteral(".missing"), sha), DigestCheck::Missing);
    QCOMPARE(checkDigest(m_outside->path(), sha), DigestCheck::Unreadable);  // A folder, not a file.
#ifdef Q_OS_WIN
    ReadDenyingLock lock(path);
    QVERIFY(lock.held());
    QCOMPARE(checkDigest(path, sha), DigestCheck::Unreadable);
    lock.release();
    QCOMPARE(checkDigest(path, sha), DigestCheck::Match);
#endif
}

void TestImportService::recoveryDefersUnreadableCopy_data()
{
    QTest::addColumn<int>("stage");
    QTest::newRow("staged copy only") << int(ImportStage::VerifiedRecorded);
    QTest::newRow("installed copy only") << int(ImportStage::Installed);
}

// A copy that cannot be read is not a damaged copy: recovery must keep the
// file and the Verified phase, and complete once the file is readable again.
void TestImportService::recoveryDefersUnreadableCopy()
{
#ifndef Q_OS_WIN
    QSKIP("Needs Windows share modes to make a file unreadable but deletable.");
#else
    QFETCH(int, stage);
    const QString source = external(QStringLiteral("locked.pdf"));
    const QString sha = shaOf(source);
    m_service->setCrashHook([stage](ImportStage s) { return int(s) == stage; });
    const ImportResult r = m_service->importFile(source);
    QCOMPARE(r.outcome, Outcome::Interrupted);
    restart();
    const ImportOperation op =
        db([id = *r.operation](QSqlDatabase& d) { return catalog::importOperation(d, id); }).value();
    const QString only = m_service->layout().absolute(
        stage == int(ImportStage::Installed) ? LibraryLayout::managedSourcePath(*op.assetId)
                                             : LibraryLayout::stagedSourcePath(op.id));
    QCOMPARE(shaOf(only), sha);

    {
        ReadDenyingLock lock(only);
        QVERIFY(lock.held());
        for (int attempt = 0; attempt < 2; ++attempt) {
            const RecoveryReport report = m_service->recover();
            QCOMPARE(report.deferred, 1);
            QCOMPARE(report.failed + report.registered + report.removedUnreferencedFiles, 0);
            auto stored = db([id = op.id](QSqlDatabase& d) { return catalog::importOperation(d, id); });
            QCOMPARE(stored.value().phase, ImportPhase::Verified);  // Still recoverable.
        }
    }  // Lock released: had the file been deleted while shared, it would now be gone.

    QVERIFY(QFile::exists(only));
    QCOMPARE(shaOf(only), sha);
    const RecoveryReport report = m_service->recover();
    QCOMPARE(report.registered, 1);
    QCOMPARE(report.deferred, 0);
    QCOMPARE(bookCount(), 1);
    auto details = db([id = op.id](QSqlDatabase& d) -> Result<BookDetails> {
        auto o = catalog::importOperation(d, id);
        if (!o)
            return o.error();
        return catalog::bookDetails(d, *o.value().book);
    });
    QVERIFY(details);
    QCOMPARE(shaOf(m_service->layout().absolute(details.value().asset.managedPath)), sha);
    QVERIFY(filesUnder(m_service->layout().absolute(QStringLiteral("staging"))).isEmpty());
#endif
}

// The book's first metadata request is part of the import transaction, also
// when recovery completes the import; a duplicate queues nothing.
void TestImportService::importQueuesOneMetadataJob()
{
    const ImportResult first = m_service->importFile(fixture("title-page.pdf"));
    QCOMPARE(first.outcome, Outcome::Imported);
    m_service->setCrashHook([](ImportStage s) { return s == ImportStage::Installed; });
    QCOMPARE(m_service->importFile(fixture("contents-book.pdf")).outcome, Outcome::Interrupted);
    m_service->setCrashHook({});
    QCOMPARE(m_service->recover().registered, 1);
    QCOMPARE(m_service->importFile(fixture("title-page.pdf")).outcome, Outcome::Duplicate);

    const auto jobs = db([](QSqlDatabase& d) { return catalog::listJobs(d, false); }).value();
    QCOMPARE(jobs.size(), 2);
    for (const JobRecord& job : jobs) {
        QCOMPARE(job.kind, JobKind::Metadata);
        QCOMPARE(job.state, JobState::Queued);
        QCOMPARE(job.generation, 1);
        const BookDetails details = db([book = job.book](QSqlDatabase& d) { return catalog::bookDetails(d, book); }).value();
        QCOMPARE(job.sourceSha256, details.asset.sha256);
        QCOMPARE(details.metadataGeneration, 1);
    }
}

QTEST_GUILESS_MAIN(TestImportService)
#include "tst_importservice.moc"
