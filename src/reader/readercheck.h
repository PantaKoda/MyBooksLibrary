// M01 feasibility harness for the reader: Qt PDF availability and its
// coexistence with SDK work in one process. Both embed PDFium (Qt statically
// in Qt6Pdf.dll, the SDK as pdfium.dll), and the SDK's internal
// serialisation does not cover Qt PDF, so this is checked by running them
// concurrently, not inferred from headers.
#pragma once

#include <QString>
#include <QTextStream>

namespace mbl::reader {

enum class ViewMode {
    Churn,       // Each GUI turn loads, renders every page of, and closes a new QPdfDocument.
    Persistent,  // One open QPdfDocument renders the next page each GUI turn.
    None,        // No viewing while the SDK rounds run (isolation).
};

struct ReaderCheckOptions {
    QString pdfPath;          // Local path of a readable PDF.
    int rounds = 3;           // SDK rounds run concurrently with viewing.
    ViewMode view = ViewMode::Churn;
    bool requireOcr = false;  // OCR must complete, or SDK checks are NOT_EXERCISED.
    bool useModels = true;    // false: run the SDK without OCR models.
    bool qtControl = true;    // Run the Qt-only control for the SDK baseline's duration.
    int ocrThreads = 0;       // SDK OCR threads; 0 = automatic.
    int qmlCycles = 1;        // Create/destroy cycles of the QtQuick.Pdf view.
    bool qmlNaiveTeardown = false; // Destroy the document with the view still active (diagnostic).
    int timeoutSeconds = 900;
};

// Runs the checks, prints "check.<name>=PASS|FAIL|NOT_EXERCISED ..." lines
// (plus "sample." memory lines during long phases) and returns the number of
// checks that did not pass. Requires a QGuiApplication; call from its thread.
int runReaderCheck(const ReaderCheckOptions& options, QTextStream& out);

} // namespace mbl::reader
