#include "catalog/reading.h"

#include "catalog/catalog_internal.h"

#include <QSqlQuery>
#include <QVariant>

namespace mbl::catalog {

using namespace mbl::domain;
using detail::now;
using detail::sqlError;

Status setReadingPosition(QSqlDatabase& db, const BookId& book, int pageIndex)
{
    if (pageIndex < 0)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("A page index cannot be negative."));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT a.page_count FROM books b JOIN assets a ON a.id = b.asset_id WHERE b.id = ?"));
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No book %1.").arg(book.toString()));
    if (!q.value(0).isNull() && pageIndex >= q.value(0).toInt()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Page index %1 is beyond the %2-page document.").arg(pageIndex).arg(q.value(0).toInt()));
    }
    q.finish();
    q.prepare(QStringLiteral("INSERT INTO reading_positions(book_id, page_index, updated_at) VALUES (?, ?, ?) "
                             "ON CONFLICT(book_id) DO UPDATE SET page_index = excluded.page_index, "
                             "updated_at = excluded.updated_at"));
    q.addBindValue(book.toString());
    q.addBindValue(pageIndex);
    q.addBindValue(now());
    if (!q.exec())
        return sqlError(q);
    return Done{};
}

Result<std::optional<int>> readingPosition(QSqlDatabase& db, const BookId& book)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT page_index FROM reading_positions WHERE book_id = ?"));
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return std::optional<int>();
    return std::optional<int>(q.value(0).toInt());
}

} // namespace mbl::catalog
