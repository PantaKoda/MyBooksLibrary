#include "presentation/appearance.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QPointer>
#include <QSettings>
#include <QStyleHints>
#include <QVariantMap>

namespace mbl::presentation {

namespace {

const auto themeKey = QStringLiteral("appearance/theme");
const auto accentKey = QStringLiteral("appearance/accent");

QPointer<Appearance> s_instance;

QString themeName(Appearance::ThemeChoice theme)
{
    switch (theme) {
    case Appearance::Light:
        return QStringLiteral("light");
    case Appearance::Dark:
        return QStringLiteral("dark");
    case Appearance::System:
        break;
    }
    return QStringLiteral("system");
}

const Appearance::Preset* findPreset(const QString& id)
{
    for (const Appearance::Preset& preset : Appearance::presets()) {
        if (preset.id == id)
            return &preset;
    }
    return nullptr;
}

} // namespace

const QList<Appearance::Preset>& Appearance::presets()
{
    // Hues apart from the status colours (red, green, yellow). Light: white
    // text on it; dark: black text on it (tst_appearance checks both).
    static const QList<Preset> list{
        {QStringLiteral("lapis"), QT_TRANSLATE_NOOP("Appearance", "Lapis blue"), QColor(0x24, 0x59, 0xc7), QColor(0x8a, 0xb4, 0xff)},
        {QStringLiteral("teal"), QT_TRANSLATE_NOOP("Appearance", "Teal"), QColor(0x00, 0x70, 0x7c), QColor(0x4f, 0xd1, 0xd9)},
        {QStringLiteral("violet"), QT_TRANSLATE_NOOP("Appearance", "Violet"), QColor(0x69, 0x43, 0xc9), QColor(0xb8, 0xa2, 0xff)},
        {QStringLiteral("rose"), QT_TRANSLATE_NOOP("Appearance", "Rose"), QColor(0xb0, 0x30, 0x6e), QColor(0xff, 0x94, 0xc2)},
        {QStringLiteral("graphite"), QT_TRANSLATE_NOOP("Appearance", "Graphite"), QColor(0x46, 0x52, 0x5f), QColor(0xb3, 0xbf, 0xcc)},
        {QString::fromLatin1(systemAccent), QT_TRANSLATE_NOOP("Appearance", "Windows accent"), QColor(), QColor()},
    };
    return list;
}

QString Appearance::defaultAccent()
{
    return QStringLiteral("lapis");
}

Appearance::Appearance(QSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings), m_accent(defaultAccent())
{
    if (m_settings) {
        const QString theme = m_settings->value(themeKey).toString();
        if (theme == themeName(Light))
            m_theme = Light;
        else if (theme == themeName(Dark))
            m_theme = Dark;
        if (const QString accent = m_settings->value(accentKey).toString(); findPreset(accent))
            m_accent = accent;
    }
    // System leaves the scheme as it is (Windows', or --color-scheme's).
    if (m_theme != System)
        applyTheme();
    // The system's accent may change while the app runs.
    if (qApp)
        qApp->installEventFilter(this);
}

void Appearance::setInstance(Appearance* appearance)
{
    s_instance = appearance;
}

Appearance* Appearance::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine)
    if (s_instance) {
        // The composition root owns it, not the engine.
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }
    Q_UNUSED(jsEngine)
    return new Appearance(nullptr);  // The engine owns it.
}

void Appearance::setTheme(ThemeChoice theme)
{
    if (theme == m_theme)
        return;
    m_theme = theme;
    applyTheme();
    if (m_settings)
        m_settings->setValue(themeKey, themeName(theme));
    emit themeChanged();
}

void Appearance::setAccent(const QString& id)
{
    if (id == m_accent || !findPreset(id))
        return;
    m_accent = id;
    if (m_settings)
        m_settings->setValue(accentKey, id);
    emit accentChanged();
    emit accentColorsChanged();
}

QVariantList Appearance::accents() const
{
    QVariantList list;
    for (const Preset& preset : presets()) {
        const bool system = !preset.light.isValid();
        list.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("name"), QCoreApplication::translate("Appearance", preset.name)},
            {QStringLiteral("light"), system ? systemAccentColor() : preset.light},
            {QStringLiteral("dark"), system ? systemAccentColor() : preset.dark},
        });
    }
    return list;
}

QColor Appearance::accentLight() const
{
    const Preset* preset = findPreset(m_accent);
    return preset && preset->light.isValid() ? preset->light : systemAccentColor();
}

QColor Appearance::accentDark() const
{
    const Preset* preset = findPreset(m_accent);
    return preset && preset->dark.isValid() ? preset->dark : systemAccentColor();
}

bool Appearance::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        emit accentColorsChanged();  // Only "Windows accent" changes, but the list shows it too.
    return QObject::eventFilter(watched, event);
}

void Appearance::applyTheme()
{
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return;
    QStyleHints* hints = QGuiApplication::styleHints();
    switch (m_theme) {
    case Light:
        hints->setColorScheme(Qt::ColorScheme::Light);
        break;
    case Dark:
        hints->setColorScheme(Qt::ColorScheme::Dark);
        break;
    case System:
        hints->unsetColorScheme();
        break;
    }
}

QColor Appearance::systemAccentColor()
{
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return {};
    return QGuiApplication::palette().color(QPalette::Accent);
}

} // namespace mbl::presentation
