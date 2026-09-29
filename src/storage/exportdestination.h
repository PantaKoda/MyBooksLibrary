// A1: where an exported (bookmarked) PDF copy may be written. The SDK only
// refuses to overwrite its own input; the library protects much more:
// nothing inside the library folder (catalog, managed sources, reports,
// staging), no managed source anywhere under another name (another case,
// "..", a short name, a hard link), and no external original the user
// imported. An existing file is kept unless replacing it was asked for.
#pragma once

#include "domain/export.h"
#include "domain/result.h"
#include "storage/librarylayout.h"

#include <QString>
#include <QStringList>

namespace mbl::storage {

struct ExportDestinationRules {
    // Absolute paths of files that must never be written: the managed
    // sources and the imported originals (provenance).
    QStringList protectedFiles;
    bool replaceExisting = false;
};

// The absolute, cleaned destination if it may be written. Refused with
// InvalidArgument (not a .pdf, no folder, inside the library, a protected
// file or an alias of one) or Duplicate (it exists and replacing was not
// asked for).
domain::Result<QString> validateExportDestination(const QString& destination, const LibraryLayout& layout,
                                                  const ExportDestinationRules& rules);

// The size and modification time of the file at `path`; nullopt when there
// is no file (nothing, or a folder).
std::optional<domain::FileIdentity> fileIdentity(const QString& path);

// "<title> (bookmarked).pdf" in `folder`, with characters that file names
// cannot hold replaced, and " (2)", " (3)"... added if the name is taken.
QString suggestedExportPath(const QString& title, const QString& folder);

} // namespace mbl::storage
