// Import operation contracts. An operation records one attempt to bring an
// external PDF into the library, so interrupted attempts can be recovered.
#pragma once

#include "domain/ids.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace mbl::domain {

// Copying:  staging in progress; bytes not yet verified.
// Verified: staged bytes verified; SHA-256 and asset ID reserved. The file is
//           in staging or already at its managed path, but not registered.
// Terminal: Registered, Duplicate, Failed, Cancelled, Abandoned.
enum class ImportPhase { Copying, Verified, Registered, Duplicate, Failed, Cancelled, Abandoned };

bool isTerminal(ImportPhase phase);
QString toCode(ImportPhase phase);
std::optional<ImportPhase> importPhaseFromCode(const QString& code);

struct ImportOperation {
    ImportId id;
    QString sourcePath;               // Provenance only; never written.
    QString sourceName;
    qint64 sourceSize = 0;            // Observed when the operation began.
    QDateTime sourceModified;         // Observed when the operation began (UTC).
    ImportPhase phase = ImportPhase::Copying;
    std::optional<QString> sha256;    // Once Verified.
    std::optional<qint64> byteSize;   // Once Verified.
    std::optional<AssetId> assetId;   // Reserved once Verified.
    std::optional<BookId> book;       // Registered book, or the existing book of a Duplicate.
    QString error;                    // Reason for Failed/Abandoned.
};

} // namespace mbl::domain
