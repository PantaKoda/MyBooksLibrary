// Presentation: Appearance (the theme and accent) and the real
// AppearanceDialog.qml, offscreen. The accents keep text readable on them
// (WCAG 2.2 AA: 4.5:1 for the text of an accent button, 3:1 for an accent bar
// on the window), choices are remembered and applied to the colour scheme,
// unknown stored values fall back, and nothing is stored without a QSettings.
// The offscreen platform ignores colour-scheme requests, so the scheme itself
// is checked only where the platform honours them (QT_QPA_PLATFORM=windows).
#include "presentation/appearance.h"

#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <memory>

Q_IMPORT_QML_PLUGIN(MyBooksLibrary_PresentationPlugin)

using mbl::presentation::Appearance;

namespace {

double luminance(const QColor& c)
{
    const auto channel = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

double contrast(const QColor& a, const QColor& b)
{
    const double x = luminance(a);
    const double y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

QQuickItem* findItem(QQuickItem* root, const QString& name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItem(child, name))
            return found;
    }
    return nullptr;
}

} // namespace

class TestAppearance : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void accentsKeepTextReadable();
    void choicesAreRememberedAndApplied();
    void unknownStoredValuesFallBack();
    void overridesNeitherReadNorStore();
    void theDialogChangesTheChoice();

private:
    void checkScheme(Qt::ColorScheme expected) const;

    QTemporaryDir m_dir;
    bool m_schemeApplies = false;
    QString iniPath(const char* name) const { return m_dir.filePath(QString::fromLatin1(name)); }
};

void TestAppearance::initTestCase()
{
    QStyleHints* hints = QGuiApplication::styleHints();
    hints->setColorScheme(Qt::ColorScheme::Dark);
    m_schemeApplies = hints->colorScheme() == Qt::ColorScheme::Dark;
    hints->unsetColorScheme();
    qInfo("colour scheme requests %s on %s", m_schemeApplies ? "apply" : "are ignored",
          qPrintable(QGuiApplication::platformName()));
}

void TestAppearance::checkScheme(Qt::ColorScheme expected) const
{
    if (m_schemeApplies)
        QCOMPARE(QGuiApplication::styleHints()->colorScheme(), expected);
}

void TestAppearance::accentsKeepTextReadable()
{
    QVERIFY(Appearance::presets().size() >= 4);
    bool hasSystem = false;
    for (const Appearance::Preset& preset : Appearance::presets()) {
        if (preset.id == QLatin1String(Appearance::systemAccent)) {
            hasSystem = true;
            continue;
        }
        const auto check = [&](const QColor& accent, const QColor& other, double minimum, const char* what) {
            const double ratio = contrast(accent, other);
            QVERIFY2(ratio >= minimum, qPrintable(QStringLiteral("%1: %2 %3 against %4: %5:1")
                                                      .arg(preset.id, QString::fromLatin1(what), accent.name(), other.name())
                                                      .arg(ratio, 0, 'f', 2)));
        };
        // Accent buttons: white text in light, black text in dark (FluentWinUI3).
        check(preset.light, Qt::white, 4.5, "button text");
        check(preset.dark, Qt::black, 4.5, "button text");
        // A selection bar or focus ring on the window (#f3f3f3, #202020).
        check(preset.light, QColor(0xf3, 0xf3, 0xf3), 3.0, "on the window");
        check(preset.dark, QColor(0x20, 0x20, 0x20), 3.0, "on the window");
    }
    QVERIFY(hasSystem);
    QVERIFY(std::any_of(Appearance::presets().cbegin(), Appearance::presets().cend(),
                        [](const Appearance::Preset& p) { return p.id == Appearance::defaultAccent(); }));
}

void TestAppearance::choicesAreRememberedAndApplied()
{
    {
        QSettings settings(iniPath("remembered.ini"), QSettings::IniFormat);
        Appearance appearance(&settings);
        QCOMPARE(appearance.theme(), Appearance::System);
        QCOMPARE(appearance.accent(), Appearance::defaultAccent());
        appearance.setTheme(Appearance::Dark);
        checkScheme(Qt::ColorScheme::Dark);
        appearance.setAccent(QStringLiteral("teal"));
        QCOMPARE(appearance.accentLight(), QColor(0x00, 0x70, 0x7c));
        QCOMPARE(appearance.accentDark(), QColor(0x4f, 0xd1, 0xd9));
    }
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    {
        // The next start: the scheme is applied again before any window.
        QSettings settings(iniPath("remembered.ini"), QSettings::IniFormat);
        Appearance appearance(&settings);
        QCOMPARE(appearance.theme(), Appearance::Dark);
        QCOMPARE(appearance.accent(), QStringLiteral("teal"));
        checkScheme(Qt::ColorScheme::Dark);

        // Light, then back to the system's setting (no override left).
        appearance.setTheme(Appearance::Light);
        checkScheme(Qt::ColorScheme::Light);
        appearance.setTheme(Appearance::System);
        QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("system"));
    }
    // "Windows accent" is the system's colour in both schemes.
    Appearance appearance(nullptr);
    appearance.setAccent(QString::fromLatin1(Appearance::systemAccent));
    const QColor system = QGuiApplication::palette().color(QPalette::Accent);
    QCOMPARE(appearance.accentLight(), system);
    QCOMPARE(appearance.accentDark(), system);
}

void TestAppearance::unknownStoredValuesFallBack()
{
    QSettings settings(iniPath("edited.ini"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("purple"));
    settings.setValue(QStringLiteral("appearance/accent"), QStringLiteral("#ff0000"));
    Appearance appearance(&settings);
    QCOMPARE(appearance.theme(), Appearance::System);
    QCOMPARE(appearance.accent(), Appearance::defaultAccent());
    // Only presets can be chosen.
    appearance.setAccent(QStringLiteral("#00ff00"));
    QCOMPARE(appearance.accent(), Appearance::defaultAccent());
}

// main.cpp's start-up: without development options the stored choice is
// used and kept up to date; with --color-scheme or --accent it is neither
// read nor overwritten, and an unknown accent id is reported.
void TestAppearance::overridesNeitherReadNorStore()
{
    QSettings settings(iniPath("startup.ini"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("dark"));
    settings.setValue(QStringLiteral("appearance/accent"), QStringLiteral("teal"));
    const QStringList app{QStringLiteral("appMyBooksLibrary")};
    QString warning;

    {
        const auto appearance = Appearance::fromArguments(settings, app, &warning);
        QCOMPARE(appearance->theme(), Appearance::Dark);
        QCOMPARE(appearance->accent(), QStringLiteral("teal"));
        QVERIFY(warning.isEmpty());
    }
    {
        const auto appearance = Appearance::fromArguments(
            settings, app + QStringList{QStringLiteral("--accent"), QStringLiteral("rose")}, &warning);
        QCOMPARE(appearance->theme(), Appearance::System);  // Not read.
        QCOMPARE(appearance->accent(), QStringLiteral("rose"));
        appearance->setTheme(Appearance::Light);
        appearance->setAccent(QStringLiteral("violet"));
        QVERIFY(warning.isEmpty());
    }
    {
        const auto appearance = Appearance::fromArguments(
            settings, app + QStringList{QStringLiteral("--color-scheme"), QStringLiteral("light")}, &warning);
        QCOMPARE(appearance->theme(), Appearance::Light);
        QCOMPARE(appearance->accent(), Appearance::defaultAccent());  // Not read.
        appearance->setTheme(Appearance::System);
    }
    {
        const auto appearance = Appearance::fromArguments(
            settings, app + QStringList{QStringLiteral("--accent"), QStringLiteral("blue")}, &warning);
        QCOMPARE(appearance->accent(), Appearance::defaultAccent());
        QVERIFY2(warning.contains(QLatin1String("\"blue\"")) && warning.contains(QLatin1String("lapis"))
                     && warning.contains(QLatin1String("graphite")),
                 qPrintable(warning));
    }
    // The user's stored choice is untouched.
    settings.sync();
    QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(settings.value(QStringLiteral("appearance/accent")).toString(), QStringLiteral("teal"));
    QGuiApplication::styleHints()->unsetColorScheme();
}

void TestAppearance::theDialogChangesTheChoice()
{
    QSettings settings(iniPath("dialog.ini"), QSettings::IniFormat);
    Appearance appearance(&settings);
    Appearance::setInstance(&appearance);

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/theme/AppearanceDialog.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(480, 360);  // Main.qml's minimum size.
    std::unique_ptr<QObject> dialog(component.createWithInitialProperties(
        {{QStringLiteral("parent"), QVariant::fromValue<QObject*>(window.contentItem())}}));
    QVERIFY2(dialog, qPrintable(component.errorString()));
    window.show();
    // Laid out (the swatches placed) only once the window is exposed.
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "open"));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    auto* content = qvariant_cast<QQuickItem*>(dialog->property("contentItem"));
    const auto click = [&](const char* name) {
        QQuickItem* button = findItem(content, QString::fromLatin1(name));
        QVERIFY2(button && button->isVisible(), name);
        QVERIFY(QMetaObject::invokeMethod(button, "click"));
    };

    // The current choice is shown checked.
    QVERIFY(findItem(content, QStringLiteral("themeSystem"))->property("checked").toBool());
    QVERIFY(findItem(content, QStringLiteral("accent_lapis"))->property("checked").toBool());

    click("themeDark");
    QCOMPARE(appearance.theme(), Appearance::Dark);
    checkScheme(Qt::ColorScheme::Dark);
    click("accent_violet");
    QCOMPARE(appearance.accent(), QStringLiteral("violet"));
    QTRY_VERIFY(findItem(content, QStringLiteral("accent_violet"))->property("checked").toBool());
    QVERIFY(!findItem(content, QStringLiteral("accent_lapis"))->property("checked").toBool());

    // Clicking the chosen accent again, with the mouse and with Space, keeps
    // it chosen: the ring stays and it stays checked for assistive technology.
    // Dark can change Windows' accent shade, which rebuilds the swatches:
    // wait until the current ones are placed (violet is the third).
    QTRY_VERIFY(findItem(content, QStringLiteral("accent_violet"))->x() > 0);
    QQuickItem* chosen = findItem(content, QStringLiteral("accent_violet"));
    const QPoint centre = chosen->mapToScene(QPointF(chosen->width() / 2, chosen->height() / 2)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centre);
    QTest::qWait(50);
    QVERIFY(chosen->property("checked").toBool());
    chosen->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Space);
    QTest::qWait(50);
    QVERIFY(chosen->property("checked").toBool());
    QCOMPARE(appearance.accent(), QStringLiteral("violet"));
    // A mouse click on another accent chooses it.
    QQuickItem* rose = findItem(content, QStringLiteral("accent_rose"));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      rose->mapToScene(QPointF(rose->width() / 2, rose->height() / 2)).toPoint());
    QTRY_COMPARE(appearance.accent(), QStringLiteral("rose"));
    QVERIFY(rose->property("checked").toBool());
    QVERIFY(!chosen->property("checked").toBool());
    click("accent_violet");
    QTRY_VERIFY(chosen->property("checked").toBool());
    QCOMPARE(findItem(content, QStringLiteral("accentNameLabel"))->property("text").toString(), QStringLiteral("Violet"));
    QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(settings.value(QStringLiteral("appearance/accent")).toString(), QStringLiteral("violet"));

    // The Theme singleton follows: its accent is violet, the dark-scheme one
    // where the platform applied Dark.
    QObject* theme = engine.singletonInstance<QObject*>("MyBooksLibrary.Presentation", "Theme");
    QVERIFY(theme);
    QTRY_COMPARE(theme->property("accent").value<QColor>(),
                 m_schemeApplies ? QColor(0xb8, 0xa2, 0xff) : QColor(0x69, 0x43, 0xc9));

    // For the PR: MBL_SCREENSHOT_DIR=<folder> saves the dialog.
    if (const QString shots = qEnvironmentVariable("MBL_SCREENSHOT_DIR"); !shots.isEmpty()) {
        QTest::qWait(200);
        window.grabWindow().save(QDir(shots).filePath(QStringLiteral("appearance-dialog.png")));
    }

    click("themeSystem");
    QCOMPARE(appearance.theme(), Appearance::System);
    Appearance::setInstance(nullptr);
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    TestAppearance test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appearance.moc"
