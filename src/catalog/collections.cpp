#include "catalog/collections.h"

#include "catalog/catalog_internal.h"

#include <QSqlQuery>

#include <algorithm>

namespace mbl::catalog {

using namespace mbl::domain;
using detail::now;
using detail::sqlError;
using detail::Transaction;

namespace {

Result<QString> validName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("A collection needs a name."));
    return trimmed;
}

// Another collection with this name, ignoring ASCII case.
Status checkNameFree(QSqlDatabase& db, const QString& name, const CollectionId& except)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM collections WHERE name = ? COLLATE NOCASE AND id <> ?"));
    q.addBindValue(name);
    q.addBindValue(except.toString());
    if (!q.exec())
        return sqlError(q);
    if (q.next())
        return makeError(ErrorCode::Duplicate, QStringLiteral("A collection named \"%1\" already exists.").arg(name));
    return Done{};
}

Status requireCollection(QSqlDatabase& db, const CollectionId& collection)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM collections WHERE id = ?"));
    q.addBindValue(collection.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No collection %1.").arg(collection.toString()));
    return Done{};
}

Status touchCollection(QSqlDatabase& db, const CollectionId& collection)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE collections SET updated_at = ? WHERE id = ?"));
    q.addBindValue(now());
    q.addBindValue(collection.toString());
    if (!q.exec())
        return sqlError(q);
    return Done{};
}

void sortByName(QList<CollectionSummary>& list)
{
    std::sort(list.begin(), list.end(), [](const CollectionSummary& a, const CollectionSummary& b) {
        const int byName = QString::localeAwareCompare(a.name, b.name);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
}

// Collections with their active book counts, optionally only those of one book.
Result<QList<CollectionSummary>> summaries(QSqlDatabase& db, const std::optional<BookId>& ofBook)
{
    QSqlQuery q(db);
    QString sql = QStringLiteral(
        "SELECT c.id, c.name, (SELECT COUNT(*) FROM collection_books cb JOIN books b ON b.id = cb.book_id "
        "WHERE cb.collection_id = c.id AND b.lifecycle = 'active') FROM collections c");
    if (ofBook)
        sql += QStringLiteral(" WHERE c.id IN (SELECT collection_id FROM collection_books WHERE book_id = ?)");
    q.prepare(sql);
    if (ofBook)
        q.addBindValue(ofBook->toString());
    if (!q.exec())
        return sqlError(q);
    QList<CollectionSummary> out;
    while (q.next())
        out << CollectionSummary{CollectionId::fromString(q.value(0).toString()), q.value(1).toString(), q.value(2).toInt()};
    sortByName(out);
    return out;
}

} // namespace

Result<CollectionId> createCollection(QSqlDatabase& db, const QString& name)
{
    auto valid = validName(name);
    if (!valid)
        return valid.error();
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    const CollectionId id = CollectionId::create();
    if (auto s = checkNameFree(db, valid.value(), id); !s)
        return s.error();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO collections(id, name, created_at, updated_at) VALUES (?, ?, ?, ?)"));
    const QString stamp = now();
    q.addBindValue(id.toString());
    q.addBindValue(valid.value());
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    if (!q.exec())
        return sqlError(q);
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return id;
}

Status renameCollection(QSqlDatabase& db, const CollectionId& collection, const QString& name)
{
    auto valid = validName(name);
    if (!valid)
        return valid.error();
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    if (auto s = requireCollection(db, collection); !s)
        return s;
    if (auto s = checkNameFree(db, valid.value(), collection); !s)
        return s;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE collections SET name = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(valid.value());
    q.addBindValue(now());
    q.addBindValue(collection.toString());
    if (!q.exec())
        return sqlError(q);
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

Status deleteCollection(QSqlDatabase& db, const CollectionId& collection)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    if (auto s = requireCollection(db, collection); !s)
        return s;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM collections WHERE id = ?"));  // Memberships cascade.
    q.addBindValue(collection.toString());
    if (!q.exec())
        return sqlError(q);
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

Status addToCollection(QSqlDatabase& db, const CollectionId& collection, const QList<BookId>& books)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    if (auto s = requireCollection(db, collection); !s)
        return s;
    QSqlQuery q(db);
    const QString stamp = now();
    for (const BookId& book : books) {
        q.prepare(QStringLiteral("SELECT lifecycle FROM books WHERE id = ?"));
        q.addBindValue(book.toString());
        if (!q.exec())
            return sqlError(q);
        if (!q.next())
            return makeError(ErrorCode::NotFound, QStringLiteral("No book %1.").arg(book.toString()));
        if (q.value(0).toString() == QLatin1String("trashed"))
            return makeError(ErrorCode::Trashed, QStringLiteral("A book in Trash cannot be added to a collection."));
        q.prepare(QStringLiteral("INSERT OR IGNORE INTO collection_books(collection_id, book_id, added_at) "
                                 "VALUES (?, ?, ?)"));
        q.addBindValue(collection.toString());
        q.addBindValue(book.toString());
        q.addBindValue(stamp);
        if (!q.exec())
            return sqlError(q);
    }
    if (auto s = touchCollection(db, collection); !s)
        return s;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

Status removeFromCollection(QSqlDatabase& db, const CollectionId& collection, const QList<BookId>& books)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    if (auto s = requireCollection(db, collection); !s)
        return s;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM collection_books WHERE collection_id = ? AND book_id = ?"));
    for (const BookId& book : books) {
        q.addBindValue(collection.toString());
        q.addBindValue(book.toString());
        if (!q.exec())
            return sqlError(q);
    }
    if (auto s = touchCollection(db, collection); !s)
        return s;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

Result<QList<CollectionSummary>> listCollections(QSqlDatabase& db)
{
    return summaries(db, std::nullopt);
}

Result<QList<BookSummary>> listCollectionBooks(QSqlDatabase& db, const CollectionId& collection)
{
    if (auto s = requireCollection(db, collection); !s)
        return s.error();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT b.id FROM books b JOIN collection_books cb ON cb.book_id = b.id "
                             "WHERE cb.collection_id = ? AND b.lifecycle = 'active' ORDER BY b.created_at, b.id"));
    q.addBindValue(collection.toString());
    if (!q.exec())
        return sqlError(q);
    QList<BookId> ids;
    while (q.next())
        ids << BookId::fromString(q.value(0).toString());
    q.finish();
    QList<BookSummary> books;
    for (const BookId& id : ids) {
        auto summary = detail::bookSummary(db, id);
        if (!summary)
            return summary.error();
        books << summary.value();
    }
    return books;
}

Result<QList<BookId>> collectionBookIds(QSqlDatabase& db, const CollectionId& collection)
{
    if (auto s = requireCollection(db, collection); !s)
        return s.error();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT book_id FROM collection_books WHERE collection_id = ?"));
    q.addBindValue(collection.toString());
    if (!q.exec())
        return sqlError(q);
    QList<BookId> ids;
    while (q.next())
        ids << BookId::fromString(q.value(0).toString());
    return ids;
}

Result<QList<CollectionSummary>> collectionsOf(QSqlDatabase& db, const BookId& book)
{
    return summaries(db, book);
}

} // namespace mbl::catalog
