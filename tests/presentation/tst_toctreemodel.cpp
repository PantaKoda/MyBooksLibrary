// Presentation: the contents tree. Built in two passes (a parent may come
// after its child); invalid or cyclic parents never produce an invented
// parent or an infinite tree; physical pages are shown as index + 1.
#include "presentation/toctreemodel.h"

#include <QAbstractItemModelTester>
#include <QTest>

using namespace mbl::domain;
using mbl::presentation::TocTreeModel;

namespace {

TocEntry entry(const char* id, int order, const char* title, HierarchyState h = HierarchyState::Root,
               const char* parent = nullptr)
{
    TocEntry e;
    e.sdkEntryId = QString::fromLatin1(id);
    e.order = order;
    e.title = QString::fromUtf8(title);
    e.hierarchy = h;
    if (parent)
        e.parentSdkEntryId = QString::fromLatin1(parent);
    e.destinationState = DestinationState::Resolved;
    e.destinationPage = order;
    e.inExportPlan = true;
    return e;
}

QString titleAt(const TocTreeModel& m, const QModelIndex& index)
{
    return m.data(index, TocTreeModel::TitleRole).toString();
}

// "title(child, child)" for the whole tree, in order.
QString shape(const TocTreeModel& m, const QModelIndex& parent = {})
{
    QStringList parts;
    for (int row = 0; row < m.rowCount(parent); ++row) {
        const QModelIndex i = m.index(row, 0, parent);
        const QString children = shape(m, i);
        parts << (children.isEmpty() ? titleAt(m, i) : titleAt(m, i) + QStringLiteral("(") + children + u')');
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

class TestTocTreeModel : public QObject {
    Q_OBJECT

private slots:
    void parentListedAfterChild();
    void invalidParentsStayAtTopLevel();
    void pageTextsUsePhysicalPages();
    void detailsExplainInPlainLanguage();
};

void TestTocTreeModel::parentListedAfterChild()
{
    TocTreeModel m;
    QAbstractItemModelTester tester(&m);
    m.setEntries({entry("c", 0, "1.1 Child", HierarchyState::KnownParent, "p"), entry("p", 1, "1 Parent"),
                  entry("d", 2, "1.1.1 Grandchild", HierarchyState::KnownParent, "c"), entry("q", 3, "2 Next")});
    QCOMPARE(shape(m), QStringLiteral("1 Parent(1.1 Child(1.1.1 Grandchild)), 2 Next"));
    QCOMPARE(m.entryCount(), 4);
    const QModelIndex child = m.index(0, 0, m.index(0, 0));
    QCOMPARE(m.parent(child), m.index(0, 0));
    QVERIFY(!m.data(child, TocTreeModel::UncertainRole).toBool());
}

void TestTocTreeModel::invalidParentsStayAtTopLevel()
{
    TocTreeModel m;
    QAbstractItemModelTester tester(&m);
    m.setEntries({
        entry("self", 0, "Self", HierarchyState::KnownParent, "self"),
        entry("a", 1, "A", HierarchyState::KnownParent, "b"),
        entry("b", 2, "B", HierarchyState::KnownParent, "a"),
        entry("under", 3, "Under A", HierarchyState::KnownParent, "a"),  // Below a cycle member: stays attached.
        entry("lost", 4, "Lost", HierarchyState::KnownParent, "missing"),
        entry("unknown", 5, "Unknown level", HierarchyState::Unknown),
    });
    QCOMPARE(shape(m), QStringLiteral("Self, A(Under A), B, Lost, Unknown level"));
    for (int row : {0, 1, 2, 3}) {
        const QModelIndex i = m.index(row, 0);
        QVERIFY2(m.data(i, TocTreeModel::StateTextRole).toString().contains(QStringLiteral("parent not found")),
                 qPrintable(titleAt(m, i)));
        QVERIFY(m.data(i, TocTreeModel::UncertainRole).toBool());
    }
    const QModelIndex unknown = m.index(4, 0);
    QVERIFY(m.data(unknown, TocTreeModel::StateTextRole).toString().contains(QStringLiteral("level uncertain")));
    QVERIFY(!m.data(unknown, TocTreeModel::StateTextRole).toString().contains(QStringLiteral("parent not found")));
}

void TestTocTreeModel::pageTextsUsePhysicalPages()
{
    TocEntry first = entry("a", 0, "Preface");  // Page index 0 is a valid destination.
    first.printedLabel = QStringLiteral("iv");
    TocEntry ambiguous = entry("b", 1, "Appendix");
    ambiguous.destinationState = DestinationState::Ambiguous;
    ambiguous.destinationPage.reset();
    ambiguous.evidence.alternativePages = {4, 9};
    TocEntry missing = entry("c", 2, "Index");
    missing.destinationState = DestinationState::Unresolved;
    missing.destinationPage.reset();
    missing.sourceTocPage = 1;
    TocEntry three = entry("d", 3, "Glossary");
    three.destinationState = DestinationState::Ambiguous;
    three.destinationPage.reset();
    three.evidence.alternativePages = {4, 9, 11};
    TocEntry many = entry("e", 4, "Problems");  // A page count that disagrees all through the book.
    many.destinationState = DestinationState::Ambiguous;
    many.destinationPage.reset();
    for (int p = 20; p < 32; ++p)
        many.evidence.alternativePages << p;
    TocTreeModel m;
    m.setEntries({first, ambiguous, missing, three, many});

    QCOMPARE(m.data(m.index(0, 0), TocTreeModel::PageTextRole).toString(), QStringLiteral("Page 1"));
    QCOMPARE(m.data(m.index(0, 0), TocTreeModel::PageRole).toInt(), 1);
    QCOMPARE(m.data(m.index(0, 0), TocTreeModel::PrintedLabelRole).toString(), QStringLiteral("iv"));
    QCOMPARE(m.data(m.index(1, 0), TocTreeModel::PageTextRole).toString(), QStringLiteral("Page 5 or 10?"));
    QCOMPARE(m.data(m.index(1, 0), TocTreeModel::PageRole).toInt(), -1);  // No page is guessed.
    QCOMPARE(m.data(m.index(2, 0), TocTreeModel::PageTextRole).toString(), QStringLiteral("Page not found"));
    QCOMPARE(m.data(m.index(2, 0), TocTreeModel::PageRole).toInt(), -1);
    QCOMPARE(m.data(m.index(2, 0), TocTreeModel::SourcePageRole).toInt(), 2);  // Where to look instead.

    // A few possible pages are listed; many are counted, so the row keeps its
    // title, and listed in the details.
    QCOMPARE(m.data(m.index(3, 0), TocTreeModel::PageTextRole).toString(), QStringLiteral("Page 5 or 10 or 12?"));
    QVERIFY(!m.data(m.index(3, 0), TocTreeModel::DetailRole).toString().contains(QStringLiteral("Possible pages")));
    QCOMPARE(m.data(m.index(4, 0), TocTreeModel::PageTextRole).toString(),
             QStringLiteral("Page uncertain (12 possible)"));
    QCOMPARE(m.data(m.index(4, 0), TocTreeModel::PageRole).toInt(), -1);
    QVERIFY(m.data(m.index(4, 0), TocTreeModel::DetailRole)
                .toString()
                .contains(QStringLiteral("Possible pages: 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 and 2 more")));
}

void TestTocTreeModel::detailsExplainInPlainLanguage()
{
    TocEntry e = entry("x", 0, "Index");
    e.destinationState = DestinationState::Unresolved;
    e.destinationPage.reset();
    e.printedLabel = QStringLiteral("xii");
    e.evidence.printedLabelUncertain = true;
    e.sourceTocPage = 2;
    e.inExportPlan = false;
    e.evidence.omissionReason = QStringLiteral("unresolved destination");
    e.evidence.destinationReasons << QStringLiteral("no page shows the label xii");
    e.evidence.destinationMethod = QStringLiteral("inferred_offset");
    e.evidence.diagnostics << QStringLiteral("footer band empty");
    TocTreeModel m;
    m.setEntries({e});
    const QVariantMap details = m.entryAt(m.index(0, 0));
    const QString detail = details.value(QStringLiteral("detail")).toString();
    QVERIFY(detail.contains(QStringLiteral("Printed page number “xii” (uncertain)")));
    QVERIFY(detail.contains(QStringLiteral("Listed on page 3 of the PDF")));
    QVERIFY(detail.contains(QStringLiteral("no page shows the label xii")));
    QVERIFY(detail.contains(QStringLiteral("Left out of the bookmarks: unresolved destination")));
    QCOMPARE(details.value(QStringLiteral("stateText")).toString(),
             QStringLiteral("page not found · not in the bookmarks"));
    const QString technical = details.value(QStringLiteral("technical")).toString();
    QVERIFY(technical.contains(QStringLiteral("method=inferred_offset")));
    QVERIFY(technical.contains(QStringLiteral("footer band empty")));
    QVERIFY(m.entryAt(QModelIndex()).isEmpty());
}

QTEST_GUILESS_MAIN(TestTocTreeModel)
#include "tst_toctreemodel.moc"
