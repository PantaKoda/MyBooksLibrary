// A3 query compilation: user text never becomes unexpected FTS5 syntax.
#include "search/ftsquery.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>

using mbl::search::compileQuery;

class TestFtsQuery : public QObject {
    Q_OBJECT

private slots:
    void compiles_data();
    void compiles();
    void everyExpressionIsValidFts5_data();
    void everyExpressionIsValidFts5();
};

void TestFtsQuery::compiles_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expression");

    QTest::newRow("plain") << "networking" << "\"networking\"";
    QTest::newRow("two terms AND") << "tcp  sockets" << "\"tcp\" \"sockets\"";
    QTest::newRow("c++") << "C++" << "\"C++\"";
    QTest::newRow("c#") << "C#" << "\"C#\"";
    QTest::newRow("tcp/ip") << "TCP/IP" << "\"TCP/IP\"";
    QTest::newRow("http/2") << "HTTP/2" << "\"HTTP/2\"";
    QTest::newRow("prefix") << "prog*" << "\"prog\"*";
    QTest::newRow("phrase") << "\"network stack\"" << "\"network stack\"";
    QTest::newRow("unclosed phrase") << "\"network stack" << "\"network stack\"";
    QTest::newRow("operators are text") << "a AND NOT b" << "\"a\" \"AND\" \"NOT\" \"b\"";
    QTest::newRow("syntax chars") << "title:(x) ^y -z" << "\"title:(x)\" \"^y\" \"-z\"";
    QTest::newRow("embedded quote") << "O\"Reilly" << "\"O\" \"Reilly\"";
    QTest::newRow("only punctuation") << "- / : ()" << "";
    QTest::newRow("lone star") << "*" << "";
    QTest::newRow("empty") << "   " << "";
    QTest::newRow("greek") << "Δίκτυα" << "\"Δίκτυα\"";
}

void TestFtsQuery::compiles()
{
    QFETCH(QString, input);
    QFETCH(QString, expression);
    QCOMPARE(compileQuery(input).expression, expression);
}

void TestFtsQuery::everyExpressionIsValidFts5_data()
{
    compiles_data();
}

// Each compiled expression must be accepted by the real FTS5 parser.
void TestFtsQuery::everyExpressionIsValidFts5()
{
    QFETCH(QString, input);
    const auto compiled = compileQuery(input);
    if (compiled.isEmpty())
        return;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fts"));
        db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE VIRTUAL TABLE t USING fts5(title, tokenize = \"unicode61 remove_diacritics 2 tokenchars '+#'\")")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO t(title) VALUES ('C++ and C# over TCP/IP')")));
        q.prepare(QStringLiteral("SELECT count(*) FROM t WHERE t MATCH ?"));
        q.addBindValue(QStringLiteral("{title} : (%1)").arg(compiled.expression));
        QVERIFY2(q.exec(), qPrintable(compiled.expression));
        q.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("fts"));
}

QTEST_GUILESS_MAIN(TestFtsQuery)
#include "tst_ftsquery.moc"
