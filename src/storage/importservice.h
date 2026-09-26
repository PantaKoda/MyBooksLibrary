// A1: imports external PDFs as verified managed copies and recovers
// interrupted imports. The external original is only ever read.
//
// Protocol for one file (docs/STORAGE.md):
//   1. record the operation (Copying);
//   2. stream-copy and hash into staging/<import>/, check the source did not
//      change, verify the staged bytes;
//   3. if the SHA-256 is already catalogued, close as Duplicate and remove
//      staging; otherwise record Verified with a reserved asset ID;
//   4. install by a strict same-volume move (commitMove: never a copy, never
//      a replacement) to files/<asset>/source.pdf;
//   5. register asset, book, search projection and the operation's
//      Registered phase in one catalog transaction.
// recover() resumes or closes every operation left open by a crash.
#pragma once

#include "domain/ids.h"
#include "storage/librarylayout.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <optional>

namespace mbl::catalog {
class Library;
}

namespace mbl::storage {

// Points at which tests can simulate a crash.
enum class ImportStage {
    Begun,             // Operation recorded, nothing copied.
    Staged,            // Staged copy verified, not yet recorded as Verified.
    VerifiedRecorded,  // Recorded as Verified; file still in staging.
    Installed,         // Renamed to its managed path; not yet registered.
};

struct ImportResult {
    enum class Outcome {
        Imported,          // New book registered.
        Duplicate,         // Same bytes already in the library; `book` is the existing book.
        DuplicateInTrash,  // Same bytes in a trashed book; `book` can be restored (not done here).
        Failed,
        Cancelled,
        Interrupted,       // Test crash hook stopped the import; left for recover().
    };
    Outcome outcome = Outcome::Failed;
    QString sourcePath;
    std::optional<domain::ImportId> operation;
    std::optional<domain::BookId> book;
    QString sha256;
    qint64 bytes = 0;
    QString error;
};

struct RecoveryReport {
    int registered = 0;             // Verified operations completed.
    int duplicates = 0;             // Verified operations whose bytes were meanwhile catalogued.
    int abandoned = 0;              // Copying operations closed; nothing had been verified.
    int failed = 0;                 // Verified operations whose file was missing or corrupt.
    int deferred = 0;               // Verified operations kept open (e.g. installation failed); retried next time.
    int removedStagingDirectories = 0;
    int removedUnreferencedFiles = 0;  // Installed copies no asset references (duplicates only).
    QStringList orphanedManagedFiles;  // files/* not referenced and not claimed; reported, never deleted.
    QStringList notes;
};

class ImportService {
public:
    using Progress = std::function<void(qint64 done, qint64 total)>;
    using CrashHook = std::function<bool(ImportStage)>;  // Return true to stop as if the process died.

    explicit ImportService(catalog::Library& library);

    // Blocking: file work runs on the calling thread and catalog work on the
    // database thread. Never call on the GUI thread. `cancel` may be null.
    ImportResult importFile(const QString& sourcePath, const std::atomic_bool* cancel = nullptr,
                            const Progress& progress = {});

    // Resumes or closes open operations and removes stray staging folders.
    // Idempotent. Run before new imports start.
    RecoveryReport recover();

    // Tests only.
    void setCrashHook(CrashHook hook) { m_crashHook = std::move(hook); }
    void setAfterFirstChunkHook(std::function<void()> hook) { m_afterFirstChunk = std::move(hook); }
    void setInstallHook(std::function<bool()> hook) { m_installHook = std::move(hook); }  // Return false to fail.

    const LibraryLayout& layout() const { return m_layout; }

private:
    bool crashAt(ImportStage stage) const { return m_crashHook && m_crashHook(stage); }
    void removeStaging(const domain::ImportId& id) const;
    // Deletes an installed copy only if no asset references it.
    bool removeIfUnreferenced(const QString& relativePath);
    bool install(const QString& staged, const QString& managed, QString* error);

    catalog::Library& m_library;
    LibraryLayout m_layout;
    CrashHook m_crashHook;
    std::function<void()> m_afterFirstChunk;
    std::function<bool()> m_installHook;
};

QString outcomeName(ImportResult::Outcome outcome);

} // namespace mbl::storage
