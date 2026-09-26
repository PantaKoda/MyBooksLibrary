// A1: streamed copy with SHA-256, source-change detection and verification of
// the written bytes. The source is only ever opened read-only.
#pragma once

#include <QDateTime>
#include <QString>

#include <atomic>
#include <functional>
#include <optional>

namespace mbl::storage {

// What the file system reports about a file; used to detect a source that
// changes while it is being copied.
struct FileStamp {
    bool exists = false;
    qint64 size = -1;
    QDateTime modified;  // UTC.

    friend bool operator==(const FileStamp& a, const FileStamp& b)
    {
        return a.exists == b.exists && a.size == b.size && a.modified == b.modified;
    }
    friend bool operator!=(const FileStamp& a, const FileStamp& b) { return !(a == b); }
};

FileStamp stampOf(const QString& path);

struct CopyOutcome {
    enum class Status { Ok, SourceUnreadable, TargetUnwritable, SourceChanged, Cancelled, VerifyFailed };
    Status status = Status::SourceUnreadable;
    QString sha256;   // Lower-case hex of the copied bytes (Ok only).
    qint64 bytes = 0;
    QString error;

    bool ok() const { return status == Status::Ok; }
};

struct CopyHooks {
    std::function<void(qint64 done, qint64 total)> progress;
    std::function<void()> afterFirstChunk;  // Tests: e.g. modify the source mid-copy.
};

// Copies `source` to `target` through a QSaveFile (the target appears only
// complete), hashing while streaming. Fails with SourceChanged if the source's
// size or modification time differs after reading, and with VerifyFailed if
// re-reading the target gives a different digest. Never modifies `source`;
// on failure no target file is left behind. Blocking: never call on the GUI thread.
CopyOutcome copyVerified(const QString& source, const QString& target, const std::atomic_bool* cancel,
                         const CopyHooks& hooks = {});

// Moves `from` to `to` on the same volume as one filesystem operation: never
// copies (unlike QFile::rename's fallback) and never replaces an existing
// `to`. On Windows the move is flushed before returning (MOVEFILE_WRITE_THROUGH).
bool commitMove(const QString& from, const QString& to, QString* error);

// True if the file starts like a PDF: "%PDF-" within its first 1024 bytes
// (the format tolerates leading bytes). A cheap guard, not a validation.
bool looksLikePdf(const QString& path);

// Lower-case hex SHA-256 of a file, or nullopt if it cannot be read or `cancel` is set.
std::optional<QString> sha256OfFile(const QString& path, const std::atomic_bool* cancel = nullptr);

} // namespace mbl::storage
