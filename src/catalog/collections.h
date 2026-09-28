// A2: collections, named groups of books. Membership only: a book in several
// collections is one book with one managed file. Trashed books keep their
// memberships and come back with them when restored, but are not listed or
// counted meanwhile. Deleting a collection never touches its books.
#pragma once

#include "domain/book.h"
#include "domain/collection.h"
#include "domain/ids.h"
#include "domain/result.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace mbl::catalog {

// Names are trimmed and must not be empty (InvalidArgument). A name already
// used by another collection, ignoring ASCII case, is refused (Duplicate).
domain::Result<domain::CollectionId> createCollection(QSqlDatabase& db, const QString& name);
domain::Status renameCollection(QSqlDatabase& db, const domain::CollectionId& collection, const QString& name);
// Removes the collection and its memberships; the books stay in the library.
domain::Status deleteCollection(QSqlDatabase& db, const domain::CollectionId& collection);

// Adding a book already in the collection changes nothing. Trashed books
// cannot be added (Trashed); unknown books or collections give NotFound.
// Each call is one transaction: all books are added, or none.
domain::Status addToCollection(QSqlDatabase& db, const domain::CollectionId& collection,
                               const QList<domain::BookId>& books);
domain::Status removeFromCollection(QSqlDatabase& db, const domain::CollectionId& collection,
                                    const QList<domain::BookId>& books);

// By name (locale-aware), with their active book counts.
domain::Result<QList<domain::CollectionSummary>> listCollections(QSqlDatabase& db);
// The collection's active books, in library order (as listBooks).
domain::Result<QList<domain::BookSummary>> listCollectionBooks(QSqlDatabase& db,
                                                               const domain::CollectionId& collection);
// The collections a book belongs to (also while it is in Trash), by name.
domain::Result<QList<domain::CollectionSummary>> collectionsOf(QSqlDatabase& db, const domain::BookId& book);

} // namespace mbl::catalog
