#include "search/searchindex.h"

#include "search/ftsquery.h"

#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace mbl::search {

using domain::BookId;
using domain::DestinationState;
using domain::ErrorCode;
using domain::makeError;

namespace {

// `+` and `#` are token characters so "C++" and "C#" do not match "C".
#define MBL_FTS_TOKENIZE "tokenize = \"unicode61 remove_diacritics 2 tokenchars '+#'\""

QVariant nullable(const std::optional<int>& value)
{
    return value ? QVariant(*value) : QVariant(QMetaType(QMetaType::Int));
}

QVariant nullable(const std::optional<QString>& value)
{
    return value ? QVariant(*value) : QVariant(QMetaType(QMetaType::QString));
}

std::optional<int> optionalInt(const QVariant& value)
{
    return value.isNull() ? std::nullopt : std::optional<int>(value.toInt());
}

bool fail(QSqlQuery& query, QString* error)
{
    if (error)
        *error = query.lastError().text();
    return false;
}

} // namespace

QStringList projectionSchema()
{
    return {
        QStringLiteral("CREATE VIRTUAL TABLE search_books USING fts5("
                       "book_id UNINDEXED, display_title UNINDEXED, title, contributors, " MBL_FTS_TOKENIZE ")"),
        QStringLiteral("CREATE VIRTUAL TABLE search_toc USING fts5("
                       "book_id UNINDEXED, entry_key UNINDEXED, entry_order UNINDEXED, printed_label UNINDEXED, "
                       "destination_state UNINDEXED, destination_page UNINDEXED, source_toc_page UNINDEXED, "
                       "in_export_plan UNINDEXED, title, " MBL_FTS_TOKENIZE ")"),
    };
}

bool removeBookProjection(QSqlDatabase& db, const BookId& book, QString* error)
{
    QSqlQuery query(db);
    for (const char* table : {"search_books", "search_toc"}) {
        query.prepare(QStringLiteral("DELETE FROM %1 WHERE book_id = ?").arg(QLatin1StringView(table)));
        query.addBindValue(book.toString());
        if (!query.exec())
            return fail(query, error);
    }
    return true;
}

bool replaceBookProjection(QSqlDatabase& db, const BookProjection& book, QString* error)
{
    if (!removeBookProjection(db, book.book, error))
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO search_books(book_id, display_title, title, contributors) VALUES (?, ?, ?, ?)"));
    query.addBindValue(book.book.toString());
    query.addBindValue(book.displayTitle);
    query.addBindValue(book.titleText);
    query.addBindValue(book.contributors.join(u'\n'));
    if (!query.exec())
        return fail(query, error);

    query.prepare(QStringLiteral(
        "INSERT INTO search_toc(book_id, entry_key, entry_order, printed_label, destination_state, "
        "destination_page, source_toc_page, in_export_plan, title) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const ChapterProjection& chapter : book.chapters) {
        query.addBindValue(book.book.toString());
        query.addBindValue(chapter.entryKey);
        query.addBindValue(chapter.order);
        query.addBindValue(nullable(chapter.printedLabel));
        query.addBindValue(domain::toCode(chapter.destinationState));
        query.addBindValue(nullable(chapter.destinationPage));
        query.addBindValue(nullable(chapter.sourceTocPage));
        query.addBindValue(chapter.inExportPlan ? 1 : 0);
        query.addBindValue(chapter.title);
        if (!query.exec())
            return fail(query, error);
    }
    return true;
}

bool clearProjections(QSqlDatabase& db, QString* error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM search_books")))
        return fail(query, error);
    if (!query.exec(QStringLiteral("DELETE FROM search_toc")))
        return fail(query, error);
    return true;
}

domain::Result<domain::SearchResponse> search(QSqlDatabase& db, const domain::SearchRequest& request)
{
    domain::SearchResponse response;
    response.generation = request.generation;

    const CompiledQuery compiled = compileQuery(request.text);
    if (compiled.isEmpty()) {
        response.queryEmpty = true;
        return response;
    }

    struct Group {
        domain::BookHit hit;
        std::optional<double> metadataRank;  // bm25 over search_books (lower is better).
        std::optional<double> chapterRank;   // Best bm25 over search_toc.
    };
    QHash<QString, Group> groups;

    const auto sqlFailure = [](QSqlQuery& query) {
        return makeError(ErrorCode::Database, QStringLiteral("Search failed: %1").arg(query.lastError().text()));
    };

    // Metadata index: columns book_id, display_title, title, contributors.
    if (request.scope != domain::SearchScope::Contents) {
        QString columns;
        switch (request.scope) {
        case domain::SearchScope::Titles: columns = QStringLiteral("{title}"); break;
        case domain::SearchScope::Contributors: columns = QStringLiteral("{contributors}"); break;
        default: columns = QStringLiteral("{title contributors}"); break;
        }
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT book_id, display_title, bm25(search_books, 0.0, 0.0, 10.0, 4.0) AS r "
            "FROM search_books WHERE search_books MATCH ? ORDER BY r"));
        query.addBindValue(QStringLiteral("%1 : (%2)").arg(columns, compiled.expression));
        if (!query.exec())
            return sqlFailure(query);
        while (query.next()) {
            Group& group = groups[query.value(0).toString()];
            group.hit.book = BookId::fromString(query.value(0).toString());
            group.hit.displayTitle = query.value(1).toString();
            group.hit.metadataMatch = true;
            group.metadataRank = query.value(2).toDouble();
        }
    }

    // Contents index: one row per TOC entry, so a long TOC gains no advantage.
    if (request.scope == domain::SearchScope::All || request.scope == domain::SearchScope::Contents) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT book_id, entry_key, entry_order, printed_label, destination_state, destination_page, "
            "source_toc_page, in_export_plan, title, bm25(search_toc) AS r "
            "FROM search_toc WHERE search_toc MATCH ? ORDER BY r, entry_order"));
        query.addBindValue(QStringLiteral("{title} : (%1)").arg(compiled.expression));
        if (!query.exec())
            return sqlFailure(query);
        while (query.next()) {
            Group& group = groups[query.value(0).toString()];
            group.hit.book = BookId::fromString(query.value(0).toString());
            const double rank = query.value(9).toDouble();
            if (!group.chapterRank || rank < *group.chapterRank)
                group.chapterRank = rank;
            ++group.hit.totalChapterMatches;
            if (group.hit.chapters.size() < request.chapterHitsPerBook) {
                domain::ChapterHit chapter;
                chapter.entryKey = query.value(1).toLongLong();
                chapter.order = query.value(2).toInt();
                if (!query.value(3).isNull())
                    chapter.printedLabel = query.value(3).toString();
                chapter.destinationState = domain::destinationStateFromCode(query.value(4).toString())
                                               .value_or(DestinationState::Unresolved);
                chapter.destinationPage = optionalInt(query.value(5));
                chapter.sourceTocPage = optionalInt(query.value(6));
                chapter.inExportPlan = query.value(7).toInt() != 0;
                chapter.title = query.value(8).toString();
                group.hit.chapters << chapter;
            }
        }
    }

    // Within one collection: drop books outside it (membership is catalog
    // data, read here only). Ranks are unaffected.
    if (request.collection && !groups.isEmpty()) {
        QSqlQuery members(db);
        members.prepare(QStringLiteral("SELECT book_id FROM collection_books WHERE collection_id = ?"));
        members.addBindValue(request.collection->toString());
        if (!members.exec())
            return sqlFailure(members);
        QSet<QString> inCollection;
        while (members.next())
            inCollection.insert(members.value(0).toString());
        for (auto it = groups.begin(); it != groups.end();) {
            if (inCollection.contains(it.key()))
                ++it;
            else
                it = groups.erase(it);
        }
    }

    // Display titles for contents-only matches: one read of the book
    // projection. book_id is UNINDEXED, so a per-book equality lookup would
    // scan the table once per matching book.
    const bool needTitles = std::any_of(groups.cbegin(), groups.cend(),
                                        [](const Group& g) { return !g.hit.metadataMatch; });
    if (needTitles) {
        QSqlQuery titles(db);
        if (!titles.exec(QStringLiteral("SELECT book_id, display_title FROM search_books")))
            return sqlFailure(titles);
        while (titles.next()) {
            const auto it = groups.find(titles.value(0).toString());
            if (it != groups.end() && !it->hit.metadataMatch)
                it->hit.displayTitle = titles.value(1).toString();
        }
    }
    QList<Group> ordered;
    ordered.reserve(groups.size());
    for (Group& group : groups) {
        group.hit.tier = group.hit.metadataMatch ? 0 : 1;
        ordered << group;
    }

    // Tier first; within a tier compare only ranks from the same index.
    std::sort(ordered.begin(), ordered.end(), [](const Group& a, const Group& b) {
        if (a.hit.tier != b.hit.tier)
            return a.hit.tier < b.hit.tier;
        const auto primary = [](const Group& g) { return g.hit.tier == 0 ? g.metadataRank : g.chapterRank; };
        if (primary(a) != primary(b))
            return primary(a).value_or(0) < primary(b).value_or(0);
        const bool aChapter = a.chapterRank.has_value();
        const bool bChapter = b.chapterRank.has_value();
        if (aChapter != bChapter)
            return aChapter;  // Also matching contents ranks higher.
        if (aChapter && *a.chapterRank != *b.chapterRank)
            return *a.chapterRank < *b.chapterRank;
        const int byTitle = QString::localeAwareCompare(a.hit.displayTitle, b.hit.displayTitle);
        if (byTitle != 0)
            return byTitle < 0;
        return a.hit.book < b.hit.book;
    });

    response.totalBooks = int(ordered.size());
    const int from = std::clamp(request.offset, 0, response.totalBooks);
    const int to = std::clamp(from + std::max(request.limit, 0), from, response.totalBooks);
    for (int i = from; i < to; ++i)
        response.books << ordered.at(i).hit;
    return response;
}

} // namespace mbl::search
