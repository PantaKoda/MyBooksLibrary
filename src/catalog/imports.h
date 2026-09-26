// A2: persistent import operations. Filesystem steps and catalog commits are
// not one atomic transaction, so each phase is recorded before the next
// filesystem step; recovery (storage::ImportService::recover) resumes or
// closes open operations after a crash.
#pragma once

#include "domain/book.h"
#include "domain/ids.h"
#include "domain/importing.h"
#include "domain/result.h"

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

namespace mbl::catalog {

// Records a new operation in phase Copying.
domain::Result<domain::ImportId> beginImport(QSqlDatabase& db, const QString& sourcePath, const QString& sourceName,
                                             qint64 sourceSize, const QDateTime& sourceModified);

// Copying -> Verified: the staged bytes were verified; reserves the asset ID.
domain::Status markImportVerified(QSqlDatabase& db, const domain::ImportId& id, const QString& sha256,
                                  qint64 byteSize, const domain::AssetId& assetId);

// Verified -> Registered, in one transaction with inserting the asset and book
// and projecting the book into search. Fails with Duplicate (nothing changed)
// if a book with the same SHA-256 exists.
domain::Result<domain::BookId> completeImport(QSqlDatabase& db, const domain::ImportId& id,
                                              const domain::NewBook& book);

// Copying or Verified -> Duplicate: records the digest and size of the bytes
// that were read and the existing book they match.
domain::Status closeImportAsDuplicate(QSqlDatabase& db, const domain::ImportId& id, const QString& sha256,
                                      qint64 byteSize, const domain::BookId& existing);

// Moves an open operation to Failed, Cancelled or Abandoned.
domain::Status closeImport(QSqlDatabase& db, const domain::ImportId& id, domain::ImportPhase phase,
                           const std::optional<domain::BookId>& book, const QString& error);

domain::Result<domain::ImportOperation> importOperation(QSqlDatabase& db, const domain::ImportId& id);
domain::Result<QList<domain::ImportOperation>> openImports(QSqlDatabase& db);

// True if an asset row references `relativePath` (such a file must never be deleted).
domain::Result<bool> managedPathReferenced(QSqlDatabase& db, const QString& relativePath);

} // namespace mbl::catalog
