#include "catalog/migrations.h"

#include "search/searchindex.h"

#include <QSqlError>
#include <QSqlQuery>

namespace mbl::catalog {

using domain::ErrorCode;
using domain::makeError;

namespace {

QList<Migration> buildMigrations()
{
    Migration v1;
    v1.version = 1;
    v1.name = QStringLiteral("catalog, metadata, contents and search projections");
    v1.statements = {
        QStringLiteral(R"(CREATE TABLE assets (
            id TEXT PRIMARY KEY,
            sha256 TEXT NOT NULL UNIQUE CHECK (length(sha256) = 64),
            byte_size INTEGER NOT NULL CHECK (byte_size >= 0),
            page_count INTEGER CHECK (page_count IS NULL OR page_count >= 0),
            managed_path TEXT NOT NULL UNIQUE,
            created_at TEXT NOT NULL))"),

        QStringLiteral(R"(CREATE TABLE books (
            id TEXT PRIMARY KEY,
            asset_id TEXT NOT NULL UNIQUE REFERENCES assets(id) ON DELETE RESTRICT,
            lifecycle TEXT NOT NULL DEFAULT 'active' CHECK (lifecycle IN ('active', 'trashed')),
            revision INTEGER NOT NULL DEFAULT 1,
            metadata_generation INTEGER NOT NULL DEFAULT 0,
            toc_generation INTEGER NOT NULL DEFAULT 0,
            active_metadata_run_id TEXT REFERENCES metadata_runs(id),
            active_toc_run_id TEXT REFERENCES toc_runs(id),
            original_file_name TEXT NOT NULL,
            original_path TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL))"),

        QStringLiteral(R"(CREATE TABLE metadata_runs (
            id TEXT PRIMARY KEY,
            book_id TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
            generation INTEGER NOT NULL,
            source_sha256 TEXT NOT NULL,
            sdk_version TEXT NOT NULL,
            model_identity TEXT NOT NULL,
            options_json TEXT NOT NULL,
            outcome TEXT NOT NULL,
            report_path TEXT,
            created_at TEXT NOT NULL,
            title_status TEXT NOT NULL,
            title TEXT,
            subtitle TEXT,
            contributors_status TEXT NOT NULL,
            edition_status TEXT NOT NULL,
            edition_statement TEXT,
            edition_ordinal INTEGER,
            publication_year_status TEXT NOT NULL,
            publication_year INTEGER,
            copyright_year_status TEXT NOT NULL,
            copyright_year INTEGER))"),
        QStringLiteral("CREATE INDEX metadata_runs_book ON metadata_runs(book_id)"),

        QStringLiteral(R"(CREATE TABLE metadata_run_contributors (
            run_id TEXT NOT NULL REFERENCES metadata_runs(id) ON DELETE CASCADE,
            position INTEGER NOT NULL,
            name TEXT NOT NULL,
            role TEXT NOT NULL CHECK (role IN ('author', 'editor', 'translator', 'organization')),
            PRIMARY KEY (run_id, position)))"),

        // No row means Auto for that field.
        QStringLiteral(R"(CREATE TABLE metadata_overrides (
            book_id TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
            field TEXT NOT NULL CHECK (field IN ('title', 'subtitle', 'contributors', 'edition',
                                                 'publication_year', 'copyright_year')),
            mode TEXT NOT NULL CHECK (mode IN ('value', 'cleared')),
            value_text TEXT,
            value_int INTEGER,
            updated_at TEXT NOT NULL,
            PRIMARY KEY (book_id, field)))"),

        QStringLiteral(R"(CREATE TABLE metadata_override_contributors (
            book_id TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
            position INTEGER NOT NULL,
            name TEXT NOT NULL,
            role TEXT NOT NULL CHECK (role IN ('author', 'editor', 'translator', 'organization')),
            PRIMARY KEY (book_id, position)))"),

        QStringLiteral(R"(CREATE TABLE toc_runs (
            id TEXT PRIMARY KEY,
            book_id TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
            generation INTEGER NOT NULL,
            source_sha256 TEXT NOT NULL,
            sdk_version TEXT NOT NULL,
            model_identity TEXT NOT NULL,
            options_json TEXT NOT NULL,
            outcome TEXT NOT NULL,
            report_path TEXT,
            created_at TEXT NOT NULL,
            plan_ready INTEGER NOT NULL CHECK (plan_ready IN (0, 1))))"),
        QStringLiteral("CREATE INDEX toc_runs_book ON toc_runs(book_id)"),

        QStringLiteral(R"(CREATE TABLE toc_entries (
            id INTEGER PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES toc_runs(id) ON DELETE CASCADE,
            sdk_entry_id TEXT NOT NULL,
            entry_order INTEGER NOT NULL,
            title TEXT NOT NULL,
            hierarchy TEXT NOT NULL CHECK (hierarchy IN ('root', 'known_parent', 'unknown')),
            parent_sdk_entry_id TEXT,
            printed_label TEXT,
            destination_state TEXT NOT NULL CHECK (destination_state IN ('resolved', 'ambiguous', 'unresolved')),
            destination_page INTEGER CHECK (destination_page IS NULL OR destination_page >= 0),
            source_toc_page INTEGER CHECK (source_toc_page IS NULL OR source_toc_page >= 0),
            in_export_plan INTEGER NOT NULL CHECK (in_export_plan IN (0, 1)),
            UNIQUE (run_id, sdk_entry_id),
            CHECK ((destination_state = 'resolved') = (destination_page IS NOT NULL)),
            CHECK (hierarchy = 'known_parent' OR parent_sdk_entry_id IS NULL)))"),
        QStringLiteral("CREATE INDEX toc_entries_run ON toc_entries(run_id, entry_order)"),
    };
    // A3's derived, rebuildable projections.
    v1.statements += search::projectionSchema();

    Migration v2;
    v2.version = 2;
    v2.name = QStringLiteral("import operations");
    v2.statements = {
        // One row per attempt to import an external PDF; open rows are recovered at startup.
        // Import history blocks permanent deletion of its book (RESTRICT) until M08
        // defines how history is kept or removed.
        QStringLiteral(R"(CREATE TABLE import_operations (
            id TEXT PRIMARY KEY,
            source_path TEXT NOT NULL,
            source_name TEXT NOT NULL,
            source_size INTEGER NOT NULL CHECK (source_size >= 0),
            source_modified TEXT NOT NULL,
            phase TEXT NOT NULL CHECK (phase IN ('copying', 'verified', 'registered', 'duplicate', 'failed',
                                                 'cancelled', 'abandoned')),
            sha256 TEXT CHECK (sha256 IS NULL OR length(sha256) = 64),
            byte_size INTEGER CHECK (byte_size IS NULL OR byte_size >= 0),
            asset_id TEXT,
            book_id TEXT REFERENCES books(id) ON DELETE RESTRICT,
            error TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            CHECK (phase NOT IN ('verified', 'registered')
                   OR (sha256 IS NOT NULL AND byte_size IS NOT NULL AND asset_id IS NOT NULL)),
            CHECK (phase <> 'duplicate' OR (sha256 IS NOT NULL AND byte_size IS NOT NULL AND book_id IS NOT NULL)),
            CHECK (phase <> 'registered' OR book_id IS NOT NULL)))"),
        QStringLiteral("CREATE INDEX import_operations_open ON import_operations(phase) "
                       "WHERE phase IN ('copying', 'verified')"),
    };

    Migration v3;
    v3.version = 3;
    v3.name = QStringLiteral("processing jobs and metadata evidence");
    v3.statements = {
        // Durable jobs: persisted before they run, recovered after a crash.
        QStringLiteral(R"(CREATE TABLE jobs (
            id TEXT PRIMARY KEY,
            book_id TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
            kind TEXT NOT NULL CHECK (kind IN ('metadata', 'toc')),
            state TEXT NOT NULL CHECK (state IN ('queued', 'running', 'cancel_requested', 'succeeded', 'failed',
                                                 'cancelled', 'interrupted')),
            generation INTEGER NOT NULL,
            source_sha256 TEXT NOT NULL CHECK (length(source_sha256) = 64),
            attempt INTEGER NOT NULL DEFAULT 0,
            outcome TEXT,
            error TEXT,
            run_id TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            started_at TEXT,
            finished_at TEXT,
            CHECK (state <> 'succeeded' OR run_id IS NOT NULL)))"),
        // At most one open job per book and kind.
        QStringLiteral("CREATE UNIQUE INDEX jobs_one_open ON jobs(book_id, kind) "
                       "WHERE state IN ('queued', 'running', 'cancel_requested')"),
        QStringLiteral("CREATE INDEX jobs_by_state ON jobs(state, created_at)"),

        // Evidence, candidates and reasons behind each metadata field of a run
        // (JSON arrays; see catalog/jobs.cpp for the shape).
        QStringLiteral(R"(CREATE TABLE metadata_field_details (
            run_id TEXT NOT NULL REFERENCES metadata_runs(id) ON DELETE CASCADE,
            field TEXT NOT NULL CHECK (field IN ('title', 'contributors', 'edition', 'publication_year',
                                                 'copyright_year')),
            evidence_json TEXT NOT NULL,
            alternatives_json TEXT NOT NULL,
            reasons_json TEXT NOT NULL,
            PRIMARY KEY (run_id, field)))"),
    };
    return {v1, v2, v3};
}

} // namespace

const QList<Migration>& catalogMigrations()
{
    static const QList<Migration> migrations = buildMigrations();
    return migrations;
}

int latestSchemaVersion()
{
    return catalogMigrations().isEmpty() ? 0 : catalogMigrations().constLast().version;
}

int schemaVersion(QSqlDatabase& db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
        return -1;
    return query.value(0).toInt();
}

domain::Status checkCompatible(QSqlDatabase& db, const QList<Migration>& migrations)
{
    const int current = schemaVersion(db);
    if (current < 0)
        return makeError(ErrorCode::Database, QStringLiteral("Cannot read the catalog schema version."));
    const int latest = migrations.isEmpty() ? 0 : migrations.constLast().version;
    if (current > latest) {
        return makeError(ErrorCode::SchemaTooNew,
                         QStringLiteral("This library's catalog uses schema version %1, but this version of "
                                        "MyBooksLibrary supports up to version %2. Open it with a newer "
                                        "MyBooksLibrary; the catalog was not changed.")
                             .arg(current)
                             .arg(latest));
    }
    return domain::Done{};
}

domain::Status migrate(QSqlDatabase& db, const QList<Migration>& migrations)
{
    if (auto compatible = checkCompatible(db, migrations); !compatible)
        return compatible;
    const int current = schemaVersion(db);

    int expected = 1;
    for (const Migration& migration : migrations) {
        if (migration.version != expected++) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("Migration list is not consecutive at version %1.")
                                 .arg(migration.version));
        }
        if (migration.version <= current)
            continue;

        if (!db.transaction()) {
            return makeError(ErrorCode::Database, QStringLiteral("Cannot begin migration %1: %2")
                                                      .arg(migration.version)
                                                      .arg(db.lastError().text()));
        }
        QSqlQuery query(db);
        QString failure;
        for (const QString& statement : migration.statements) {
            if (!query.exec(statement)) {
                failure = QStringLiteral("%1 (statement: %2)")
                              .arg(query.lastError().text(), statement.simplified().left(120));
                break;
            }
        }
        if (failure.isEmpty()
            && !query.exec(QStringLiteral("PRAGMA user_version = %1").arg(migration.version)))
            failure = query.lastError().text();
        query.finish();
        if (failure.isEmpty() && !db.commit())
            failure = db.lastError().text();
        if (!failure.isEmpty()) {
            db.rollback();
            return makeError(ErrorCode::Database,
                             QStringLiteral("Catalog migration %1 (%2) failed and was rolled back; the "
                                            "catalog remains at schema version %3. %4")
                                 .arg(migration.version)
                                 .arg(migration.name)
                                 .arg(schemaVersion(db))
                                 .arg(failure));
        }
    }
    return domain::Done{};
}

} // namespace mbl::catalog
