// SDK boundary (A4), internal: small conversions shared by the SDK adapters.
#pragma once

#include "processing/metadataextractor.h"

#include <pdfbookmark/pdfbookmark.hpp>

#include <QString>

#include <filesystem>

namespace mbl::sdk {

// UTF-16 on Windows, explicit UTF-8 elsewhere; never the local 8-bit codepage.
std::filesystem::path toSdkPath(const QString& localPath);

// Lower-case hex of an SDK input digest.
QString sha256Hex(const pdfbookmark::InputIdentity& input);

// A completed (or cancelled) metadata report as the application's extraction
// result, with the report JSON kept for the run.
processing::MetadataExtraction metadataExtraction(const pdfbookmark::MetadataReport& report,
                                                  const QString& optionsJson);

} // namespace mbl::sdk
