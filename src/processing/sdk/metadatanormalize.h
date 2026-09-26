// SDK boundary (A4), internal: converts the SDK's metadata result into the
// application's contracts. Statuses are preserved exactly: values only for
// Resolved fields, never an ambiguous candidate promoted to a value; ordered
// contributors with roles; publication and copyright years kept apart.
#pragma once

#include "domain/metadata.h"

#include <pdfbookmark/pdfbookmark.hpp>

#include <QList>

namespace mbl::sdk {

domain::ExtractedMetadata normalizeMetadata(const pdfbookmark::metadata::MetadataResult& result,
                                            QList<domain::MetadataFieldDetail>* details = nullptr);

} // namespace mbl::sdk
