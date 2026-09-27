// A1: immutable SDK reports at reports/<run-id>.json (relative to the library
// root). A report is written once, before its run is published, and never
// overwritten.
#pragma once

#include "domain/ids.h"
#include "domain/result.h"
#include "storage/librarylayout.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace mbl::storage {

QString reportPath(const domain::RunId& run);  // Relative: reports/<run-id>.json

// Writes atomically (QSaveFile). Fails if the report already exists.
domain::Result<QString> writeReport(const LibraryLayout& layout, const domain::RunId& run, const QByteArray& json);

// Removes the report of a run that was not published. Returns false if absent.
// Startup recovery: removes reports/<uuid>.json files not in `referenced`
// (relative paths), i.e. reports written by a run that was never published
// because the process stopped first. Other files are left alone. Only safe
// while no job is running. Returns the removed relative paths.
QStringList removeUnreferencedReports(const LibraryLayout& layout, const QStringList& referenced);

bool removeUnpublishedReport(const LibraryLayout& layout, const domain::RunId& run);

} // namespace mbl::storage
