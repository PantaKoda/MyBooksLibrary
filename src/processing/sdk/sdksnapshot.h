// SDK boundary (A4), internal: canonical text of the observable results of a
// metadata or analysis report, so two runs can be compared semantically.
//
// Included: statuses and values of every bibliographic field with ordered
// contributors and alternatives; searched pages; analysis outcome, search
// pages, chosen candidate, every parsed entry (id, order, title, printed
// reference, hierarchy), every mapping (status, page, method, alternatives)
// and the plan (readiness, nodes, omissions, promotions, blockers).
// Encoding is unambiguous: strings are quoted and escaped (no separator or
// line break can appear unescaped), absent optionals are the bare word null,
// composites are bracketed, and document text is never used as a format
// string. Distinct results therefore never share a snapshot.
// Excluded as transient or diagnostic: diagnostics, stop reasons, reasons
// text, OCR attempt counters, acquired page text and timings.
#pragma once

#include <pdfbookmark/pdfbookmark.hpp>

#include <QString>

namespace mbl::sdk {

QString metadataSnapshot(const pdfbookmark::MetadataReport& report);
QString analysisSnapshot(const pdfbookmark::AnalysisReport& report);

// Lower-case hex SHA-256 of the UTF-8 snapshot.
QString snapshotDigest(const QString& snapshot);

} // namespace mbl::sdk
