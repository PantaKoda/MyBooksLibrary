// A2: where the user was reading each book (a zero-based physical page
// index, never a printed label). One row per book; kept while a book is in
// Trash, removed with the book.
#pragma once

#include "domain/ids.h"
#include "domain/result.h"

#include <QSqlDatabase>

#include <optional>

namespace mbl::catalog {

// Records the page (0 <= page, and < the page count when known). Fails with
// NotFound for an unknown book, InvalidArgument for a page out of range.
domain::Status setReadingPosition(QSqlDatabase& db, const domain::BookId& book, int pageIndex);

// The recorded page, or nullopt when the book was never read.
domain::Result<std::optional<int>> readingPosition(QSqlDatabase& db, const domain::BookId& book);

} // namespace mbl::catalog
