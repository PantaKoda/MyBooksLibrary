#include "search/ftsquery.h"

namespace mbl::search {

namespace {

bool isSearchable(const QString& term)
{
    for (const QChar c : term) {
        if (c.isLetterOrNumber() || c == u'+' || c == u'#')
            return true;
    }
    return false;
}

QString quoted(const QString& term)
{
    QString escaped = term;
    escaped.replace(u'"', QStringLiteral("\"\""));
    return u'"' + escaped + u'"';
}

} // namespace

CompiledQuery compileQuery(const QString& userText)
{
    CompiledQuery out;
    QStringList parts;

    const auto accept = [&](QString term, bool prefix) {
        term = term.trimmed();
        if (!isSearchable(term))
            return;
        out.terms << (prefix ? term + u'*' : term);
        parts << (prefix ? quoted(term) + u'*' : quoted(term));
    };

    const QString text = userText.normalized(QString::NormalizationForm_C);
    qsizetype i = 0;
    const qsizetype n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c == u'"') {
            const qsizetype close = text.indexOf(u'"', i + 1);
            const qsizetype end = close < 0 ? n : close;
            accept(text.mid(i + 1, end - i - 1), false);
            i = end + 1;
            continue;
        }
        qsizetype end = i;
        while (end < n && !text.at(end).isSpace() && text.at(end) != u'"')
            ++end;
        QString term = text.mid(i, end - i);
        bool prefix = false;
        if (term.endsWith(u'*')) {
            while (term.endsWith(u'*'))
                term.chop(1);
            prefix = true;
        }
        accept(term, prefix && isSearchable(term));
        i = end;
    }

    out.expression = parts.join(u' ');
    return out;
}

} // namespace mbl::search
