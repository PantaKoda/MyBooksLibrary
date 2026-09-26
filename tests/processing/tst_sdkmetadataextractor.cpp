// SDK boundary: normalization of the SDK's metadata result, the real
// extractor on the fixtures, and one end-to-end job through the coordinator.
#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "processing/processingcoordinator.h"
#include "processing/sdk/metadatanormalize.h"
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
using mbl::processing::MetadataExtraction;
namespace pm = pdfbookmark::metadata;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

} // namespace

class TestSdkMetadataExtractor : public QObject {
    Q_OBJECT

private slots:
    void normalizationKeepsStatusesAndOrder();
    void extractsTheFixtureTitle();
    void cancelledBeforeStart();
    void withoutModelsStillCompletes();
    void endToEndJobPublishes();
};

void TestSdkMetadataExtractor::normalizationKeepsStatusesAndOrder()
{
    pm::MetadataResult r;
    // An ambiguous title with a value set must not become a value.
    r.title.status = pm::FieldStatus::Ambiguous;
    r.title.value = pm::TitleValue{"Maybe", std::nullopt};
    pm::Candidate<pm::TitleValue> alt;
    alt.value = pm::TitleValue{"Option B", std::string("Sub")};
    alt.score = 0.5;
    r.title.alternatives.push_back(alt);
    r.title.reasons.push_back("two candidates of equal prominence");

    r.contributors.status = pm::FieldStatus::Resolved;
    r.contributors.value = std::vector<pm::Contributor>{{"Zed", pm::ContributorRole::Editor},
                                                        {"Amy", pm::ContributorRole::Author}};
    r.publication_year.status = pm::FieldStatus::NotFoundInSearch;
    r.copyright_year.status = pm::FieldStatus::Resolved;
    r.copyright_year.value = pm::YearValue{2019, pm::YearKind::Copyright, "(c) 2019"};

    QList<MetadataFieldDetail> details;
    const ExtractedMetadata m = mbl::sdk::normalizeMetadata(r, &details);
    QCOMPARE(m.titleStatus, FieldStatus::Ambiguous);
    QVERIFY(!m.title);  // Never promoted.
    QCOMPARE(m.contributors, (QList<Contributor>{{QStringLiteral("Zed"), ContributorRole::Editor},
                                                 {QStringLiteral("Amy"), ContributorRole::Author}}));  // Order kept.
    QCOMPARE(m.publicationYearStatus, FieldStatus::NotFoundInSearch);
    QVERIFY(!m.publicationYear);
    QCOMPARE(*m.copyrightYear, 2019);  // Not used as a publication year.
    QCOMPARE(details.size(), 5);
    QCOMPARE(details.first().field, MetadataField::Title);
    QCOMPARE(details.first().alternatives.first().value, QStringLiteral("Option B: Sub"));
    QCOMPARE(details.first().reasons.first(), QStringLiteral("two candidates of equal prominence"));
}

void TestSdkMetadataExtractor::extractsTheFixtureTitle()
{
    mbl::sdk::SdkMetadataExtractor extractor;
    std::atomic_bool cancel{false};
    const MetadataExtraction r = extractor.extract(fixture("title-page.pdf"), cancel);
    QCOMPARE(r.status, MetadataExtraction::Status::Completed);
    QCOMPARE(r.metadata.titleStatus, FieldStatus::Resolved);
    QCOMPARE(*r.metadata.title, QStringLiteral("Practical Library Engineering"));
    QCOMPARE(r.sourceSha256, mbl::storage::sha256OfFile(fixture("title-page.pdf")).value());
    QCOMPARE(r.pageCount, 3);
    QCOMPARE(r.sdkVersion, QStringLiteral(PDFBOOKMARK_VERSION_STRING));
    const QJsonObject report = QJsonDocument::fromJson(r.reportJson).object();
    QCOMPARE(report.value(QStringLiteral("kind")).toString(), QStringLiteral("pdfbookmark.metadata"));
    QVERIFY(QJsonDocument::fromJson(r.optionsJson.toUtf8()).object().value(QStringLiteral("models")).toBool());
    QCOMPARE(r.details.size(), 5);
}

void TestSdkMetadataExtractor::cancelledBeforeStart()
{
    mbl::sdk::SdkMetadataExtractor extractor;
    std::atomic_bool cancel{true};
    const MetadataExtraction r = extractor.extract(fixture("contents-book.pdf"), cancel);
    QCOMPARE(r.status, MetadataExtraction::Status::Cancelled);
}

void TestSdkMetadataExtractor::withoutModelsStillCompletes()
{
    mbl::sdk::SdkMetadataExtractor extractor({false, 0});
    std::atomic_bool cancel{false};
    const MetadataExtraction r = extractor.extract(fixture("image-only.pdf"), cancel);
    QCOMPARE(r.status, MetadataExtraction::Status::Completed);  // OCR unavailable is not a failure...
    QVERIFY(r.metadata.titleStatus != FieldStatus::Resolved);   // ...but nothing is read from scans.
    QVERIFY(!QJsonDocument::fromJson(r.optionsJson.toUtf8()).object().value(QStringLiteral("models")).toBool());
}

void TestSdkMetadataExtractor::endToEndJobPublishes()
{
    QTemporaryDir root;
    auto library = mbl::catalog::Library::open(root.path());
    QVERIFY(library);
    mbl::storage::ImportService importer(*library.value());
    const auto imported = importer.importFile(fixture("title-page.pdf"));
    QVERIFY(imported.book);
    mbl::processing::ProcessingCoordinator c(*library.value(), std::make_shared<mbl::sdk::SdkMetadataExtractor>());
    QSignalSpy published(&c, &mbl::processing::ProcessingCoordinator::metadataPublished);
    c.enqueueMetadata(*imported.book);
    QTRY_COMPARE_WITH_TIMEOUT(published.size(), 1, 60000);
    auto details = library.value()->run([book = *imported.book](QSqlDatabase& db) {
        return mbl::catalog::bookDetails(db, book);
    }).result();
    QVERIFY(details);
    QCOMPARE(*details.value().summary.metadata.title, QStringLiteral("Practical Library Engineering"));
    QCOMPARE(details.value().summary.displayTitleFromFileName, false);
    QCOMPARE(details.value().asset.pageCount, 3);
}

QTEST_GUILESS_MAIN(TestSdkMetadataExtractor)
#include "tst_sdkmetadataextractor.moc"
