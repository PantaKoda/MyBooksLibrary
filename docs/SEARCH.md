# Search (A3)

Search covers **effective bibliographic fields and table-of-contents titles**. It is not full-book text search.

## Projections

| FTS5 table | One row per | Indexed | Stored alongside (unindexed) |
| --- | --- | --- | --- |
| `search_books` | active book | `title` (effective title and subtitle, or the file-name fallback), `contributors` (effective names) | `book_id`, `display_title` |
| `search_toc` | TOC entry of the active run of an active book | `title` | `book_id`, `entry_key`, order, printed label, destination state/page, source TOC page, in-export-plan flag |

Tokenizer: `unicode61 remove_diacritics 2 tokenchars '+#'`. Matching is case-insensitive and ignores diacritics ("edition" finds "Édition"). `+` and `#` are part of tokens, so `C++` and `C#` do not match plain `C`.

The projections are derived data. A2 updates them inside the same transaction as the catalog change (publication, override, trash, restore), on the same connection. `catalog::rebuildSearchIndex` recreates them from catalog tables.

## Query syntax (`search::compileQuery`)

User text is never passed to FTS5 as syntax.

| Input | Meaning |
| --- | --- |
| `tcp sockets` | Both terms must match (AND). |
| `TCP/IP`, `HTTP/2`, `C++`, `C#` | Each whitespace-separated term is quoted. `TCP/IP` is the phrase *tcp ip*; `C++` is the token *c++*. |
| `"network stack"` | Text in double quotes is one phrase. An unclosed quote runs to the end of the text. |
| `prog*` | A trailing `*` on an unquoted term is a prefix search. |
| `AND`, `OR`, `NOT`, `NEAR`, `-`, `:`, `^`, `(`, `)` | Plain text, not operators. |
| Only punctuation (`- / :`) | Nothing searchable: `SearchResponse.queryEmpty`. |

## Scopes

`All` searches titles, contributors and contents. `Titles` and `Contributors` search only that metadata column. `Contents` searches only TOC titles.

## Results and ranking

Hits are grouped by book:

1. **Tier 0**: the book's metadata matched. Ordered by the metadata `bm25` (title weighted 10, contributors 4). Ties go to books that also match in contents, by their best chapter rank.
2. **Tier 1**: only contents matched. Ordered by the **best** chapter `bm25`.

Display titles of contents-only books come from **one** read of `search_books` per search. `book_id` is an UNINDEXED FTS column, so a per-book equality lookup would scan the table once per matching book. Ranks from the two indexes are never compared with each other. Each TOC entry is its own FTS row, so a long TOC gains nothing from having many matches. Remaining ties are ordered by display title, then book ID.

Each book lists at most `chapterHitsPerBook` chapter hits (best first) and reports `totalChapterMatches`. Results are paginated by book (`offset`, `limit`). `SearchRequest.generation` is echoed in the response so that callers can drop stale responses (debouncing is a presentation concern, M06).

Chapter hits include unresolved, ambiguous and export-omitted entries. Only a `Resolved` hit carries `destinationPage` (zero-based; show it as index + 1). Other hits carry `sourceTocPage` when known. No page is ever guessed.

Suggested empty-state wording: "No matches in indexed titles and contents."
