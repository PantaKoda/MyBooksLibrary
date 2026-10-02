// Presentation: the Theme singleton (qml/theme/Theme.qml, UI overhaul PR 1).
// Its text colours stay readable on the FluentWinUI3 style's window and card
// backgrounds in both light and dark (WCAG 2.2 AA: 4.5:1 for text), its type
// ramp is ordered, and the views use it rather than fixed colours and sizes.
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QRegularExpression>
#include <QTest>

#include <algorithm>
#include <cmath>

Q_IMPORT_QML_PLUGIN(MyBooksLibrary_PresentationPlugin)

namespace {

// WCAG relative luminance of an opaque colour.
double luminance(const QColor& c)
{
    const auto channel = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

// `top` (possibly translucent) drawn over the opaque `bottom`.
QColor over(const QColor& top, const QColor& bottom)
{
    const double a = top.alphaF();
    return QColor::fromRgbF(float(top.redF() * a + bottom.redF() * (1 - a)),
                            float(top.greenF() * a + bottom.greenF() * (1 - a)),
                            float(top.blueF() * a + bottom.blueF() * (1 - a)));
}

double contrast(const QColor& text, const QColor& background)
{
    const double a = luminance(over(text, background));
    const double b = luminance(background);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

} // namespace

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void textIsReadableInBothSchemes_data();
    void textIsReadableInBothSchemes();
    void typeRampIsOrdered();
    void viewsUseTheTheme();
};

void TestTheme::textIsReadableInBothSchemes_data()
{
    QTest::addColumn<int>("scheme");
    // FluentWinUI3's window background (measured on its screenshots: #f3f3f3)
    // and WinUI 3's dark one.
    QTest::addColumn<QColor>("window");
    QTest::newRow("light") << int(Qt::ColorScheme::Light) << QColor(0xf3, 0xf3, 0xf3);
    QTest::newRow("dark") << int(Qt::ColorScheme::Dark) << QColor(0x20, 0x20, 0x20);
}

void TestTheme::textIsReadableInBothSchemes()
{
    QFETCH(int, scheme);
    QFETCH(QColor, window);
    QQmlEngine engine;
    QObject* theme = engine.singletonInstance<QObject*>("MyBooksLibrary.Presentation", "Theme");
    QVERIFY2(theme, "the Theme singleton is not registered");
    QVERIFY(theme->setProperty("colorScheme", scheme));
    QCOMPARE(theme->property("dark").toBool(), scheme == int(Qt::ColorScheme::Dark));

    // Text sits on the window, or on a card (the contents entry's details).
    const QColor card = over(theme->property("cardFill").value<QColor>(), window);
    const QColor selected = over(theme->property("selectedFill").value<QColor>(), window);
    for (const char* role : {"textSecondary", "critical", "success", "caution"}) {
        const QColor text = theme->property(role).value<QColor>();
        QVERIFY2(text.isValid(), role);
        for (const QColor& background : {window, card, selected}) {
            const double ratio = contrast(text, background);
            QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1 %2 on %3: %4:1")
                                                  .arg(QString::fromLatin1(role), over(text, background).name(),
                                                       background.name())
                                                  .arg(ratio, 0, 'f', 2)));
        }
    }
}

void TestTheme::typeRampIsOrdered()
{
    QQmlEngine engine;
    QObject* theme = engine.singletonInstance<QObject*>("MyBooksLibrary.Presentation", "Theme");
    QVERIFY(theme);
    const int caption = theme->property("captionSize").toInt();
    const int body = theme->property("bodySize").toInt();
    QVERIFY2(caption >= 11, "a caption smaller than 11 px is hard to read");
    QVERIFY(caption < body);
    QVERIFY(body < theme->property("bodyLargeSize").toInt());
    QVERIFY(theme->property("bodyLargeSize").toInt() < theme->property("subtitleSize").toInt());
    QVERIFY(theme->property("subtitleSize").toInt() < theme->property("titleSize").toInt());
}

// The handwritten views take colours and sizes from Theme or the palette: no
// fixed colour names or hex values, no fixed font sizes, and no selection text
// colour, which the FluentWinUI3 style does not draw behind a list row (its
// selection is a subtle fill, so that text would be white on light grey).
void TestTheme::viewsUseTheTheme()
{
    QStringList files{QStringLiteral(MBL_SOURCE_DIR "/Main.qml")};
    QDirIterator it(QStringLiteral(MBL_SOURCE_DIR "/qml"), {QStringLiteral("*.qml")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (!file.endsWith(QLatin1String("/theme/Theme.qml")))
            files << file;
    }
    QVERIFY(files.size() > 5);
    const QRegularExpression forbidden(
        QString::fromLatin1(R"re(color:\s*"(#[0-9a-fA-F]{3,8}|[a-z]+)"|font\.pixelSize:\s*\d|highlightedText)re"));
    QStringList found;
    for (const QString& file : std::as_const(files)) {
        QFile f(file);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(file));
        const QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n');
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();
            if (line.startsWith(QLatin1String("//")))
                continue;
            if (const auto m = forbidden.match(line); m.hasMatch() && m.captured(0) != QLatin1String("color: \"transparent\""))
                found << QStringLiteral("%1:%2: %3").arg(QDir(QStringLiteral(MBL_SOURCE_DIR)).relativeFilePath(file)).arg(i + 1).arg(line);
        }
    }
    QVERIFY2(found.isEmpty(), qPrintable(found.join(u'\n')));
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestTheme test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_theme.moc"
