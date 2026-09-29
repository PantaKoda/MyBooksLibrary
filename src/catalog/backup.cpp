#include "catalog/backup.h"

#include "catalog/catalog_internal.h"
#include "catalog/migrations.h"

#include <QFileInfo>
#include <QSqlQuery>
#include <QVariant>

namespace mbl::catalog {

using namespace mbl::domain;
using detail::sqlError;

Result<CatalogSnapshot> snapshotCatalog(QSqlDatabase& db, const QString& file)
{
    if (QFileInfo::exists(file))
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("The catalog copy %1 already exists.").arg(file));
    CatalogSnapshot snapshot;
    snapshot.schemaVersion = schemaVersion(db);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT managed_path, sha256, byte_size FROM assets ORDER BY managed_path")))
        return sqlError(q);
    while (q.next())
        snapshot.sources << CatalogSnapshot::Source{q.value(0).toString(), q.value(1).toString(), q.value(2).toLongLong()};
    if (!q.exec(QStringLiteral("SELECT report_path FROM metadata_runs WHERE report_path IS NOT NULL "
                               "UNION SELECT report_path FROM toc_runs WHERE report_path IS NOT NULL ORDER BY 1")))
        return sqlError(q);
    while (q.next())
        snapshot.reports << q.value(0).toString();
    if (!q.exec(QStringLiteral("SELECT count(*) FROM books")) || !q.next())
        return sqlError(q);
    snapshot.books = q.value(0).toInt();
    q.finish();

    // Nothing else runs on this connection in between: the list and the copy
    // describe the same catalog.
    q.prepare(QStringLiteral("VACUUM INTO ?"));
    q.addBindValue(file);
    if (!q.exec())
        return sqlError(q);
    return snapshot;
}

} // namespace mbl::catalog
