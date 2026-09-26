#include "storage/filecopy.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimeZone>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <cstdio>
#  include <cstring>
#endif

namespace mbl::storage {

namespace {

constexpr qint64 kChunk = 1024 * 1024;

bool cancelled(const std::atomic_bool* cancel)
{
    return cancel && cancel->load();
}

} // namespace

FileStamp stampOf(const QString& path)
{
    const QFileInfo info(path);
    FileStamp stamp;
    stamp.exists = info.exists() && info.isFile();
    if (stamp.exists) {
        stamp.size = info.size();
        stamp.modified = info.lastModified(QTimeZone::UTC);
    }
    return stamp;
}

bool commitMove(const QString& from, const QString& to, QString* error)
{
    if (QFileInfo::exists(to)) {
        if (error)
            *error = QStringLiteral("%1 already exists.").arg(to);
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring source = QDir::toNativeSeparators(from).toStdWString();
    const std::wstring target = QDir::toNativeSeparators(to).toStdWString();
    // No MOVEFILE_COPY_ALLOWED: a cross-volume move fails instead of copying.
    // No MOVEFILE_REPLACE_EXISTING: an existing target is never overwritten.
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        if (error)
            *error = QStringLiteral("Moving %1 to %2 failed (Windows error %3).").arg(from, to).arg(GetLastError());
        return false;
    }
    return true;
#else
    // rename(2) is atomic within one filesystem; the existence check above
    // guards replacement (the library writer lock excludes other writers).
    if (std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) != 0) {
        if (error)
            *error = QStringLiteral("Moving %1 to %2 failed: %3").arg(from, to, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    return true;
#endif
}

bool looksLikePdf(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.read(1024).contains("%PDF-");
}

std::optional<QString> sha256OfFile(const QString& path, const std::atomic_bool* cancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer;
    buffer.resize(kChunk);
    for (;;) {
        if (cancelled(cancel))
            return std::nullopt;
        const qint64 n = file.read(buffer.data(), kChunk);
        if (n < 0)
            return std::nullopt;
        if (n == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), n));
    }
    return QString::fromLatin1(hash.result().toHex());
}

CopyOutcome copyVerified(const QString& source, const QString& target, const std::atomic_bool* cancel,
                         const CopyHooks& hooks)
{
    using Status = CopyOutcome::Status;
    CopyOutcome out;
    const FileStamp before = stampOf(source);
    QFile in(source);
    if (!before.exists || !in.open(QIODevice::ReadOnly)) {
        out.status = Status::SourceUnreadable;
        out.error = QStringLiteral("Cannot read %1: %2").arg(source, in.errorString());
        return out;
    }
    QSaveFile outFile(target);
    if (!outFile.open(QIODevice::WriteOnly)) {
        out.status = Status::TargetUnwritable;
        out.error = QStringLiteral("Cannot write %1: %2").arg(target, outFile.errorString());
        return out;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer;
    buffer.resize(kChunk);
    bool first = true;
    for (;;) {
        if (cancelled(cancel)) {
            outFile.cancelWriting();
            out.status = Status::Cancelled;
            out.error = QStringLiteral("Import cancelled.");
            return out;
        }
        const qint64 n = in.read(buffer.data(), kChunk);
        if (n < 0) {
            outFile.cancelWriting();
            out.status = Status::SourceUnreadable;
            out.error = QStringLiteral("Reading %1 failed: %2").arg(source, in.errorString());
            return out;
        }
        if (n == 0)
            break;
        const QByteArrayView chunk(buffer.constData(), n);
        hash.addData(chunk);
        if (outFile.write(chunk.data(), n) != n) {
            outFile.cancelWriting();
            out.status = Status::TargetUnwritable;
            out.error = QStringLiteral("Writing %1 failed: %2").arg(target, outFile.errorString());
            return out;
        }
        out.bytes += n;
        if (hooks.progress)
            hooks.progress(out.bytes, before.size);
        if (first && hooks.afterFirstChunk)
            hooks.afterFirstChunk();
        first = false;
    }
    in.close();

    // The source must be exactly what was observed before reading.
    if (stampOf(source) != before || out.bytes != before.size) {
        outFile.cancelWriting();
        out.status = Status::SourceChanged;
        out.error = QStringLiteral("%1 changed while it was being imported.").arg(source);
        return out;
    }
    if (!outFile.commit()) {  // Flushes to disk and renames into place.
        out.status = Status::TargetUnwritable;
        out.error = QStringLiteral("Committing %1 failed: %2").arg(target, outFile.errorString());
        return out;
    }

    out.sha256 = QString::fromLatin1(hash.result().toHex());
    const auto verified = sha256OfFile(target, cancel);
    if (!verified || *verified != out.sha256) {
        QFile::remove(target);
        out.status = cancelled(cancel) ? Status::Cancelled : Status::VerifyFailed;
        out.error = cancelled(cancel) ? QStringLiteral("Import cancelled.")
                                      : QStringLiteral("The copy of %1 did not verify.").arg(source);
        return out;
    }
    out.status = Status::Ok;
    return out;
}

} // namespace mbl::storage
