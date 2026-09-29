// A1: where an exported copy may be written. Real files in temporary
// folders: the library folder, managed sources and originals are protected
// under any name, and existing files are kept unless replacing was asked.
#include "storage/exportdestination.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>

using namespace mbl::domain;
using mbl::storage::ExportDestinationRules;
using mbl::storage::LibraryLayout;
using mbl::storage::suggestedExportPath;
using mbl::storage::validateExportDestination;

namespace {

bool writeFile(const QString& path, const QByteArray& bytes = QByteArrayLiteral("%PDF-1.4\n"))
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

} // namespace

class TestExportDestination : public QObject {
    Q_OBJECT

private slots:
    void init();

    void aNewPdfOutsideTheLibraryIsAccepted();
    void theLibraryFolderIsProtected();
    void protectedFilesUnderAnyName();
    void existingFilesAndBadNames();
    void suggestedNames();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_library;
    QString m_outside;
    QString m_source;    // A managed source.
    QString m_original;  // An imported original, outside the library.
};

void TestExportDestination::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_library = m_dir->filePath(QStringLiteral("Library"));
    m_outside = m_dir->filePath(QStringLiteral("Exports"));
    QVERIFY(QDir().mkpath(m_library) && QDir().mkpath(m_outside));
    m_source = m_library + QStringLiteral("/files/abc/source.pdf");
    m_original = m_dir->filePath(QStringLiteral("Downloads/book.pdf"));
    QVERIFY(writeFile(m_source) && writeFile(m_original));
    QVERIFY(writeFile(m_library + QStringLiteral("/library.sqlite"), QByteArrayLiteral("db")));
}

void TestExportDestination::aNewPdfOutsideTheLibraryIsAccepted()
{
    const LibraryLayout layout(m_library);
    auto ok = validateExportDestination(m_outside + QStringLiteral("/Book (bookmarked).PDF"), layout, {});
    QVERIFY2(ok, ok ? "" : qPrintable(ok.error().message));
    QVERIFY(ok.value().endsWith(QStringLiteral("/Book (bookmarked).PDF")));
    // Non-ASCII names.
    QVERIFY(validateExportDestination(m_outside + QStringLiteral("/Βιβλίο — 日本語.pdf"), layout, {}));
}

void TestExportDestination::theLibraryFolderIsProtected()
{
    const LibraryLayout layout(m_library);
    const QStringList inside{
        m_library + QStringLiteral("/new.pdf"),
        m_library + QStringLiteral("/derivatives/x.pdf"),
        m_library + QStringLiteral("/files/abc/copy.pdf"),
        m_outside + QStringLiteral("/../Library/sneaky.pdf"),  // Through "..".
#ifdef Q_OS_WIN
        m_library.toUpper() + QStringLiteral("/FILES/abc/other.pdf"),  // Another case.
#endif
    };
    QDir().mkpath(m_library + QStringLiteral("/derivatives"));
    for (const QString& path : inside) {
        auto refused = validateExportDestination(path, layout, {});
        QVERIFY2(!refused, qPrintable(path));
        QCOMPARE(refused.error().code, ErrorCode::InvalidArgument);
        QCOMPARE(refused.error().message, QStringLiteral("The copy cannot be saved inside the library folder."));
    }
}

void TestExportDestination::protectedFilesUnderAnyName()
{
    const LibraryLayout layout(m_library);
    ExportDestinationRules rules;
    rules.protectedFiles = {m_source, m_original};
    rules.replaceExisting = true;  // Even when replacing is allowed.

    auto original = validateExportDestination(m_original, layout, rules);
    QVERIFY(!original);
    QCOMPARE(original.error().code, ErrorCode::InvalidArgument);

    // A hard link to the managed source, outside the library: the same file.
    const QString link = m_outside + QStringLiteral("/linked.pdf");
    std::error_code ec;
    std::filesystem::create_hard_link(std::filesystem::path(QDir::toNativeSeparators(m_source).toStdWString()),
                                      std::filesystem::path(QDir::toNativeSeparators(link).toStdWString()), ec);
    if (ec)
        QSKIP("Hard links are not supported on this file system.");
    auto linked = validateExportDestination(link, layout, rules);
    QVERIFY(!linked);
    QCOMPARE(linked.error().code, ErrorCode::InvalidArgument);

    // A hard link to an internal library file that is not listed: refused,
    // because the file has other names.
    const QString catalogLink = m_outside + QStringLiteral("/catalog.pdf");
    std::filesystem::create_hard_link(
        std::filesystem::path(QDir::toNativeSeparators(m_library + QStringLiteral("/library.sqlite")).toStdWString()),
        std::filesystem::path(QDir::toNativeSeparators(catalogLink).toStdWString()), ec);
    QVERIFY(!ec);
    auto catalog = validateExportDestination(catalogLink, layout, rules);
    QVERIFY(!catalog);
    QCOMPARE(catalog.error().code, ErrorCode::InvalidArgument);

#ifdef Q_OS_WIN
    // A stream on a protected original.
    QCOMPARE(validateExportDestination(m_original + QStringLiteral(":bookmarks.pdf"), layout, rules).error().code,
             ErrorCode::InvalidArgument);
#endif

    // An unrelated existing file may be replaced when asked.
    const QString other = m_outside + QStringLiteral("/other.pdf");
    QVERIFY(writeFile(other));
    QVERIFY(validateExportDestination(other, layout, rules));
}

void TestExportDestination::existingFilesAndBadNames()
{
    const LibraryLayout layout(m_library);
    const QString existing = m_outside + QStringLiteral("/done.pdf");
    QVERIFY(writeFile(existing));
    auto kept = validateExportDestination(existing, layout, {});
    QCOMPARE(kept.error().code, ErrorCode::Duplicate);

    QCOMPARE(validateExportDestination(QString(), layout, {}).error().code, ErrorCode::InvalidArgument);
    QCOMPARE(validateExportDestination(QStringLiteral("relative.pdf"), layout, {}).error().code, ErrorCode::InvalidArgument);
    QCOMPARE(validateExportDestination(m_outside + QStringLiteral("/book.txt"), layout, {}).error().code,
             ErrorCode::InvalidArgument);
    QCOMPARE(validateExportDestination(m_outside + QStringLiteral("/missing/book.pdf"), layout, {}).error().code,
             ErrorCode::InvalidArgument);
    QVERIFY(QDir().mkpath(m_outside + QStringLiteral("/folder.pdf")));
    ExportDestinationRules replace;
    replace.replaceExisting = true;
    QCOMPARE(validateExportDestination(m_outside + QStringLiteral("/folder.pdf"), layout, replace).error().code,
             ErrorCode::InvalidArgument);

    // A library folder that cannot be resolved: refused, not unchecked.
    const LibraryLayout missing(m_dir->filePath(QStringLiteral("NoSuchLibrary")));
    QCOMPARE(validateExportDestination(m_outside + QStringLiteral("/new.pdf"), missing, {}).error().code,
             ErrorCode::InvalidArgument);
}

void TestExportDestination::suggestedNames()
{
    const QString first = suggestedExportPath(QStringLiteral("C++: The \"Guide\" / 2nd ed?."), m_outside);
    QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("C++_ The _Guide_ _ 2nd ed_ (bookmarked).pdf"));
    QVERIFY(writeFile(first));
    QCOMPARE(QFileInfo(suggestedExportPath(QStringLiteral("C++: The \"Guide\" / 2nd ed?."), m_outside)).fileName(),
             QStringLiteral("C++_ The _Guide_ _ 2nd ed_ (bookmarked) (2).pdf"));
    QCOMPARE(QFileInfo(suggestedExportPath(QStringLiteral("  ..  "), m_outside)).fileName(),
             QStringLiteral("Book (bookmarked).pdf"));
}

QTEST_GUILESS_MAIN(TestExportDestination)
#include "tst_exportdestination.moc"
