// M01 feasibility harness for the reader: Qt PDF availability and its
// coexistence with SDK work in one process. Both embed PDFium (Qt statically
// in Qt6Pdf.dll, the SDK as pdfium.dll), and the SDK's internal
// serialisation does not cover Qt PDF, so this is checked by running them
// concurrently, not inferred from headers.
#pragma once

#include <QString>
#include <QTextStream>

namespace mbl::reader {

struct ReaderCheckOptions {
    QString pdfPath;       // Local path of a readable PDF.
    int rounds = 3;        // SDK iterations run concurrently with viewing.
    bool viewDuringRounds = true;  // false: repeat the SDK rounds without viewing (isolation run).
    int timeoutSeconds = 300;
};

// Runs every check, prints "check.<name>=PASS|FAIL ..." lines and returns
// the number of failed checks. Requires a QGuiApplication; call from its
// thread (the GUI thread).
int runReaderCheck(const ReaderCheckOptions& options, QTextStream& out);

} // namespace mbl::reader
