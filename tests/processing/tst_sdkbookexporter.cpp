// SDK boundary: SdkBookExporter writes a bookmarked copy with the real
// pdfbookmark SDK on a fixture. The source is never modified; an existing
// output, a changed source, an invalid plan and a cancel before the write
// are refused; the committed copy is reported exactly as verified.
#include "processing/sdk/sdkbookexporter.h"
#include "storage/filecopy.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::domain;
using mbl::processing::ExportResult;
using mbl::sdk::SdkBookExporter;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

TocEntry entry(const char* id, const QString& title, std::optional<int> page, const char* parent = nullptr)
{
    TocEntry e;
    e.sdkEntryId = QLatin1StringView(id);
    e.title = title;
    e.hierarchy = parent ? HierarchyState::KnownParent : HierarchyState::Root;
    if (parent)
        e.parentSdkEntryId = QString::fromLatin1(parent);
    if (page) {
        e.destinationState = DestinationState::Resolved;
        e.destinationPage = page;
    }
    return e;
}

} // namespace

class TestSdkBookExporter : public QObject {
    Q_OBJECT

private slots:
    void init();

    void writesAVerifiedCopyAndLeavesTheSourceAlone();
    void plansBuiltFromContentsAreAccepted();
    void refusals();
    void cancelBeforeTheWriteWritesNothing();

private:
    ExportPlan plan() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_source;
    QString m_sourceSha;
};

void TestSdkBookExporter::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    // A copy, like a managed source; with a non-ASCII name.
    m_source = m_dir->filePath(QStringLiteral("Βιβλίο source.pdf"));
    QVERIFY(QFile::copy(fixture("contents-book.pdf"), m_source));
    m_sourceSha = mbl::storage::sha256OfFile(m_source).value_or(QString());
    QCOMPARE(m_sourceSha.size(), 64);
}

ExportPlan TestSdkBookExporter::plan() const
{
    ExportPlan p;
    p.sourceSha256 = m_sourceSha;
    p.pageCount = 27;
    // A child listed before its parent; page index 0 is valid.
    p.nodes = {ExportNode{QStringLiteral("c"), QStringLiteral("p"), QStringLiteral("2.1 Installing the Tools"), 8},
               ExportNode{QStringLiteral("p"), std::nullopt, QStringLiteral("2 Getting Started"), 7},
               ExportNode{QStringLiteral("t"), std::nullopt, QStringLiteral("Title page"), 0},
               ExportNode{QStringLiteral("n"), std::nullopt, QStringLiteral("3 Networking with TCP/IP — ή"), 14}};
    p.omitted = {ExportOmission{QStringLiteral("x"), QStringLiteral("Index"), QStringLiteral("No page was found for it.")}};
    return p;
}

void TestSdkBookExporter::writesAVerifiedCopyAndLeavesTheSourceAlone()
{
    SdkBookExporter exporter;
    std::atomic_bool cancel{false};
    const QString output = m_dir->filePath(QStringLiteral("Βιβλίο (bookmarked).pdf"));
    const ExportResult r = exporter.exportCopy(m_source, output, plan(), false, cancel);
    QVERIFY2(r.status == ExportResult::Status::Committed, qPrintable(r.error + u' ' + r.planIssues.join(u';')));
    QVERIFY(r.committed);
    QCOMPARE(r.outlineItems, 4);
    QCOMPARE(r.pageCount, 27);
    QVERIFY(r.structureMatches);
    QVERIFY(r.sourceUnchanged);
    QVERIFY(QFile::exists(output));
    QCOMPARE(mbl::storage::sha256OfFile(output).value_or(QString()), r.outputSha256);
    QVERIFY(r.planJson.contains(QStringLiteral("\"nodes\"")));
    QVERIFY(!r.sdkVersion.isEmpty());
    QCOMPARE(mbl::storage::sha256OfFile(m_source).value_or(QString()), m_sourceSha);  // Never modified.

    // Again to the same file: refused unless replacing.
    const ExportResult again = exporter.exportCopy(m_source, output, plan(), false, cancel);
    QCOMPARE(again.status, ExportResult::Status::Failed);
    QVERIFY(!again.committed);
    QCOMPARE(again.errorCode, QStringLiteral("OutputExists"));
    const ExportResult replaced = exporter.exportCopy(m_source, output, plan(), true, cancel);
    QCOMPARE(replaced.status, ExportResult::Status::Committed);
    QCOMPARE(mbl::storage::sha256OfFile(m_source).value_or(QString()), m_sourceSha);
}

// Every shape buildExportPlan produces passes the SDK's validate_plan and
// commits: omissions, each kind of promotion, and uncertain levels.
void TestSdkBookExporter::plansBuiltFromContentsAreAccepted()
{
    TocEntry unknown = entry("k", QStringLiteral("Level unknown"), 5);
    unknown.hierarchy = HierarchyState::Unknown;
    TocEntry ambiguous = entry("a", QStringLiteral("Ambiguous"), std::nullopt);
    ambiguous.destinationState = DestinationState::Ambiguous;
    TocEntry removed = entry("x", QStringLiteral("Removed"), 9, "r");
    removed.removed = true;
    TocEntry removedRoot = entry("z", QStringLiteral("Removed root"), 12);
    removedRoot.removed = true;

    const QList<std::pair<const char*, QList<TocEntry>>> shapes{
        {"no promotion", {entry("r", QStringLiteral("Part I"), 1), entry("c", QStringLiteral("Chapter"), 2, "r")}},
        {"parent without a page, to the grandparent",
         {entry("r", QStringLiteral("Part I"), 1), entry("u", QStringLiteral("No page"), std::nullopt, "r"),
          entry("s", QStringLiteral("Section"), 3, "u")}},
        {"ambiguous parent, to the top level", {ambiguous, entry("t", QStringLiteral("Under ambiguous"), 4, "a")}},
        {"uncertain level", {entry("r", QStringLiteral("Part I"), 1), unknown}},
        {"only an uncertain level", {unknown}},
        {"removed parent, to the grandparent",
         {entry("r", QStringLiteral("Part I"), 1), removed, entry("y", QStringLiteral("Under removed"), 10, "x")}},
        {"removed parent, to the top level", {removedRoot, entry("w", QStringLiteral("Under removed root"), 13, "z")}},
        {"blank title omitted, child moved up",
         {entry("b", QStringLiteral("  "), 6), entry("v", QStringLiteral("Under blank"), 7, "b")}},
        {"all together",
         {entry("r", QStringLiteral("Part I"), 1), entry("u", QStringLiteral("No page"), std::nullopt, "r"),
          entry("s", QStringLiteral("Section"), 3, "u"), ambiguous, entry("t", QStringLiteral("Under ambiguous"), 4, "a"),
          unknown, removed, entry("y", QStringLiteral("Under removed"), 10, "x")}},
    };

    AssetRecord asset;
    asset.sha256 = m_sourceSha;
    asset.pageCount = 27;
    SdkBookExporter exporter;
    std::atomic_bool cancel{false};
    int n = 0;
    for (const auto& [name, entries] : shapes) {
        TocAnalysis contents;
        contents.entries = entries;
        for (int i = 0; i < contents.entries.size(); ++i)
            contents.entries[i].order = i;
        const auto plan = buildExportPlan(contents, asset);
        QVERIFY2(plan, name);
        const QString output = m_dir->filePath(QStringLiteral("shape %1.pdf").arg(++n));
        const ExportResult r = exporter.exportCopy(m_source, output, plan.value(), false, cancel);
        QVERIFY2(r.status == ExportResult::Status::Committed,
                 qPrintable(QLatin1StringView(name) + u": " + r.errorCode + u' ' + r.planIssues.join(u';')));
        QCOMPARE(r.outlineItems, plan.value().nodes.size());
        QVERIFY(r.structureMatches);
    }
    QCOMPARE(mbl::storage::sha256OfFile(m_source).value_or(QString()), m_sourceSha);
}

void TestSdkBookExporter::refusals()
{
    SdkBookExporter exporter;
    std::atomic_bool cancel{false};
    const QString output = m_dir->filePath(QStringLiteral("out.pdf"));

    // The source itself as output: refused, even when replacing.
    const ExportResult onSource = exporter.exportCopy(m_source, m_source, plan(), true, cancel);
    QCOMPARE(onSource.status, ExportResult::Status::Failed);
    QCOMPARE(mbl::storage::sha256OfFile(m_source).value_or(QString()), m_sourceSha);

    // A plan bound to other bytes: the source changed since.
    ExportPlan stale = plan();
    stale.sourceSha256 = QString(64, u'f');  // Not all zeros: that reads as no digest.
    const ExportResult changed = exporter.exportCopy(m_source, output, stale, false, cancel);
    QCOMPARE(changed.status, ExportResult::Status::Failed);
    QCOMPARE(changed.errorCode, QStringLiteral("InputChanged"));
    QVERIFY(!QFile::exists(output));

    // An invalid plan: a page past the end, a parent that is not a bookmark.
    ExportPlan invalid = plan();
    invalid.nodes[3].page = 27;
    invalid.nodes[0].parentId = QStringLiteral("missing");
    const ExportResult bad = exporter.exportCopy(m_source, output, invalid, false, cancel);
    QCOMPARE(bad.status, ExportResult::Status::Failed);
    QCOMPARE(bad.errorCode, QStringLiteral("InvalidPlan"));
    QVERIFY(bad.planIssues.size() >= 2);
    QVERIFY(!QFile::exists(output));
}

void TestSdkBookExporter::cancelBeforeTheWriteWritesNothing()
{
    SdkBookExporter exporter;
    std::atomic_bool cancel{true};
    const QString output = m_dir->filePath(QStringLiteral("cancelled.pdf"));
    const ExportResult r = exporter.exportCopy(m_source, output, plan(), false, cancel);
    QCOMPARE(r.status, ExportResult::Status::Cancelled);
    QVERIFY(!r.committed);
    QVERIFY(!QFile::exists(output));
}

QTEST_GUILESS_MAIN(TestSdkBookExporter)
#include "tst_sdkbookexporter.moc"
