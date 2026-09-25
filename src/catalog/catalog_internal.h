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

} // namespace mbl::catalog::detail
