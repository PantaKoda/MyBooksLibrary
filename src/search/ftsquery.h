// A3: compiles user search text into a safe FTS5 query expression.
//
// Behaviour (documented in docs/SEARCH.md):
// - Whitespace separates terms; all terms must match (AND).
// - Each term is quoted as an FTS5 string, so punctuation is never query
//   syntax: "TCP/IP" becomes the phrase `"TCP/IP"` (tokens tcp, ip), and
//   `AND`, `OR`, `NOT`, `NEAR`, `-`, `:`, `^`, `(`, `)` are plain text.
// - Text inside double quotes is one phrase: "network stack".
// - A trailing `*` on an unquoted term is a prefix search: `prog*`.
// - `+` and `#` are token characters in the index, so C++ and C# stay distinct
//   from C.
// - Terms without any letter, digit, `+` or `#` are ignored.
#pragma once

#include <QString>
#include <QStringList>

namespace mbl::search {

struct CompiledQuery {
    QString expression;   // FTS5 MATCH expression; empty when nothing is searchable.
    QStringList terms;    // The accepted terms/phrases, unquoted, for display.

    bool isEmpty() const { return expression.isEmpty(); }
};

CompiledQuery compileQuery(const QString& userText);

} // namespace mbl::search
