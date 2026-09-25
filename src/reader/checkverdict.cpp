#include "reader/checkverdict.h"

namespace mbl::reader {

QString verdictName(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Pass: return QStringLiteral("PASS");
    case Verdict::Fail: return QStringLiteral("FAIL");
    case Verdict::NotExercised: return QStringLiteral("NOT_EXERCISED");
    }
    return QStringLiteral("FAIL");
}

Verdict cancellationVerdict(const CancellationObservation& o)
{
    if (!o.returned)
        return Verdict::Fail;  // Cancellation did not end the work.
    if (!o.requestMade || !o.workStartedBeforeRequest || !o.requestedWhileActive)
        return Verdict::NotExercised;  // No request during observed work.
    return o.cancellationObserved ? Verdict::Pass : Verdict::Fail;  // Completed despite the request.
}

Verdict ocrWorkloadVerdict(bool ocrRequired, int ocrPagesCompleted)
{
    if (!ocrRequired)
        return Verdict::Pass;
    return ocrPagesCompleted > 0 ? Verdict::Pass : Verdict::NotExercised;
}

} // namespace mbl::reader
