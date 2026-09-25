// A2 catalog operations. Every function runs on the database thread with the
// executor's connection (see infrastructure::DatabaseExecutor). Mutations run
// in one transaction together with the matching search-projection update, so
// a failure leaves neither catalog rows nor index rows changed.
#pragma once

#include "domain/book.h"
#include "domain/ids.h"
#include "domain/metadata.h"
#include "domain/result.h"
#include "domain/toc.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

namespace mbl::catalog {

// Registers an asset and its book, and indexes the book (file-name fallback
// title). Fails with Duplicate if an asset with the same SHA-256 exists;
// deduplication is decided by the caller using findBookBySha256().
domain::Result<domain::BookId> registerBook(QSqlDatabase& db, const domain::NewBook& book);

domain::Result<std::optional<domain::BookId>> findBookBySha256(QSqlDatabase& db, const QString& sha256);

// Starts a new request generation for the component. Results carrying an
// older generation can no longer be published.
domain::Result<domain::PublishTicket> requestMetadataRun(QSqlDatabase& db, const domain::BookId& book);
domain::Result<domain::PublishTicket> requestTocRun(QSqlDatabase& db, const domain::BookId& book);

// Stores the run and makes it active if the ticket is still current, the
// book is not trashed and the source digest matches the book's asset. Other
// components' active runs and the user's overrides are untouched.
domain::Result<domain::RunId> publishMetadata(QSqlDatabase& db, const domain::PublishTicket& ticket,
                                              const domain::RunIdentity& run,
                                              const domain::ExtractedMetadata& metadata);

// `toc.outcome` must equal `run.outcome` (InvalidArgument otherwise); it is
// stored once and read back as TocAnalysis::outcome.
// Keeps every entry. A KnownParent entry whose parent is missing, itself or
// part of a cycle is stored as Unknown rather than given an invented parent.
domain::Result<domain::RunId> publishToc(QSqlDatabase& db, const domain::PublishTicket& ticket,
                                         const domain::RunIdentity& run, const domain::TocAnalysis& toc);

// Auto removes the override. Value needs a non-empty value of the field's type.
domain::Status setOverride(QSqlDatabase& db, const domain::BookId& book, domain::MetadataField field,
                           const domain::MetadataOverride& value);

// Trash hides the book from search and invalidates pending generations.
// Restore makes it searchable again. Both are idempotent.
domain::Status trashBook(QSqlDatabase& db, const domain::BookId& book);
domain::Status restoreBook(QSqlDatabase& db, const domain::BookId& book);

domain::Result<QList<domain::BookSummary>> listBooks(QSqlDatabase& db, domain::Lifecycle lifecycle);
domain::Result<domain::BookDetails> bookDetails(QSqlDatabase& db, const domain::BookId& book);

// Recreates all search projections from catalog data, without any SDK work.
domain::Status rebuildSearchIndex(QSqlDatabase& db);

} // namespace mbl::catalog
