// SDK boundary: contents normalization of constructed reports, the real
// analyzer on the fixtures (analyze and analyze_book), numbering sections from
// page labels, and one end-to-end import through the coordinator with both
// SDK steps.
#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "processing/processingcoordinator.h"
#include "processing/sdk/contentsnormalize.h"
#include "processing/sdk/pagelabels.h"
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

pb::parsing::PrintedReference printedPage(const char* literal, pb::parsing::NumberingStyle style, std::uint32_t n)
{
    pb::parsing::PrintedReference ref;
    ref.literal = literal;
    ref.numbering = style;
    ref.ordinal = n;
    return ref;
}

struct LabelledEntry {
    const char* id;
    std::optional<pb::parsing::PrintedReference> printed;
    pb::mapping::MappingStatus status;
};

// A report of `pages` pages whose entries have the given printed pages and
// mapping states, as the SDK's default analysis would give them.
pb::AnalysisReport labelledReport(std::size_t pages, const QList<LabelledEntry>& entries)
{
    pb::AnalysisReport report;
    report.outcome = pb::AnalysisOutcome::AnalysisPartial;
    report.input.page_count = pb::PageCount(pages);
    pb::parsing::ParsedToc toc;
    pb::mapping::MappingResult mapping;
    for (const LabelledEntry& e : entries) {
        toc.entries.push_back(parsed(e.id, toc.entries.size(), e.id, pb::parsing::HierarchyKind::Root));
        toc.entries.back().printed_reference = e.printed;
        const bool placed = e.status == pb::mapping::MappingStatus::Resolved;
        mapping.entries.push_back(mapped(e.id, e.status, placed ? std::optional<std::size_t>(5) : std::nullopt));
    }
    report.parsed = toc;
    report.mapping = mapping;
    return report;
}

QString sectionOf(const pb::AnalysisOptions& options, const char* entry)
{
    for (const auto& association : options.entry_sections) {
        if (association.entry_id == entry)
            return QString::fromStdString(association.section_id);
    }
    return {};
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
    void pageLabelRunsFollowThePrintedNumbering();
    void readsThePdfPageLabels();
    void pageLabelSectionsAssociateEntriesWithTheirRun();
    void secondAnalysisIsKeptOnlyWhenItPlacesMore();
    void placesEntriesWhosePrintedNumbersSkipPages();
    void cancelDuringTheSecondAnalysisCancels();
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
    QVERIFY(!options.contains(QStringLiteral("page_label_sections")));  // Every entry placed: no second analysis.
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

void TestSdkContentsAnalyzer::pageLabelRunsFollowThePrintedNumbering()
{
    using mbl::sdk::PageLabelRun;
    using Style = PageLabelRun::Style;
    const QStringList labels{QStringLiteral("Cover"), QStringLiteral("i"),   QStringLiteral("ii"),
                             QStringLiteral("iii"),   QStringLiteral("1"),   QStringLiteral("2"),
                             QStringLiteral("3"),     QStringLiteral("5"),   QStringLiteral("6"),
                             QStringLiteral(" 7 "),   QStringLiteral("A-1"), QStringLiteral("a-2"),
                             QStringLiteral("1"),     QStringLiteral("2"),   QStringLiteral("iiii"),
                             QStringLiteral("XII")};
    const QList<PageLabelRun> runs = mbl::sdk::pageLabelRuns(labels);
    const QList<PageLabelRun> expected{
        {Style::Roman, {}, 1, 4, 1},      // i-iii
        {Style::Decimal, {}, 4, 7, 1},    // 1-3
        {Style::Decimal, {}, 7, 10, 5},   // 5-7: printed 4 is not in the file
        {Style::Prefixed, QStringLiteral("A"), 10, 12, 1},
        {Style::Decimal, {}, 12, 14, 1},  // Numbered from 1 again
        {Style::Roman, {}, 15, 16, 12},   // "iiii" is not a numeral
    };
    QCOMPARE(runs, expected);
    QVERIFY(mbl::sdk::numberingBreaks(runs));
    QCOMPARE(mbl::sdk::runHolding(runs, Style::Decimal, {}, 6), std::optional<qsizetype>(2));
    QCOMPARE(mbl::sdk::runHolding(runs, Style::Decimal, {}, 4), std::nullopt);  // In no run.
    QCOMPARE(mbl::sdk::runHolding(runs, Style::Decimal, {}, 2), std::nullopt);  // In two runs.
    QCOMPARE(mbl::sdk::runHolding(runs, Style::Prefixed, QStringLiteral("a"), 2), std::optional<qsizetype>(3));
    QCOMPARE(mbl::sdk::runHolding(runs, Style::Roman, {}, 12), std::optional<qsizetype>(5));
    QCOMPARE(mbl::sdk::runHolding(runs, Style::Roman, {}, 13), std::nullopt);

    // Labels that are the physical page numbers (or no labels at all) are one run.
    QStringList plain;
    for (int n = 1; n <= 5; ++n)
        plain << QString::number(n);
    QCOMPARE(mbl::sdk::pageLabelRuns(plain), (QList<PageLabelRun>{{Style::Decimal, {}, 0, 5, 1}}));
    QVERIFY(!mbl::sdk::numberingBreaks(mbl::sdk::pageLabelRuns(plain)));
    QVERIFY(mbl::sdk::pageLabelRuns({}).isEmpty());
}

void TestSdkContentsAnalyzer::readsThePdfPageLabels()
{
    QStringList expected{QStringLiteral("i"), QStringLiteral("ii"), QStringLiteral("iii")};
    for (int printed : {1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23, 25, 26, 27, 28,
                        29, 30})
        expected << QString::number(printed);
    QCOMPARE(mbl::sdk::readPageLabels(fixture("dropped-pages-book.pdf")), expected);

    // No page labels: Qt PDF gives the physical numbers, one run without a break.
    const QStringList plain = mbl::sdk::readPageLabels(fixture("contents-book.pdf"));
    QCOMPARE(plain.size(), 27);
    QCOMPARE(plain.first(), QStringLiteral("1"));
    QVERIFY(!mbl::sdk::numberingBreaks(mbl::sdk::pageLabelRuns(plain)));
    QVERIFY(mbl::sdk::readPageLabels(fixture("missing.pdf")).isEmpty());
}

void TestSdkContentsAnalyzer::pageLabelSectionsAssociateEntriesWithTheirRun()
{
    using Status = pb::mapping::MappingStatus;
    using Style = pb::parsing::NumberingStyle;
    // Front matter i-ii, then printed 1-3, 5-7 and 9-11: pages 4 and 8 are not in the file.
    const QStringList labels{QStringLiteral("i"), QStringLiteral("ii"), QStringLiteral("1"),  QStringLiteral("2"),
                             QStringLiteral("3"), QStringLiteral("5"),  QStringLiteral("6"),  QStringLiteral("7"),
                             QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("11"), QStringLiteral("Back")};
    auto uncertain = printedPage("9", Style::Decimal, 9);
    uncertain.uncertain = true;
    const pb::AnalysisReport first = labelledReport(
        12, {{"one", printedPage("1", Style::Decimal, 1), Status::Ambiguous},
             {"six", printedPage("6", Style::Decimal, 6), Status::Ambiguous},
             {"four", printedPage("4", Style::Decimal, 4), Status::Ambiguous},  // In no run.
             {"preface", printedPage("ii", Style::Roman, 2), Status::Resolved},
             {"nine", uncertain, Status::Unresolved},  // Not a readable number.
             {"part", std::nullopt, Status::Unresolved}});
    pb::AnalysisOptions options;
    options.plan.allow_partial = false;
    options.limits.ocr_budget = 7;

    const auto second = mbl::sdk::pageLabelAnalysisOptions(labels, first, options);
    QVERIFY(second);
    QVERIFY(second->sections);
    const auto& sections = *second->sections;
    QCOMPARE(sections.size(), std::size_t(4));
    QCOMPARE(sections[0].style, Style::Roman);
    QCOMPARE(sections[0].first, pb::PageIndex(0));
    QCOMPARE(sections[0].end, pb::PageIndex(2));
    QCOMPARE(sections[2].style, Style::Decimal);
    QCOMPARE(sections[2].first, pb::PageIndex(5));
    QCOMPARE(sections[2].end, pb::PageIndex(8));
    for (const auto& section : sections) {
        QVERIFY(!section.origin.empty());
        QVERIFY(!section.viewer_labels_match_printed);  // The SDK confirms pages from their content.
    }
    QCOMPARE(sectionOf(*second, "one"), QString::fromStdString(sections[1].id));
    QCOMPARE(sectionOf(*second, "six"), QString::fromStdString(sections[2].id));
    QCOMPARE(sectionOf(*second, "preface"), QString::fromStdString(sections[0].id));
    QVERIFY(sectionOf(*second, "four").isEmpty());
    QVERIFY(sectionOf(*second, "nine").isEmpty());
    QVERIFY(sectionOf(*second, "part").isEmpty());
    QCOMPARE(second->entry_sections.size(), std::size_t(3));
    QCOMPARE(second->plan.allow_partial, false);  // The other options are kept.
    QCOMPARE(second->limits.ocr_budget, std::size_t(7));

    // Nothing more to place: no second analysis.
    QVERIFY(!mbl::sdk::pageLabelAnalysisOptions(
        labels, labelledReport(12, {{"one", printedPage("1", Style::Decimal, 1), Status::Resolved}}), options));
    QVERIFY(!mbl::sdk::pageLabelAnalysisOptions(
        labels, labelledReport(12, {{"four", printedPage("4", Style::Decimal, 4), Status::Ambiguous}}), options));
    QStringList plain;
    for (int n = 1; n <= 12; ++n)
        plain << QString::number(n);
    QVERIFY(!mbl::sdk::pageLabelAnalysisOptions(plain, first, options));          // No break in the numbering.
    QVERIFY(!mbl::sdk::pageLabelAnalysisOptions(labels.mid(1), first, options));  // Not one label per page.
    QVERIFY(!mbl::sdk::pageLabelAnalysisOptions({}, first, options));
}

void TestSdkContentsAnalyzer::secondAnalysisIsKeptOnlyWhenItPlacesMore()
{
    using Status = pb::mapping::MappingStatus;
    using Style = pb::parsing::NumberingStyle;
    const pb::AnalysisReport first = labelledReport(12, {{"one", printedPage("1", Style::Decimal, 1), Status::Resolved},
                                                         {"six", printedPage("6", Style::Decimal, 6), Status::Ambiguous},
                                                         {"nine", printedPage("9", Style::Decimal, 9), Status::Ambiguous}});
    const pb::AnalysisReport more = labelledReport(12, {{"one", printedPage("1", Style::Decimal, 1), Status::Resolved},
                                                        {"six", printedPage("6", Style::Decimal, 6), Status::Resolved},
                                                        {"nine", printedPage("9", Style::Decimal, 9), Status::Ambiguous}});
    const pb::AnalysisReport others = labelledReport(
        12, {{"one", printedPage("1", Style::Decimal, 1), Status::Unresolved},
             {"six", printedPage("6", Style::Decimal, 6), Status::Resolved},
             {"nine", printedPage("9", Style::Decimal, 9), Status::Ambiguous}});
    QVERIFY(mbl::sdk::placesMoreEntries(more, first));
    QVERIFY(!mbl::sdk::placesMoreEntries(first, first));
    QVERIFY(!mbl::sdk::placesMoreEntries(others, first));  // As many, not more.
    QVERIFY(!mbl::sdk::placesMoreEntries(first, more));

    pb::AnalysisReport renamed = more;
    renamed.parsed->entries[2].title = "Nine";
    QVERIFY(!mbl::sdk::placesMoreEntries(renamed, first));  // Not the same contents.
    pb::AnalysisReport relabelled = more;
    relabelled.parsed->entries[2].printed_reference->literal = "10";
    QVERIFY(!mbl::sdk::placesMoreEntries(relabelled, first));
    pb::AnalysisReport shorter = more;
    shorter.parsed->entries.pop_back();
    QVERIFY(!mbl::sdk::placesMoreEntries(shorter, first));
}

void TestSdkContentsAnalyzer::placesEntriesWhosePrintedNumbersSkipPages()
{
    // The printed numbers skip one page at each chapter end. The SDK's default
    // single section places none of these entries; the page labels place all.
    mbl::sdk::SdkContentsAnalyzer analyzer;
    std::atomic_bool cancel{false};
    const ContentsAnalysis r = analyzer.analyze(fixture("dropped-pages-book.pdf"), cancel, {});
    QCOMPARE(r.status, ContentsAnalysis::Status::Completed);
    QCOMPARE(r.outcome, QStringLiteral("plan_ready"));
    QCOMPARE(r.pageCount, 30);
    const QList<TocEntry>& e = r.toc.entries;
    QCOMPARE(e.size(), 5);
    const QStringList printed{QStringLiteral("1"), QStringLiteral("9"), QStringLiteral("11"), QStringLiteral("17"),
                              QStringLiteral("25")};
    const QList<int> pages{3, 10, 12, 17, 24};
    for (qsizetype i = 0; i < e.size(); ++i) {
        QCOMPARE(e.at(i).printedLabel, std::optional<QString>(printed.at(i)));
        QCOMPARE(e.at(i).destinationState, DestinationState::Resolved);
        QCOMPARE(e.at(i).destinationPage, std::optional<int>(pages.at(i)));
    }
    const QJsonObject options = QJsonDocument::fromJson(r.optionsJson.toUtf8()).object();
    QCOMPARE(options.value(QStringLiteral("page_label_sections")).toString(), QStringLiteral("used"));
    const QJsonObject report = QJsonDocument::fromJson(r.reportJson).object();
    QVERIFY(report.value(QStringLiteral("options")).toObject().value(QStringLiteral("sections_supplied")).toBool());

    // The same through analyze_book, with the metadata delivered once.
    int metadataCalls = 0;
    const ContentsAnalysis book = analyzer.analyzeBook(fixture("dropped-pages-book.pdf"), cancel, {},
                                                       [&](const MetadataExtraction&) { ++metadataCalls; });
    QCOMPARE(metadataCalls, 1);
    QCOMPARE(book.status, ContentsAnalysis::Status::Completed);
    QCOMPARE(book.toc.entries.size(), 5);
    for (qsizetype i = 0; i < book.toc.entries.size(); ++i)
        QCOMPARE(book.toc.entries.at(i).destinationPage, std::optional<int>(pages.at(i)));
    const QJsonObject bookOptions = QJsonDocument::fromJson(book.optionsJson.toUtf8()).object();
    QCOMPARE(bookOptions.value(QStringLiteral("page_label_sections")).toString(), QStringLiteral("used"));
    QCOMPARE(bookOptions.value(QStringLiteral("run")).toString(), QStringLiteral("analyze_book"));
}

void TestSdkContentsAnalyzer::cancelDuringTheSecondAnalysisCancels()
{
    // The second analysis counts on from the first one's pages, so a count
    // above the page count (30) is progress of the second analysis.
    mbl::sdk::SdkContentsAnalyzer analyzer;
    std::atomic_bool cancel{false};
    int highest = 0;
    const ContentsAnalysis r = analyzer.analyze(fixture("dropped-pages-book.pdf"), cancel,
                                                [&](const QString&, int pages) {
                                                    highest = std::max(highest, pages);
                                                    if (pages > 30)
                                                        cancel = true;
                                                });
    QVERIFY2(highest > 30, "the second analysis reported no progress");
    QCOMPARE(r.status, ContentsAnalysis::Status::Cancelled);
    QVERIFY(r.toc.entries.isEmpty());  // Nothing to publish.
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
