// A2: a consistent copy of the catalog for a backup, and the files it
// references. A1 (storage/backup.*) copies those files and writes the backup.
#pragma once

#include "domain/result.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace mbl::catalog {

struct CatalogSnapshot {
    struct Source {
        QString managedPath;  // Relative to the library root.
        QString sha256;       // As recorded at import.
        qint64 size = 0;
    };
    int schemaVersion = 0;
    int books = 0;
    QList<Source> sources;   // Every asset, trashed books included.
    QStringList reports;     // Report paths of metadata and contents runs (relative).
};

// Writes the committed state of the catalog to `file` (which must not exist)
// with VACUUM INTO: one consistent copy, WAL content included, never a copy of
// the live file. The files are listed first, in the same database task, so no
// publication can come in between. Managed sources and reports are immutable
// and a referenced one is never removed, so they can be copied afterwards.
domain::Result<CatalogSnapshot> snapshotCatalog(QSqlDatabase& db, const QString& file);

} // namespace mbl::catalog
