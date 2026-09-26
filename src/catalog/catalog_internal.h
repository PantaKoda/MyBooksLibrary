// A2 internal helpers shared by the catalog translation units. Not part of
// the catalog's public contract.
#pragma once

#include "domain/book.h"
#include "domain/result.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

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

// publishMetadata() without its own transaction, with a caller-chosen run ID
// (so the run's report file can be named before publication).
domain::Result<domain::RunId> publishMetadataRun(QSqlDatabase& db, const domain::RunId& id,
                                                 const domain::PublishTicket& ticket,
                                                 const domain::RunIdentity& run,
                                                 const domain::ExtractedMetadata& metadata);

} // namespace mbl::catalog::detail
