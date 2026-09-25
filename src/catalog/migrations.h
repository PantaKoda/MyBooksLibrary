// A2 catalog schema: ordered, versioned migrations tracked in PRAGMA user_version.
// A2 owns migration order and includes A3's search index definitions.
#pragma once

#include "domain/result.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace mbl::catalog {

struct Migration {
    int version = 0;       // Consecutive from 1.
    QString name;
    QStringList statements;
};

// The application's migrations, oldest first. Never edit a released entry;
// add a new one.
const QList<Migration>& catalogMigrations();
int latestSchemaVersion();

// Reads PRAGMA user_version, or -1 on error.
int schemaVersion(QSqlDatabase& db);

// Read-only check that this application can open the catalog: fails with
// SchemaTooNew for a newer schema. Writes nothing.
domain::Status checkCompatible(QSqlDatabase& db, const QList<Migration>& migrations = catalogMigrations());

// Applies each pending migration in its own transaction. A failing migration
// is rolled back and leaves the catalog at the previous version; nothing is
// recreated or discarded. A catalog newer than `migrations` is refused
// (SchemaTooNew) without modification.
domain::Status migrate(QSqlDatabase& db, const QList<Migration>& migrations = catalogMigrations());

} // namespace mbl::catalog
