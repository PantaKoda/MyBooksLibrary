// A1: backups of a library and their restoration (M10).
//
// A backup is a folder "MyBooksLibrary backup <date> <time>" holding:
//   library.sqlite                a consistent copy of the catalog (VACUUM INTO)
//   files/<asset-id>/source.pdf   every managed source, trashed books included
//   reports/<run-id>.json         every report a run references
//   backup.json                   the manifest: format, schema, and each file
//                                 with its size and SHA-256
// It is written into a hidden ".partial" folder next to it, verified (every
// digest, and the catalog's integrity check), and only then renamed into
// place, so an incomplete backup never looks like one. derivatives/, cache/
// and staging/ are not included: nothing in them is needed to restore.
//
// Restoring never touches the library in use. It verifies the backup, fills
// a new folder (again through a hidden staging folder), opens it as a
// library (migrating an older catalog) and checks it.
//
// All functions block: call them on a worker thread.
#pragma once

#include "domain/result.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace mbl::catalog {
class Library;
}

namespace mbl::storage {

struct BackupInfo {
    QString folder;          // The backup (absolute).
    QDateTime createdAt;     // UTC.
    int schemaVersion = 0;
    int books = 0;
    int sources = 0;
    int reports = 0;
    qint64 bytes = 0;        // Catalog and files.
    // Reports a run referenced but that were not in the library folder: a
    // backup does not fail for missing diagnostics, and says so.
    QStringList missingReports;
};

// Files done and total (catalog included), for progress.
using BackupProgress = std::function<void(int done, int total)>;

// Backs up the open `library` into a new folder inside `parentFolder`, which
// must exist and lie outside the library folder. A managed source whose bytes
// no longer match its recorded digest fails the backup (never a silent copy of
// damage). Cancelled (InvalidArgument, "cancelled") or failed backups leave
// nothing behind.
domain::Result<BackupInfo> createBackup(catalog::Library& library, const QString& parentFolder,
                                        const std::atomic_bool* cancel = nullptr, const BackupProgress& progress = {});

// Reads the manifest and checks every file's size and digest, the catalog's
// integrity, and that the backup holds every file the catalog needs (each
// source with the digest the catalog records; each report, or it is listed
// as missing). Fails with InvalidArgument and the first problem.
domain::Result<BackupInfo> verifyBackup(const QString& backupFolder, const std::atomic_bool* cancel = nullptr);

struct RestoreInfo {
    QString libraryFolder;   // The restored library (absolute).
    BackupInfo backup;
    int schemaVersion = 0;   // After opening (an older catalog is migrated).
    int exportsClosed = 0;   // Waiting exports closed as not written.
};

// Restores the backup as a new library at `targetFolder`, which must not
// exist or be an empty folder, must not be inside the backup, and must be
// neither inside nor around any of `librariesInUse` (the root of each open
// library: its start-up recovery could remove, or mix in, what is restored
// there). The backup is verified first; nothing is written if it fails. On
// failure or cancel the target is left as it was. Exports that were waiting
// in the backup are closed as not written (catalog::closeExportsAfterRestore).
domain::Result<RestoreInfo> restoreBackup(const QString& backupFolder, const QString& targetFolder,
                                          const QStringList& librariesInUse, const std::atomic_bool* cancel = nullptr,
                                          const BackupProgress& progress = {});

} // namespace mbl::storage
