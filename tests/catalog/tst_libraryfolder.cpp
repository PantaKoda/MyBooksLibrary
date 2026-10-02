// A2 (issue #30): whether a folder is an existing library, and opening one
// only if it is (Library::OpenMode::ExistingOnly), on real folders. A missing
// folder, an empty one, a file, the folder around a library, a library's
// own subfolder and a backup are refused with the reason and left exactly as
// they were; an existing library opens; the default mode still makes a new
// library on first use.
#include "catalog/library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::domain;
using mbl::catalog::Library;

namespace {

QStringList entriesOf(const QString& folder)
{
    return QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
}

bool writeFile(const QString& path, const QByteArray& bytes = QByteArrayLiteral("x"))
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

bool makeLibrary(const QString& root)
{
    auto created = Library::open(root);
    return bool(created);  // Closed again at the end of this scope.
}

} // namespace

class TestLibraryFolder : public QObject {
    Q_OBJECT

private slots:
    void anExistingLibraryOpens();
    void notALibraryIsRefusedAndLeftAlone_data();
    void notALibraryIsRefusedAndLeftAlone();
    void aBackupIsRefusedAndLeftAlone();
    void createIfMissingStillMakesALibrary();
};

void TestLibraryFolder::anExistingLibraryOpens()
{
    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("Βιβλιοθήκη"));  // A non-ASCII name.
    QVERIFY(makeLibrary(root));
    const auto existing = Library::checkExisting(root);
    QVERIFY2(existing, existing ? "" : qPrintable(existing.error().message));
    {
        auto opened = Library::open(root, Library::OpenMode::ExistingOnly);
        QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
        QCOMPARE(opened.value()->rootDir(), QDir::cleanPath(root));
    }
    // Written another way: native separators, and through "..".
    QVERIFY(Library::checkExisting(QDir::toNativeSeparators(root)));
    QVERIFY(Library::checkExisting(root + QStringLiteral("/../") + QFileInfo(root).fileName()));
}

void TestLibraryFolder::notALibraryIsRefusedAndLeftAlone_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<int>("code");
    QTest::addColumn<QString>("reason");
    QTest::newRow("a folder that does not exist (moved, or a drive not connected)")
        << QStringLiteral("missing") << int(ErrorCode::NotFound) << QStringLiteral("does not exist");
    QTest::newRow("an empty folder") << QStringLiteral("empty") << int(ErrorCode::NotFound)
                                     << QStringLiteral("is not a MyBooksLibrary library");
    QTest::newRow("a file") << QStringLiteral("file") << int(ErrorCode::InvalidArgument) << QStringLiteral("is a file");
    QTest::newRow("the folder around a library") << QStringLiteral("parent") << int(ErrorCode::NotFound)
                                                 << QStringLiteral("is not a MyBooksLibrary library");
    QTest::newRow("a library's own files folder") << QStringLiteral("subfolder") << int(ErrorCode::NotFound)
                                                  << QStringLiteral("is not a MyBooksLibrary library");
}

void TestLibraryFolder::notALibraryIsRefusedAndLeftAlone()
{
    QFETCH(QString, kind);
    QFETCH(int, code);
    QFETCH(QString, reason);
    QTemporaryDir dir;
    const QString library = dir.filePath(QStringLiteral("Parent/Library"));
    QVERIFY(makeLibrary(library));
    QString chosen;
    if (kind == QLatin1String("missing")) {
        chosen = dir.filePath(QStringLiteral("Moved library"));
    } else if (kind == QLatin1String("empty")) {
        chosen = dir.filePath(QStringLiteral("Empty"));
        QVERIFY(QDir().mkpath(chosen));
    } else if (kind == QLatin1String("file")) {
        chosen = dir.filePath(QStringLiteral("notes.txt"));
        QVERIFY(writeFile(chosen));
    } else if (kind == QLatin1String("parent")) {
        chosen = dir.filePath(QStringLiteral("Parent"));
    } else {
        chosen = library + QStringLiteral("/files");
        QVERIFY(QDir().mkpath(chosen));
    }
    const QStringList before = QFileInfo(chosen).isDir() ? entriesOf(chosen) : QStringList();
    const QStringList libraryBefore = entriesOf(library);

    const auto existing = Library::checkExisting(chosen);
    QVERIFY(!existing);
    QCOMPARE(int(existing.error().code), code);
    QVERIFY2(existing.error().message.contains(reason), qPrintable(existing.error().message));
    auto opened = Library::open(chosen, Library::OpenMode::ExistingOnly);
    QVERIFY(!opened);
    QCOMPARE(int(opened.error().code), code);
    QCOMPARE(opened.error().message, existing.error().message);

    // Nothing was made: no folder, no catalog, no lock.
    if (kind == QLatin1String("missing"))
        QVERIFY(!QFileInfo::exists(chosen));
    else if (QFileInfo(chosen).isDir())
        QCOMPARE(entriesOf(chosen), before);
    QCOMPARE(entriesOf(library), libraryBefore);
}

void TestLibraryFolder::aBackupIsRefusedAndLeftAlone()
{
    QTemporaryDir dir;
    // A backup's layout: its catalog, and the manifest that marks it.
    const QString backup = dir.filePath(QStringLiteral("MyBooksLibrary backup 2026-09-29 120000"));
    QVERIFY(writeFile(QDir(backup).filePath(QLatin1StringView(Library::kCatalogFileName)), QByteArrayLiteral("catalog")));
    QVERIFY(writeFile(QDir(backup).filePath(QLatin1StringView(Library::kBackupManifestFileName)), QByteArrayLiteral("{}")));
    const QStringList before = entriesOf(backup);
    const auto existing = Library::checkExisting(backup);
    QVERIFY(!existing);
    QCOMPARE(existing.error().code, ErrorCode::InvalidArgument);
    QVERIFY2(existing.error().message.contains(QStringLiteral("is a MyBooksLibrary backup, not a library")),
             qPrintable(existing.error().message));
    QVERIFY(!Library::open(backup, Library::OpenMode::ExistingOnly));
    QCOMPARE(entriesOf(backup), before);
}

void TestLibraryFolder::createIfMissingStillMakesALibrary()
{
    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("First start/Library"));
    {
        auto opened = Library::open(root);  // CreateIfMissing, the default.
        QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    }
    QVERIFY(QFileInfo(QDir(root).filePath(QLatin1StringView(Library::kCatalogFileName))).isFile());
    QVERIFY(Library::checkExisting(root));
}

QTEST_GUILESS_MAIN(TestLibraryFolder)
#include "tst_libraryfolder.moc"
