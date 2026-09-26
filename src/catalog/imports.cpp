#include "catalog/imports.h"

#include "catalog/catalog_internal.h"

#include <QSqlQuery>
#include <QVariant>

namespace mbl::catalog {

using namespace mbl::domain;
using detail::now;
using detail::sqlError;
using detail::Transaction;

namespace {

const QString kColumns = QStringLiteral(
    "id, source_path, source_name, source_size, source_modified, phase, sha256, byte_size, asset_id, book_id, error");

ImportOperation readOperation(const QSqlQuery& q)
{
    ImportOperation op;
    op.id = ImportId::fromString(q.value(0).toString());
    op.sourcePath = q.value(1).toString();
    op.sourceName = q.value(2).toString();
    op.sourceSize = q.value(3).toLongLong();
    op.sourceModified = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    op.phase = importPhaseFromCode(q.value(5).toString()).value_or(ImportPhase::Failed);
    if (!q.value(6).isNull())
        op.sha256 = q.value(6).toString();
    if (!q.value(7).isNull())
        op.byteSize = q.value(7).toLongLong();
    if (!q.value(8).isNull())
        op.assetId = AssetId::fromString(q.value(8).toString());
    if (!q.value(9).isNull())
        op.book = BookId::fromString(q.value(9).toString());
    op.error = q.value(10).toString();
    return op;
}

// Updates phase (and optional columns) only if the operation is currently in `from`.
Status transition(QSqlDatabase& db, const ImportId& id, ImportPhase from, ImportPhase to,
                  const std::optional<BookId>& book, const QString& error)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE import_operations SET phase = ?, book_id = COALESCE(?, book_id), "
                             "error = ?, updated_at = ? WHERE id = ? AND phase = ?"));
    q.addBindValue(toCode(to));
    q.addBindValue(book ? QVariant(book->toString()) : QVariant(QMetaType(QMetaType::QString)));
    q.addBindValue(error.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(error));
    q.addBindValue(now());
    q.addBindValue(id.toString());
    q.addBindValue(toCode(from));
    if (!q.exec())
        return sqlError(q);
    if (q.numRowsAffected() != 1) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Import %1 is not in phase %2.").arg(id.toString(), toCode(from)));
    }
    return Done{};
}

} // namespace

Result<ImportId> beginImport(QSqlDatabase& db, const QString& sourcePath, const QString& sourceName,
                             qint64 sourceSize, const QDateTime& sourceModified)
{
    const ImportId id = ImportId::create();
    const QString stamp = now();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO import_operations(id, source_path, source_name, source_size, "
                             "source_modified, phase, created_at, updated_at) VALUES (?, ?, ?, ?, ?, 'copying', ?, ?)"));
    q.addBindValue(id.toString());
    q.addBindValue(sourcePath);
    q.addBindValue(sourceName);
    q.addBindValue(sourceSize);
    q.addBindValue(sourceModified.toUTC().toString(Qt::ISODateWithMs));
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    if (!q.exec())
        return sqlError(q);
    return id;
}

Status markImportVerified(QSqlDatabase& db, const ImportId& id, const QString& sha256, qint64 byteSize,
                          const AssetId& assetId)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE import_operations SET phase = 'verified', sha256 = ?, byte_size = ?, "
                             "asset_id = ?, updated_at = ? WHERE id = ? AND phase = 'copying'"));
    q.addBindValue(sha256.toLower());
    q.addBindValue(byteSize);
    q.addBindValue(assetId.toString());
    q.addBindValue(now());
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (q.numRowsAffected() != 1)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Import %1 is not copying.").arg(id.toString()));
    return Done{};
}

Result<BookId> completeImport(QSqlDatabase& db, const ImportId& id, const NewBook& book)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto op = importOperation(db, id);
    if (!op)
        return op.error();
    const ImportOperation& o = op.value();
    if (o.phase != ImportPhase::Verified || !o.sha256 || !o.assetId || *o.assetId != book.asset.id
        || *o.sha256 != book.asset.sha256.toLower()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Import %1 does not match the verified asset.").arg(id.toString()));
    }
    auto inserted = detail::insertBook(db, book);
    if (!inserted)
        return inserted;  // Duplicate or SQL error; the transaction rolls back.
    if (auto s = transition(db, id, ImportPhase::Verified, ImportPhase::Registered, inserted.value(), {}); !s)
        return s.error();
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return inserted;
}

Status closeImportAsDuplicate(QSqlDatabase& db, const ImportId& id, const QString& sha256, qint64 byteSize,
                              const BookId& existing)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE import_operations SET phase = 'duplicate', sha256 = ?, byte_size = ?, "
                             "book_id = ?, updated_at = ? WHERE id = ? AND phase IN ('copying', 'verified')"));
    q.addBindValue(sha256.toLower());
    q.addBindValue(byteSize);
    q.addBindValue(existing.toString());
    q.addBindValue(now());
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (q.numRowsAffected() != 1)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Import %1 is not open.").arg(id.toString()));
    return Done{};
}

Status closeImport(QSqlDatabase& db, const ImportId& id, ImportPhase phase, const std::optional<BookId>& book,
                   const QString& error)
{
    if (phase != ImportPhase::Failed && phase != ImportPhase::Cancelled && phase != ImportPhase::Abandoned)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Not a closing phase."));
    auto op = importOperation(db, id);
    if (!op)
        return op.error();
    if (isTerminal(op.value().phase))
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Import %1 is already closed.").arg(id.toString()));
    return transition(db, id, op.value().phase, phase, book, error);
}

Result<ImportOperation> importOperation(QSqlDatabase& db, const ImportId& id)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT %1 FROM import_operations WHERE id = ?").arg(kColumns));
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No import %1.").arg(id.toString()));
    return readOperation(q);
}

Result<QList<ImportOperation>> openImports(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM import_operations WHERE phase IN ('copying', 'verified') "
                               "ORDER BY created_at, id")
                    .arg(kColumns)))
        return sqlError(q);
    QList<ImportOperation> ops;
    while (q.next())
        ops << readOperation(q);
    return ops;
}

Result<bool> managedPathReferenced(QSqlDatabase& db, const QString& relativePath)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT count(*) FROM assets WHERE managed_path = ?"));
    q.addBindValue(relativePath);
    if (!q.exec() || !q.next())
        return sqlError(q);
    return q.value(0).toInt() > 0;
}

} // namespace mbl::catalog
