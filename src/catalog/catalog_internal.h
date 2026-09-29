// A2 internal helpers shared by the catalog translation units. Not part of
// the catalog's public contract.
#pragma once

#include "domain/book.h"
#include "domain/jobs.h"
#include "domain/result.h"

#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

#include <optional>

namespace mbl::catalog::detail {

// Rolls back unless commit() succeeded.
class Transaction {
public:
    explicit Transaction(QSqlDatabase& db) : m_db(db), m_active(db.transaction()) {}
    ~Transaction()
    {
        if (m_active)
            m_db.rollback();
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    bool begun() const { return m_active; }
    bool commit()
    {
        if (!m_active || !m_db.commit())
            return false;
        m_active = false;
        return true;
    }

private:
    QSqlDatabase& m_db;
    bool m_active;
};

QString now();
domain::Error sqlError(const QSqlQuery& query);
domain::Error sqlError(const QSqlDatabase& db, const QString& what);

// Inserts the asset and its book and projects the book into search, inside
// the caller's transaction. Fails with Duplicate if the SHA-256 is known.
domain::Result<domain::BookId> insertBook(QSqlDatabase& db, const domain::NewBook& book);

// Increments the metadata (or TOC) request generation inside the caller's
// transaction. Fails with Trashed for a trashed book.
domain::Result<domain::PublishTicket> bumpGeneration(QSqlDatabase& db, const domain::BookId& book, bool metadata);

// Moves a job from one of `from` to `to` (terminal states also set
// finished_at); fails with InvalidArgument if the job is in none of `from`.
domain::Status transitionJob(QSqlDatabase& db, const domain::JobId& id, std::initializer_list<domain::JobState> from,
                             domain::JobState to, const QString& outcome, const QString& error,
                             const std::optional<domain::RunId>& run = std::nullopt);

// enqueueJob() without its own transaction: returns the pending job of that
// kind, or starts a new request generation and queues a job for it.
domain::Result<domain::JobRecord> queueJob(QSqlDatabase& db, const domain::BookId& book, domain::JobKind kind);

// publishMetadata() without its own transaction, with a caller-chosen run ID
// (so the run's report file can be named before publication).
domain::Result<domain::RunId> publishMetadataRun(QSqlDatabase& db, const domain::RunId& id,
                                                 const domain::PublishTicket& ticket,
                                                 const domain::RunIdentity& run,
                                                 const domain::ExtractedMetadata& metadata);

// Bumps the book's revision and brings its search rows in line with the
// catalog, inside the caller's transaction.
domain::Status touchBook(QSqlDatabase& db, const domain::BookId& book);
domain::Status refreshBookProjection(QSqlDatabase& db, const domain::BookId& book);

// One book's summary, as listBooks() gives it.
domain::Result<domain::BookSummary> bookSummary(QSqlDatabase& db, const domain::BookId& book);

// Stored JSON of a TOC entry's evidence.
QString tocEvidenceToJson(const domain::TocEntryEvidence& evidence);
domain::TocEntryEvidence tocEvidenceFromJson(const QString& json);

// A run's stored entries, in order (hierarchy already normalised at publication).
domain::Result<QList<domain::TocEntry>> loadRunTocEntries(QSqlDatabase& db, const domain::RunId& run);

// One saved revision of a book's edited contents (tocedits.cpp).
struct StoredTocRevision {
    domain::TocRevisionId id;
    domain::BookId book;
    int number = 0;
    domain::RunId baseRun;
    QList<domain::TocEntry> entries;                     // sdkEntryId holds the entry key.
    QList<std::optional<QString>> baseSdkEntryIds;       // Parallel: the base run's entry, if any.
    QList<qint64> rowIds;                                // Parallel: toc_edit_entries.id.
};
domain::Result<StoredTocRevision> loadTocRevision(QSqlDatabase& db, const domain::TocRevisionId& id);

// After a new TOC run became active for a book with edited contents: if the
// new run's entries equal the edited revision's base entries in content,
// the edits carry over as a new revision on the new run. Otherwise nothing
// changes, and the book needs reconciliation. Caller holds the transaction.
domain::Status carryTocEdits(QSqlDatabase& db, const domain::BookId& book, const domain::RunId& newRun);

// publishToc() without its own transaction, with a caller-chosen run ID.
domain::Result<domain::RunId> publishTocRun(QSqlDatabase& db, const domain::RunId& id,
                                            const domain::PublishTicket& ticket, const domain::RunIdentity& run,
                                            const domain::TocAnalysis& toc);

} // namespace mbl::catalog::detail
