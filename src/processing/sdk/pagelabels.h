// SDK boundary (A4), internal: numbering sections from a PDF's page labels.
//
// The SDK maps a printed page number to a physical page with one numbering
// section by default: the decimal pages after the TOC, with one offset between
// printed and physical pages. A publisher's PDF often leaves out the blank
// pages of the printed book, so its printed numbers skip ahead at chapter
// ends and the pages disagree about that offset: the SDK then places no entry.
// The PDF's page labels ("1", "2", ... "xii") often record the skips. From
// them the contents analyzer supplies one numbering section per run of labels
// (AnalysisOptions::sections) and associates each entry with the run holding
// its printed page (entry_sections), then keeps the second analysis only if it
// places more entries. The labels only divide the pages into sections; the SDK
// still confirms each page from what is printed on it.
#pragma once

#include <pdfbookmark/pdfbookmark.hpp>

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace mbl::sdk {

// Physical pages whose labels count up by one in one style: "7", "8", "9",
// or "xi", "xii", or "A-1", "A-2" (1 to 3 letters, a hyphen and digits), the
// forms the SDK reads as printed page numbers. Other labels ("Cover", "C1")
// belong to no run.
struct PageLabelRun {
    enum class Style { Decimal, Roman, Prefixed };
    Style style = Style::Decimal;
    QString prefix;           // Upper case; only for Prefixed.
    int first = 0;            // Zero-based physical page.
    int end = 0;              // Exclusive.
    quint32 firstNumber = 0;  // The number printed on `first`.

    friend bool operator==(const PageLabelRun& a, const PageLabelRun& b)
    {
        return a.style == b.style && a.prefix == b.prefix && a.first == b.first && a.end == b.end
               && a.firstNumber == b.firstNumber;
    }
};

// Runs of `labels` (one per physical page, in order).
QList<PageLabelRun> pageLabelRuns(const QStringList& labels);

// True when a style has more than one run: its numbering skips or restarts,
// so one offset between printed and physical pages cannot hold for it.
bool numberingBreaks(const QList<PageLabelRun>& runs);

// The run holding printed page `number` of `style`/`prefix` (compared without
// case), or nullopt when none or more than one does.
std::optional<qsizetype> runHolding(const QList<PageLabelRun>& runs, PageLabelRun::Style style,
                                    const QString& prefix, quint32 number);

// The label of every page, read with Qt PDF, which gives "n" for page n - 1
// when the PDF has no labels. Empty if the file cannot be read.
QStringList readPageLabels(const QString& pdfPath);

// The options of a second analysis after `first`: `options` with a numbering
// section per label run and the entries associated with their runs. nullopt
// when that cannot place anything more: every entry has a page, the labels
// (one per page) show no break, or no entry without a page has its printed
// page in exactly one run.
std::optional<pdfbookmark::AnalysisOptions> pageLabelAnalysisOptions(const QStringList& labels,
                                                                     const pdfbookmark::AnalysisReport& first,
                                                                     const pdfbookmark::AnalysisOptions& options);

// True when `second` parsed the same entries as `first` (ID, title, printed
// page) and gives more of them a page.
bool placesMoreEntries(const pdfbookmark::AnalysisReport& second, const pdfbookmark::AnalysisReport& first);

} // namespace mbl::sdk
