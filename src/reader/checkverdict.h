// Verdict rules for the reader feasibility checks, kept free of Qt PDF and
// SDK types so they can be unit-tested with test doubles.
#pragma once

#include <QString>

namespace mbl::reader {

// NotExercised: the scenario the check exists for did not happen (for example
// OCR never completed, or the work finished before cancel could be requested).
// It is never a pass.
enum class Verdict { Pass, Fail, NotExercised };

QString verdictName(Verdict verdict);

// Cancellation must be requested while work is known to be active and then
// be observed as a cancellation.
struct CancellationObservation {
    bool returned = false;               // The worker call ended within the timeout.
    bool workStartedBeforeRequest = false; // A progress callback ran before the request.
    bool requestMade = false;            // The cancel flag was set.
    bool requestedWhileActive = false;   // The call had not returned when the flag was set.
    bool cancellationObserved = false;   // Cancelled error, or a report marked cancelled.
};
Verdict cancellationVerdict(const CancellationObservation& observation);

// An OCR-required scenario is exercised only if at least one page completed OCR.
Verdict ocrWorkloadVerdict(bool ocrRequired, int ocrPagesCompleted);

} // namespace mbl::reader
