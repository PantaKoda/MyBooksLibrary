// A3: derived FTS5 projections of effective metadata and TOC titles, and the
// search query over them. The projections are rebuildable from the catalog.
//
// Write functions never open or commit a transaction: A2 calls them inside
// the catalog transaction that changes the underlying data, on the shared
// database connection.
#pragma once

#include "domain/ids.h"
#include "domain/result.h"
#include "domain/search.h"
#include "domain/toc.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <optional>

namespace mbl::search {

// DDL for the projections; A2 includes it in its migrations.
QStringList projectionSchema();

struct ChapterProjection {
    qint64 entryKey = 0;  // toc_entries.id, or toc_edit_entries.id for edited contents
    QString title;
    int order = 0;
    std::optional<QString> printedLabel;
    domain::DestinationState destinationState = domain::DestinationState::Unresolved;
    std::optional<int> destinationPage;
    std::optional<int> sourceTocPage;
    bool inExportPlan = false;
};

struct BookProjection {
    domain::BookId book;
    QString displayTitle;        // Shown in results.
    QString titleText;           // Indexed: effective title and subtitle, or the file name.
    QStringList contributors;    // Indexed: effective contributor names, in order.
    QList<ChapterProjection> chapters;
};

// Replaces every projection row of the book. Returns false and sets `error`
// on SQL failure; the caller must roll back.
bool replaceBookProjection(QSqlDatabase& db, const BookProjection& book, QString* error);
bool removeBookProjection(QSqlDatabase& db, const domain::BookId& book, QString* error);
bool clearProjections(QSqlDatabase& db, QString* error);

domain::Result<domain::SearchResponse> search(QSqlDatabase& db, const domain::SearchRequest& request);

} // namespace mbl::search
