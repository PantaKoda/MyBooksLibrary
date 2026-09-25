// Verdict rules of the reader feasibility checks, with test doubles for the
// worker behaviours the harness must not mistake for success.
#include "reader/checkverdict.h"

#include <QTest>

using namespace mbl::reader;

class TestCheckVerdict : public QObject {
    Q_OBJECT

private slots:
    void cancellation_data();
    void cancellation();
    void ocrWorkload();
};

void TestCheckVerdict::cancellation_data()
{
    QTest::addColumn<bool>("returned");
    QTest::addColumn<bool>("started");
    QTest::addColumn<bool>("requested");
    QTest::addColumn<bool>("whileActive");
    QTest::addColumn<bool>("observed");
    QTest::addColumn<int>("verdict");

    // A worker that honours the flag during observed work.
    QTest::newRow("honoured") << true << true << true << true << true << int(Verdict::Pass);
    // Ignore-cancel double: the request happened during work, but the call completed normally.
    QTest::newRow("ignore-cancel double") << true << true << true << true << false << int(Verdict::Fail);
    // The work finished before any request could be made.
    QTest::newRow("finished first") << true << false << false << false << false << int(Verdict::NotExercised);
    // A request made before work was observed to start proves nothing.
    QTest::newRow("requested before start") << true << false << true << true << true << int(Verdict::NotExercised);
    // The request raced with completion.
    QTest::newRow("requested after return") << true << true << true << false << false << int(Verdict::NotExercised);
    // The worker never returned.
    QTest::newRow("hung") << false << true << true << true << false << int(Verdict::Fail);
}

void TestCheckVerdict::cancellation()
{
    QFETCH(bool, returned);
    QFETCH(bool, started);
    QFETCH(bool, requested);
    QFETCH(bool, whileActive);
    QFETCH(bool, observed);
    QFETCH(int, verdict);
    CancellationObservation o;
    o.returned = returned;
    o.workStartedBeforeRequest = started;
    o.requestMade = requested;
    o.requestedWhileActive = whileActive;
    o.cancellationObserved = observed;
    QCOMPARE(int(cancellationVerdict(o)), verdict);
}

void TestCheckVerdict::ocrWorkload()
{
    QCOMPARE(ocrWorkloadVerdict(true, 0), Verdict::NotExercised);  // Missing models or failed OCR.
    QCOMPARE(ocrWorkloadVerdict(true, 3), Verdict::Pass);
    QCOMPARE(ocrWorkloadVerdict(false, 0), Verdict::Pass);         // Text-only checks need no OCR.
    QCOMPARE(verdictName(Verdict::NotExercised), QStringLiteral("NOT_EXERCISED"));
}

QTEST_GUILESS_MAIN(TestCheckVerdict)
#include "tst_checkverdict.moc"
