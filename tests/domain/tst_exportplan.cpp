// Export plans from a book's effective contents: which entries become
// bookmarks, and every choice that makes the bookmarks differ from the
// contents is recorded (omissions, promotions), never silent.
#include "domain/export.h"

#include <QTest>

using namespace mbl::domain;

namespace {

TocEntry entry(const QString& id, int order, const QString& title, std::optional<int> page,
               std::optional<QString> parent = std::nullopt)
{
    TocEntry e;
    e.sdkEntryId = id;
    e.order = order;
    e.title = title;
    e.hierarchy = parent ? HierarchyState::KnownParent : HierarchyState::Root;
    e.parentSdkEntryId = parent;
    if (page) {
        e.destinationState = DestinationState::Resolved;
        e.destinationPage = page;
    }
    return e;
}

AssetRecord asset(std::optional<int> pages = 30)
{
    AssetRecord a;
    a.sha256 = QString(64, u'a');
    a.pageCount = pages;
    return a;
}

TocAnalysis contents(const QList<TocEntry>& entries)
{
    TocAnalysis t;
    t.entries = entries;
    return t;
}

} // namespace

class TestExportPlan : public QObject {
    Q_OBJECT

private slots:
    void resolvedEntriesBecomeBookmarks();
    void omissionsAndPromotionsAreRecorded();
    void unknownLevelsAndRemovedEntries();
    void refusals();
};

void TestExportPlan::resolvedEntriesBecomeBookmarks()
{
    // A parent listed after its child; page index 0 is valid.
    auto plan = buildExportPlan(contents({entry(QStringLiteral("c"), 0, QStringLiteral("Child"), 5, QStringLiteral("p")),
                                          entry(QStringLiteral("p"), 1, QStringLiteral(" Parent "), 0)}),
                                asset());
    QVERIFY(plan);
    const ExportPlan& p = plan.value();
    QCOMPARE(p.sourceSha256, QString(64, u'a'));
    QCOMPARE(p.pageCount, 30);
    QCOMPARE(p.nodes.size(), 2);
    QCOMPARE(p.nodes.at(0).id, QStringLiteral("c"));
    QCOMPARE(p.nodes.at(0).parentId, std::optional<QString>(QStringLiteral("p")));
    QCOMPARE(p.nodes.at(1).title, QStringLiteral("Parent"));  // Trimmed.
    QCOMPARE(p.nodes.at(1).page, 0);
    QVERIFY(p.complete());
}

void TestExportPlan::omissionsAndPromotionsAreRecorded()
{
    TocEntry ambiguous = entry(QStringLiteral("a"), 1, QStringLiteral("Ambiguous"), std::nullopt);
    ambiguous.destinationState = DestinationState::Ambiguous;
    auto plan = buildExportPlan(
        contents({entry(QStringLiteral("r"), 0, QStringLiteral("Part I"), 1),
                  ambiguous,
                  entry(QStringLiteral("u"), 2, QStringLiteral("Chapter without page"), std::nullopt, QStringLiteral("r")),
                  entry(QStringLiteral("s"), 3, QStringLiteral("Section"), 9, QStringLiteral("u")),
                  entry(QStringLiteral("t"), 4, QStringLiteral("Under ambiguous"), 10, QStringLiteral("a"))}),
        asset());
    QVERIFY(plan);
    const ExportPlan& p = plan.value();
    QVERIFY(!p.complete());
    QCOMPARE(p.omitted.size(), 2);
    QCOMPARE(p.omitted.at(0).entryId, QStringLiteral("a"));
    QCOMPARE(p.omitted.at(0).reason, QStringLiteral("More than one possible page; none was chosen."));
    QCOMPARE(p.omitted.at(1).reason, QStringLiteral("No page was found for it."));
    QCOMPARE(p.nodes.size(), 3);
    // "Section" goes to the nearest kept ancestor, "Part I"; "Under ambiguous" to the top level.
    QCOMPARE(p.nodes.at(1).id, QStringLiteral("s"));
    QCOMPARE(p.nodes.at(1).parentId, std::optional<QString>(QStringLiteral("r")));
    QCOMPARE(p.nodes.at(2).parentId, std::optional<QString>());
    QCOMPARE(p.promotions.size(), 2);
    QCOMPARE(p.promotions.at(0).nodeId, QStringLiteral("s"));
    QCOMPARE(p.promotions.at(0).originalParentId, std::optional<QString>(QStringLiteral("u")));
    QCOMPARE(p.promotions.at(0).newParentId, std::optional<QString>(QStringLiteral("r")));
    QCOMPARE(p.promotions.at(0).reason, QStringLiteral("“Chapter without page” has no bookmark; it is placed under “Part I”."));
    QCOMPARE(p.promotions.at(1).reason, QStringLiteral("“Ambiguous” has no bookmark; it is placed at the top level."));
}

void TestExportPlan::unknownLevelsAndRemovedEntries()
{
    TocEntry unknown = entry(QStringLiteral("k"), 1, QStringLiteral("Level unknown"), 4);
    unknown.hierarchy = HierarchyState::Unknown;
    TocEntry removed = entry(QStringLiteral("x"), 2, QStringLiteral("Noise"), 6);
    removed.removed = true;
    TocEntry removedChild = entry(QStringLiteral("y"), 3, QStringLiteral("Under noise"), 7, QStringLiteral("x"));
    auto plan = buildExportPlan(contents({entry(QStringLiteral("r"), 0, QStringLiteral("Intro"), 2), unknown, removed,
                                          removedChild}),
                                asset());
    QVERIFY(plan);
    const ExportPlan& p = plan.value();
    QCOMPARE(p.removedByUser, 1);
    QVERIFY(p.omitted.isEmpty());  // Removal is the user's choice, not missing coverage.
    QCOMPARE(p.nodes.size(), 3);
    // An uncertain level is not a move from a parent: listed on its own.
    QCOMPARE(p.uncertainLevels.size(), 1);
    QCOMPARE(p.uncertainLevels.at(0).nodeId, QStringLiteral("k"));
    QCOMPARE(p.uncertainLevels.at(0).reason, QStringLiteral("Its level in the contents is uncertain; it is placed at the top level."));
    QCOMPARE(p.nodes.at(1).parentId, std::optional<QString>());
    QCOMPARE(p.promotions.size(), 1);
    QCOMPARE(p.promotions.at(0).nodeId, QStringLiteral("y"));
    QVERIFY(p.promotions.at(0).originalParentId != p.promotions.at(0).newParentId);
    QVERIFY(!p.complete());

    // Only an uncertain level: still not complete.
    auto onlyUnknown = buildExportPlan(contents({entry(QStringLiteral("r"), 0, QStringLiteral("Intro"), 2), unknown}), asset());
    QVERIFY(onlyUnknown);
    QVERIFY(onlyUnknown.value().promotions.isEmpty());
    QVERIFY(!onlyUnknown.value().complete());
}

void TestExportPlan::refusals()
{
    const auto one = contents({entry(QStringLiteral("r"), 0, QStringLiteral("Intro"), 2)});
    QCOMPARE(buildExportPlan(one, asset(std::nullopt)).error().code, ErrorCode::InvalidArgument);  // Page count unknown.
    QCOMPARE(buildExportPlan(contents({entry(QStringLiteral("r"), 0, QStringLiteral("Intro"), 30)}), asset(30)).error().code,
             ErrorCode::InvalidArgument);  // Outside a 30-page book.
    QCOMPARE(buildExportPlan(contents({entry(QStringLiteral("r"), 0, QStringLiteral("Intro"), std::nullopt)}), asset())
                 .error()
                 .code,
             ErrorCode::InvalidArgument);  // Nothing to bookmark.
    QCOMPARE(buildExportPlan(contents({}), asset()).error().code, ErrorCode::InvalidArgument);
}

QTEST_GUILESS_MAIN(TestExportPlan)
#include "tst_exportplan.moc"
