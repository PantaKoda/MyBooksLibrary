// SDK boundary: semantic snapshots detect result changes that counts miss,
// and ignore transient diagnostics. Also checks that a run without OCR models
// cannot count as an exercised OCR workload.
#include "processing/sdk/sdkinfo.h"
#include "processing/sdk/sdksnapshot.h"
#include "reader/checkverdict.h"

#include <QDir>
#include <QTest>

using namespace mbl::sdk;
namespace pb = pdfbookmark;

namespace {

pb::AnalysisReport fiveEntryReport()
{
    pb::AnalysisReport r;
    r.outcome = pb::AnalysisOutcome::PlanReady;
    r.search_pages = {0, 1, 2};
    r.parsed = pb::parsing::ParsedToc{};
    r.mapping = pb::mapping::MappingResult{};
    r.plan.ready = true;
    r.plan.plan = pb::BookmarkPlan{};
    for (int i = 0; i < 5; ++i) {
        const std::string id = "e" + std::to_string(i);
        pb::parsing::TocEntry e;
        e.id = id;
        e.order = std::size_t(i);
        e.title = "Chapter " + std::to_string(i);
        e.hierarchy.kind = pb::parsing::HierarchyKind::Root;
        r.parsed->entries.push_back(e);

        pb::mapping::EntryMapping m;
        m.entry_id = id;
        m.status = pb::mapping::MappingStatus::Resolved;
        m.pdf_page_index = 3 + 4 * i;
        r.mapping->entries.push_back(m);

        pb::BookmarkNode node;
        node.id = id;
        node.title = e.title;
        node.destination.pdf_page_index = 3 + 4 * i;
        r.plan.plan->nodes.push_back(node);
    }
    return r;
}

pb::MetadataReport metadataReport()
{
    pb::MetadataReport r;
    r.result.title.status = pb::metadata::FieldStatus::Resolved;
    r.result.title.value = pb::metadata::TitleValue{"Library Systems", std::string("In Practice")};
    r.result.contributors.status = pb::metadata::FieldStatus::Resolved;
    r.result.contributors.value = std::vector<pb::metadata::Contributor>{
        {"Alex Sample", pb::metadata::ContributorRole::Author},
        {"Bo Editor", pb::metadata::ContributorRole::Editor}};
    r.result.copyright_year.status = pb::metadata::FieldStatus::Ambiguous;
    pb::metadata::Candidate<pb::metadata::YearValue> candidate;
    candidate.value = {2021, pb::metadata::YearKind::Copyright, "Copyright 2021"};
    candidate.score = 0.5;
    r.result.copyright_year.alternatives.push_back(candidate);
    r.searched_pages = {0, 1};
    return r;
}

} // namespace

class TestSdkSnapshot : public QObject {
    Q_OBJECT

private slots:
    void identicalReportsMatch();
    void shiftedDestinationWithSameCountsDiffers();
    void titleParentAndPlanChangesDiffer();
    void transientFieldsAreIgnored();
    void metadataChangesDiffer();
    void encodingHasNoCollisions();
    void withoutModelsOcrIsNotExercised();
};

void TestSdkSnapshot::identicalReportsMatch()
{
    QCOMPARE(snapshotDigest(analysisSnapshot(fiveEntryReport())), snapshotDigest(analysisSnapshot(fiveEntryReport())));
    QCOMPARE(snapshotDigest(metadataSnapshot(metadataReport())), snapshotDigest(metadataSnapshot(metadataReport())));
}

// The reviewer's example: the same outcome, counts and readiness, but every
// resolved destination shifted by one page.
void TestSdkSnapshot::shiftedDestinationWithSameCountsDiffers()
{
    const auto base = fiveEntryReport();
    auto one = base;
    *one.mapping->entries[2].pdf_page_index += 1;
    QVERIFY(analysisSnapshot(one) != analysisSnapshot(base));

    auto all = base;
    for (auto& m : all.mapping->entries)
        *m.pdf_page_index += 1;
    for (auto& n : all.plan.plan->nodes)
        n.destination.pdf_page_index += 1;
    QVERIFY(snapshotDigest(analysisSnapshot(all)) != snapshotDigest(analysisSnapshot(base)));
}

void TestSdkSnapshot::titleParentAndPlanChangesDiffer()
{
    const auto base = fiveEntryReport();
    auto title = base;
    title.parsed->entries[1].title = "Chapter One";
    QVERIFY(analysisSnapshot(title) != analysisSnapshot(base));

    auto parent = base;
    parent.parsed->entries[3].hierarchy.kind = pb::parsing::HierarchyKind::KnownParent;
    parent.parsed->entries[3].hierarchy.parent_id = "e2";
    QVERIFY(analysisSnapshot(parent) != analysisSnapshot(base));

    auto plan = base;
    plan.plan.plan->nodes[4].destination.pdf_page_index = 0;
    QVERIFY(analysisSnapshot(plan) != analysisSnapshot(base));

    auto status = base;
    status.mapping->entries[0].status = pb::mapping::MappingStatus::Ambiguous;
    QVERIFY(analysisSnapshot(status) != analysisSnapshot(base));
}

void TestSdkSnapshot::transientFieldsAreIgnored()
{
    const auto base = fiveEntryReport();
    auto noisy = base;
    noisy.diagnostics.push_back("timing: 123 ms");
    noisy.stop_reasons.push_back("budget reached");
    noisy.ocr_attempts_used = 7;
    noisy.pages.resize(2);
    noisy.mapping->diagnostics.push_back("round 2");
    QCOMPARE(analysisSnapshot(noisy), analysisSnapshot(base));

    auto meta = metadataReport();
    meta.result.diagnostics.push_back("x");
    meta.ocr_attempts_used = 3;
    QCOMPARE(metadataSnapshot(meta), metadataSnapshot(metadataReport()));
}

void TestSdkSnapshot::metadataChangesDiffer()
{
    const auto base = metadataSnapshot(metadataReport());
    auto role = metadataReport();
    (*role.result.contributors.value)[1].role = pb::metadata::ContributorRole::Translator;
    QVERIFY(metadataSnapshot(role) != base);

    auto order = metadataReport();
    std::swap((*order.result.contributors.value)[0], (*order.result.contributors.value)[1]);
    QVERIFY(metadataSnapshot(order) != base);

    auto alternative = metadataReport();
    alternative.result.copyright_year.alternatives[0].value.year = 2022;
    QVERIFY(metadataSnapshot(alternative) != base);

    auto subtitle = metadataReport();
    subtitle.result.title.value->subtitle.reset();
    QVERIFY(metadataSnapshot(subtitle) != base);
}

// Regressions for the PR #4 re-review: separators, absent values and line
// breaks inside document text must not make different results encode alike.
void TestSdkSnapshot::encodingHasNoCollisions()
{
    const auto withTitle = [](std::string title, std::optional<std::string> subtitle) {
        auto r = metadataReport();
        r.result.title.value = pb::metadata::TitleValue{std::move(title), std::move(subtitle)};
        return metadataSnapshot(r);
    };
    // The reviewer's example.
    QVERIFY(withTitle("A|B", std::string("C")) != withTitle("A", std::string("B|C")));
    // An absent subtitle is not the text "-" or "null".
    QVERIFY(withTitle("T", std::nullopt) != withTitle("T", std::string("-")));
    QVERIFY(withTitle("T", std::nullopt) != withTitle("T", std::string("null")));
    QVERIFY(withTitle("T", std::nullopt) != withTitle("T", std::string("")));
    // Quotes and backslashes are escaped distinctly.
    QVERIFY(withTitle("a\"b", std::nullopt) != withTitle("a\\\"b", std::nullopt));
    QVERIFY(withTitle("a\\", std::string("b")) != withTitle("a", std::string("\b")));

    // One contributor whose name contains the old separators vs two contributors.
    auto one = metadataReport();
    one.result.contributors.value = std::vector<pb::metadata::Contributor>{
        {"A(author);B", pb::metadata::ContributorRole::Author}};
    auto two = metadataReport();
    two.result.contributors.value = std::vector<pb::metadata::Contributor>{
        {"A", pb::metadata::ContributorRole::Author}, {"B", pb::metadata::ContributorRole::Author}};
    QVERIFY(metadataSnapshot(one) != metadataSnapshot(two));

    // A line break in a title cannot forge another snapshot line.
    const auto base = fiveEntryReport();
    auto injected = base;
    injected.parsed->entries[0].title = "Chapter 0\nentry=\"e9\" order=9";
    const QString snapshot = analysisSnapshot(injected);
    QCOMPARE(snapshot.count(u'\n'), analysisSnapshot(base).count(u'\n'));
    QVERIFY(snapshot.contains(QStringLiteral("\u000a")));

    // Placeholder-like text is kept literally, not treated as a format string.
    auto percent = base;
    percent.parsed->entries[1].title = "%2 and %3";
    QVERIFY(analysisSnapshot(percent).contains(QStringLiteral("title=\"%2 and %3\"")));

    // An absent printed reference is not the literal text "null".
    auto printed = base;
    printed.parsed->entries[2].printed_reference = pb::parsing::PrintedReference{};
    printed.parsed->entries[2].printed_reference->literal = "null";
    QVERIFY(analysisSnapshot(printed) != analysisSnapshot(base));
}

// A real analysis of the scan-like fixture without models: the report is
// valid, but no page completes OCR, so an OCR-required check cannot pass.
void TestSdkSnapshot::withoutModelsOcrIsNotExercised()
{
    const QString pdf = QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QStringLiteral("image-only.pdf"));
    QVERIFY(QFile::exists(pdf));
    std::atomic_bool cancel{false};
    ProbeOptions options;
    options.useModels = false;
    const AnalysisProbe probe = probeAnalysis(pdf, &cancel, options);
    QVERIFY2(probe.ok, qPrintable(probe.error));
    QVERIFY(probe.modelIdentity.isEmpty() || probe.ocrPagesCompleted == 0);
    QCOMPARE(probe.ocrPagesCompleted, 0);
    QCOMPARE(mbl::reader::ocrWorkloadVerdict(true, probe.ocrPagesCompleted), mbl::reader::Verdict::NotExercised);
    qInfo().noquote() << "no-models outcome" << probe.outcome << "skipped" << probe.ocrPagesSkipped << "failed"
                      << probe.ocrPagesFailed << "model" << probe.modelIdentity;
}

QTEST_GUILESS_MAIN(TestSdkSnapshot)
#include "tst_sdksnapshot.moc"
