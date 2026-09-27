// SDK boundary: contents normalization of constructed reports, the real
// analyzer on the fixtures (analyze and analyze_book), and one end-to-end
// import through the coordinator with both SDK steps.
#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "processing/processingcoordinator.h"
#include "processing/sdk/contentsnormalize.h"
#include "processing/sdk/sdkcontentsanalyzer.h"
#include "processing/sdk/sdkmetadataextractor.h"
#include "storage/filecopy.h"
#include "storage/importservice.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::domain;
using mbl::processing::ContentsAnalysis;
using mbl::processing::MetadataExtraction;
namespace pb = pdfbookmark;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

pb::parsing::TocEntry parsed(const char* id, std::size_t order, const char* title, pb::parsing::HierarchyKind kind,
                             std::optional<std::string> parent = std::nullopt)
{
    pb::parsing::TocEntry e;
    e.id = id;
    e.order = order;
    e.title = title;
    e.hierarchy.kind = kind;
    e.hierarchy.parent_id = std::move(parent);
    pb::text::SourceReference source;
    source.page_index = 1;
    e.sources.push_back(source);
    return e;
}

pb::mapping::EntryMapping mapped(const char* id, pb::mapping::MappingStatus status, std::optional<std::size_t> page)
{
    pb::mapping::EntryMapping m;
    m.entry_id = id;
    m.status = status;
    if (page)
        m.pdf_page_index = pb::PageIndex(*page);
    m.method = pb::mapping::ResolutionMethod::InferredOffset;
    return m;
}

const TocEntry* entryById(const TocAnalysis& toc, const QString& id)
{
    for (const TocEntry& e : toc.entries) {
        if (e.sdkEntryId == id)
            return &e;
    }
    return nullptr;
}

} // namespace

class TestSdkContentsAnalyzer : public QObject {
    Q_OBJECT

private slots:
    void normalizationJoinsByIdAndKeepsEveryEntry();
    void analyzesTheFixtureContents();
    void analyzeBookDeliversMetadataFirstAndTheSameContents();
    void bookWithoutPrintedContentsIsAnOutcome();
    void cancelledBeforeStart();
    void endToEndImportPublishesBoth();
};

void TestSdkContentsAnalyzer::normalizationJoinsByIdAndKeepsEveryEntry()
{
    pb::AnalysisReport report;
    report.outcome = pb::AnalysisOutcome::AnalysisPartial;
    report.search_covered_document = true;
    report.stop_reasons = {"searched every page"};
    pb::parsing::ParsedToc toc;
    toc.completeness = pb::parsing::ParseCompleteness::Incomplete;
    // "child" names a parent listed after it; "loose" has unknown hierarchy.
    toc.entries.push_back(parsed("child", 0, "1.1 Child", pb::parsing::HierarchyKind::KnownParent, "parent"));
    toc.entries.push_back(parsed("parent", 1, "1 Parent", pb::parsing::HierarchyKind::Root));
    toc.entries.push_back(parsed("loose", 2, "Index", pb::parsing::HierarchyKind::Unknown));
    toc.entries.push_back(parsed("amb", 3, "Appendix", pb::parsing::HierarchyKind::Root));
    toc.entries[2].printed_reference = pb::parsing::PrintedReference{};
    toc.entries[2].printed_reference->literal = "xii";
    toc.entries[2].printed_reference->uncertain = true;
    report.parsed = toc;
    pb::mapping::MappingResult mapping;  // Deliberately not in parse order; "loose" is missing.
    mapping.entries.push_back(mapped("amb", pb::mapping::MappingStatus::Ambiguous, std::nullopt));
    mapping.entries.back().alternatives.push_back(pb::mapping::DestinationAlternative{4, {}, "label repeats", {}});
    mapping.entries.back().alternatives.push_back(pb::mapping::DestinationAlternative{9, {}, "label repeats", {}});
    mapping.entries.push_back(mapped("parent", pb::mapping::MappingStatus::Resolved, 0));
    mapping.entries.push_back(mapped("child", pb::mapping::MappingStatus::Resolved, 2));
    mapping.entries.back().reasons = {"page label matches"};
    report.mapping = mapping;
    pb::writer::BookmarkPlan plan;
    pb::writer::BookmarkNode node;
    node.id = "parent";
    node.title = "1 Parent";
    plan.nodes.push_back(node);
    node.id = "child";
    node.parent_id = "parent";
    node.title = "1.1 Child";
    plan.nodes.push_back(node);
    plan.omitted_entries.push_back(pb::writer::OmittedEntry{"amb", "ambiguous destination"});
    report.plan.ready = false;
    report.plan.blockers = {"unknown hierarchy"};
    report.plan.plan = plan;

    const TocAnalysis out = mbl::sdk::normalizeContents(report);
    QCOMPARE(out.outcome, QStringLiteral("analysis_partial"));
    QVERIFY(!out.planReady);
    QCOMPARE(out.parseComplete, std::optional<bool>(false));
    QCOMPARE(out.planBlockers, QStringList{QStringLiteral("unknown hierarchy")});
    QVERIFY(!out.planJson.isEmpty());
    QCOMPARE(out.entries.size(), 4);  // Every parsed entry, mapped or not.

    const TocEntry* child = entryById(out, QStringLiteral("child"));
    QVERIFY(child);
    QCOMPARE(child->hierarchy, HierarchyState::KnownParent);
    QCOMPARE(child->parentSdkEntryId, std::optional<QString>(QStringLiteral("parent")));
    QCOMPARE(child->destinationPage, std::optional<int>(2));  // Joined by ID, not by position.
    QCOMPARE(child->evidence.destinationReasons, QStringList{QStringLiteral("page label matches")});
    QVERIFY(child->inExportPlan);
    QCOMPARE(entryById(out, QStringLiteral("parent"))->destinationPage, std::optional<int>(0));

    const TocEntry* loose = entryById(out, QStringLiteral("loose"));
    QCOMPARE(loose->destinationState, DestinationState::Unresolved);  // No mapping at all.
    QCOMPARE(loose->hierarchy, HierarchyState::Unknown);
    QCOMPARE(loose->printedLabel, std::optional<QString>(QStringLiteral("xii")));
    QVERIFY(loose->evidence.printedLabelUncertain);
    QVERIFY(!loose->inExportPlan);

    const TocEntry* amb = entryById(out, QStringLiteral("amb"));
    QCOMPARE(amb->destinationState, DestinationState::Ambiguous);
    QVERIFY(!amb->destinationPage);  // No page is guessed.
    QCOMPARE(amb->evidence.alternativePages, (QList<int>{4, 9}));
    QCOMPARE(amb->evidence.omissionReason, std::optional<QString>(QStringLiteral("ambiguous destination")));
    QCOMPARE(amb->sourceTocPage, std::optional<int>(1));
}

void TestSdkContentsAnalyzer::analyzesTheFixtureContents()
{
    mbl::sdk::SdkContentsAnalyzer analyzer;
    std::atomic_bool cancel{false};
    QStringList stages;
    const ContentsAnalysis r = analyzer.analyze(fixture("contents-book.pdf"), cancel,
                                                [&](const QString& stage, int) { stages << stage; });
    QCOMPARE(r.status, ContentsAnalysis::Status::Completed);
    QCOMPARE(r.outcome, QStringLiteral("plan_ready"));
    QCOMPARE(r.toc.outcome, r.outcome);
    QVERIFY(r.toc.planReady);
    QCOMPARE(r.toc.parseComplete, std::optional<bool>(true));
    QCOMPARE(r.pageCount, 27);
    QCOMPARE(r.sourceSha256, mbl::storage::sha256OfFile(fixture("contents-book.pdf")).value());
    QVERIFY(!stages.isEmpty());
    const QList<TocEntry>& e = r.toc.entries;
    QCOMPARE(e.size(), 5);
    QCOMPARE(e.at(0).title, QStringLiteral("1 Introduction"));
    // The printed label is not the physical page: "1" is page index 3.
    QCOMPARE(e.at(0).printedLabel, std::optional<QString>(QStringLiteral("1")));
    QCOMPARE(e.at(0).destinationPage, std::optional<int>(3));
    QCOMPARE(e.at(0).sourceTocPage, std::optional<int>(2));
    QCOMPARE(e.at(3).title, QStringLiteral("3 Networking with TCP/IP"));
    QCOMPARE(e.at(3).destinationPage, std::optional<int>(14));
    QCOMPARE(e.at(2).hierarchy, HierarchyState::KnownParent);
    QCOMPARE(e.at(2).parentSdkEntryId, std::optional<QString>(e.at(1).sdkEntryId));
    for (const TocEntry& entry : e) {
        QCOMPARE(entry.destinationState, DestinationState::Resolved);
        QVERIFY(entry.inExportPlan);
        QCOMPARE(entry.evidence.destinationMethod, std::optional<QString>(QStringLiteral("inferred_offset")));
    }
    QVERIFY(!r.toc.planJson.isEmpty());
    const QJsonObject report = QJsonDocument::fromJson(r.reportJson).object();
    QCOMPARE(report.value(QStringLiteral("kind")).toString(), QStringLiteral("pdfbookmark.analysis"));
    const QJsonObject options = QJsonDocument::fromJson(r.optionsJson.toUtf8()).object();
    QCOMPARE(options.value(QStringLiteral("allow_partial")).toBool(true), false);
    QCOMPARE(options.value(QStringLiteral("title_style")).toString(), QStringLiteral("as_printed"));
}

void TestSdkContentsAnalyzer::analyzeBookDeliversMetadataFirstAndTheSameContents()
{
    mbl::sdk::SdkContentsAnalyzer analyzer;
    std::atomic_bool cancel{false};
    QList<MetadataExtraction> metadata;
    QStringList stagesAtMetadata;
    QStringList stages;
    const ContentsAnalysis book = analyzer.analyzeBook(
        fixture("contents-book.pdf"), cancel, [&](const QString& stage, int) { stages << stage; },
        [&](const MetadataExtraction& m) {
            metadata << m;
            stagesAtMetadata = stages;
        });
    QCOMPARE(metadata.size(), 1);  // Exactly once.
    QCOMPARE(metadata.first().status, MetadataExtraction::Status::Completed);
    QCOMPARE(metadata.first().pageCount, 27);
    QVERIFY(!metadata.first().reportJson.isEmpty());
    // Early: delivered while only the metadata stage had reported progress.
    for (const QString& stage : stagesAtMetadata)
        QCOMPARE(stage, QStringLiteral("metadata"));
    QCOMPARE(book.status, ContentsAnalysis::Status::Completed);

    const ContentsAnalysis alone = analyzer.analyze(fixture("contents-book.pdf"), cancel, {});
    QCOMPARE(book.toc.entries.size(), alone.toc.entries.size());
    for (qsizetype i = 0; i < book.toc.entries.size(); ++i) {
        QCOMPARE(book.toc.entries.at(i).title, alone.toc.entries.at(i).title);
        QCOMPARE(book.toc.entries.at(i).destinationPage, alone.toc.entries.at(i).destinationPage);
    }
    QCOMPARE(book.outcome, alone.outcome);
    QVERIFY(QJsonDocument::fromJson(book.optionsJson.toUtf8()).object().value(QStringLiteral("run")).toString()
            == QStringLiteral("analyze_book"));
}

void TestSdkContentsAnalyzer::bookWithoutPrintedContentsIsAnOutcome()
{
    mbl::sdk::SdkContentsAnalyzer analyzer;
    std::atomic_bool cancel{false};
    const ContentsAnalysis r = analyzer.analyze(fixture("title-page.pdf"), cancel, {});
    QCOMPARE(r.status, ContentsAnalysis::Status::Completed);  // Not a failure.
    QCOMPARE(r.outcome, QStringLiteral("no_toc_found_in_search"));
    QVERIFY(r.toc.entries.isEmpty());
    QVERIFY(!r.toc.parseComplete);
    QVERIFY(!r.toc.planReady);
}

void TestSdkContentsAnalyzer::cancelledBeforeStart()
{
    mbl::sdk::SdkContentsAnalyzer analyzer;
    std::atomic_bool cancel{true};
    QCOMPARE(analyzer.analyze(fixture("contents-book.pdf"), cancel, {}).status, ContentsAnalysis::Status::Cancelled);
    int calls = 0;
    MetadataExtraction::Status metadataStatus = MetadataExtraction::Status::Completed;
    const ContentsAnalysis book = analyzer.analyzeBook(fixture("contents-book.pdf"), cancel, {},
                                                       [&](const MetadataExtraction& m) {
                                                           ++calls;
                                                           metadataStatus = m.status;
                                                       });
    QCOMPARE(calls, 1);
    QCOMPARE(metadataStatus, MetadataExtraction::Status::Cancelled);
    QCOMPARE(book.status, ContentsAnalysis::Status::Cancelled);
}

void TestSdkContentsAnalyzer::endToEndImportPublishesBoth()
{
    QTemporaryDir root;
    auto library = mbl::catalog::Library::open(root.path());
    QVERIFY(library);
    mbl::storage::ImportService importer(*library.value());
    const auto imported = importer.importFile(fixture("contents-book.pdf"));
    QVERIFY(imported.book);
    mbl::processing::ProcessingCoordinator c(*library.value(), std::make_shared<mbl::sdk::SdkMetadataExtractor>(),
                                             std::make_shared<mbl::sdk::SdkContentsAnalyzer>());
    QSignalSpy metadata(&c, &mbl::processing::ProcessingCoordinator::metadataPublished);
    QSignalSpy contents(&c, &mbl::processing::ProcessingCoordinator::contentsPublished);
    c.start();
    QTRY_COMPARE_WITH_TIMEOUT(contents.size(), 1, 60000);
    QCOMPARE(metadata.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    auto details = library.value()->run([book = *imported.book](QSqlDatabase& db) {
        return mbl::catalog::bookDetails(db, book);
    }).result();
    QVERIFY(details);
    QVERIFY(details.value().toc);
    QCOMPARE(details.value().toc->entries.size(), 5);
    QCOMPARE(details.value().toc->outcome, QStringLiteral("plan_ready"));
    QCOMPARE(details.value().summary.tocEntryCount, 5);
    QCOMPARE(details.value().asset.pageCount, 27);
    QCOMPARE(details.value().toc->entries.at(0).evidence.sourcePages, QList<int>{2});
}

QTEST_GUILESS_MAIN(TestSdkContentsAnalyzer)
#include "tst_sdkcontentsanalyzer.moc"
