// A2: the user's edits of a book's contents. Edits never change an analysis
// run: each save is a new numbered revision (a full entry list) based on one
// run, and earlier revisions are kept. The book's effective contents are its
// active revision, else its active run (catalog::bookDetails), and search
// follows them in the same transaction.
//
// SDK entry IDs are not stable across reruns, so edits are never moved to a
// new run by position or title. A new run whose entries are the same in
// content as the revision's base carries the edits over; any other new run
// leaves the edits in effect and marks the book as needing reconciliation,
// which only the user resolves (keepTocEdits or useAnalyzedToc).
#pragma once

#include "domain/book.h"
#include "domain/ids.h"
#include "domain/result.h"
#include "domain/toc.h"

#include <QList>
#include <QSqlDatabase>

namespace mbl::catalog {

// Applies `edits` in order to the book's effective contents and saves the
// result as a new active revision. Refused with StaleGeneration when `base`
// no longer describes the book's contents (a new run or another edit came
// first), with InvalidArgument for an invalid edit (nothing is saved), and
// with Trashed for a trashed book.
domain::Result<domain::TocRevisionInfo> editToc(QSqlDatabase& db, const domain::BookId& book,
                                                const domain::TocEditBase& base,
                                                const QList<domain::TocEdit>& edits);

// Reconciliation, after a newer analysis changed the entries: keep the edited
// contents as they are (a new revision based on the newer run, no longer tied
// to any of its entries)...
domain::Result<domain::TocRevisionInfo> keepTocEdits(QSqlDatabase& db, const domain::BookId& book,
                                                     const domain::TocEditBase& base);
// ...or show the active run's contents again. Also "discard my edits" at any
// time. The revisions stay in the catalog.
domain::Status useAnalyzedToc(QSqlDatabase& db, const domain::BookId& book, const domain::TocEditBase& base);

// Saved revisions of the book's contents (history), newest first.
domain::Result<QList<domain::TocRevisionInfo>> tocRevisions(QSqlDatabase& db, const domain::BookId& book);

} // namespace mbl::catalog
