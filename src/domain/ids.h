// Stable application identifiers. Each kind of ID is a distinct type so a
// BookId cannot be passed where an AssetId is expected. Stored as lower-case
// UUID text without braces. A model row number is never an identifier.
#pragma once

#include <QHashFunctions>
#include <QMetaType>
#include <QString>
#include <QUuid>

namespace mbl::domain {

template <typename Tag>
class Id {
public:
    Id() = default;
    explicit Id(QUuid value) : m_value(value) {}

    static Id create() { return Id(QUuid::createUuid()); }
    static Id fromString(const QString& text) { return Id(QUuid::fromString(text)); }

    bool isNull() const { return m_value.isNull(); }
    QUuid uuid() const { return m_value; }
    QString toString() const { return m_value.toString(QUuid::WithoutBraces); }

    friend bool operator==(const Id& a, const Id& b) { return a.m_value == b.m_value; }
    friend bool operator!=(const Id& a, const Id& b) { return a.m_value != b.m_value; }
    friend bool operator<(const Id& a, const Id& b) { return a.m_value < b.m_value; }
    friend size_t qHash(const Id& id, size_t seed = 0) { return qHash(id.m_value, seed); }

private:
    QUuid m_value;
};

using BookId = Id<struct BookTag>;
using AssetId = Id<struct AssetTag>;
using RunId = Id<struct RunTag>;  // One metadata or TOC analysis run.
using ImportId = Id<struct ImportTag>;  // One import operation.
using JobId = Id<struct JobTag>;        // One processing job.
using TocRevisionId = Id<struct TocRevisionTag>;  // One saved revision of a book's edited contents.

} // namespace mbl::domain

Q_DECLARE_METATYPE(mbl::domain::BookId)
Q_DECLARE_METATYPE(mbl::domain::RunId)
Q_DECLARE_METATYPE(mbl::domain::JobId)
Q_DECLARE_METATYPE(mbl::domain::TocRevisionId)
