// Search request and result contracts. Search covers metadata and TOC titles
// only; it is not full-book text search.
#pragma once

#include "domain/ids.h"
#include "domain/toc.h"

#include <QList>
#include <QString>

#include <optional>

namespace mbl::domain {

enum class SearchScope { All, Titles, Contributors, Contents };

struct SearchRequest {
    QString text;
    SearchScope scope = SearchScope::All;
    int offset = 0;                  // In books.
    int limit = 50;                  // In books.
    int chapterHitsPerBook = 5;      // Bound on chapter hits shown per book.
    quint64 generation = 0;          // Echoed so stale responses can be dropped.
};

struct ChapterHit {
    qint64 entryKey = 0;             // Catalog row of the TOC entry.
    QString title;
    int order = 0;
    std::optional<QString> printedLabel;
    DestinationState destinationState = DestinationState::Unresolved;
    std::optional<int> destinationPage;  // Open only when Resolved.
    std::optional<int> sourceTocPage;    // Offer when unresolved; never guess a page.
    bool inExportPlan = false;
};

// Tier 0: title/contributor match. Tier 1: contents-only match.
struct BookHit {
    BookId book;
    QString displayTitle;
    int tier = 1;
    bool metadataMatch = false;
    QList<ChapterHit> chapters;      // Best first, at most chapterHitsPerBook.
    int totalChapterMatches = 0;
};

struct SearchResponse {
    quint64 generation = 0;
    QList<BookHit> books;
    int totalBooks = 0;              // Before pagination.
    bool queryEmpty = false;         // Nothing searchable in the text.
};

} // namespace mbl::domain
