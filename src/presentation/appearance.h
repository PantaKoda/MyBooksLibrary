// Presentation: the window's theme (System, Light, Dark) and accent colour,
// chosen in the Appearance dialog and remembered for the next start.
//
// The theme sets the application's colour scheme (QStyleHints), which the
// FluentWinUI3 style and the Theme singleton follow. System leaves it to
// Windows. The accent is one of a few presets, each with a light-scheme and a
// dark-scheme colour: the light one carries white text (an accent button) and
// the dark one black text, both at 4.5:1 or more (tst_appearance). Status
// colours (Theme.critical, success, caution) never follow the accent, and no
// preset uses their hues. "Windows accent" uses the system's colour.
//
// Kept in QSettings (on Windows HKCU\Software\MyBooksLibrary\MyBooksLibrary,
// values appearance/theme and appearance/accent). Only known values are
// honoured: a hand-edited one falls back to the default. Without a QSettings
// (tests, development screenshots) nothing is read or written.
//
// QML reaches the composition root's instance as the singleton Appearance
// (setInstance before the engine loads); without one, each engine gets its
// own, which stores nothing.
#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>

class QSettings;

namespace mbl::presentation {

class Appearance : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(ThemeChoice theme READ theme WRITE setTheme NOTIFY themeChanged FINAL)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged FINAL)
    // [{id, name, light, dark}], in the order the dialog shows them. Changes
    // only with the system's accent, so the dialog's swatches are not rebuilt
    // when the choice changes.
    Q_PROPERTY(QVariantList accents READ accents NOTIFY accentsChanged FINAL)
    // The chosen accent for each scheme (the system's colour for "windows").
    Q_PROPERTY(QColor accentLight READ accentLight NOTIFY accentColorsChanged FINAL)
    Q_PROPERTY(QColor accentDark READ accentDark NOTIFY accentColorsChanged FINAL)

public:
    enum ThemeChoice { System, Light, Dark };
    Q_ENUM(ThemeChoice)

    struct Preset {
        QString id;
        const char* name;  // Translated in the "Appearance" context.
        QColor light;      // Invalid: the system's accent.
        QColor dark;
    };
    static const QList<Preset>& presets();
    static QString defaultAccent();
    static constexpr const char* systemAccent = "windows";

    // `settings` may be null: then nothing is read or stored. No default
    // constructor: QML then always goes through create().
    explicit Appearance(QSettings* settings, QObject* parent = nullptr);

    // The composition root's Appearance from the command line: the stored
    // choice from `settings`, or, with --color-scheme light|dark or
    // --accent <id> (development), those instead, with nothing read or
    // stored. An unknown --accent id is reported in `warning` (with the
    // valid ids) and leaves the default accent.
    static std::unique_ptr<Appearance> fromArguments(QSettings& settings, const QStringList& args,
                                                     QString* warning = nullptr);

    // The instance QML's singleton returns. Not owned; must outlive the
    // engines. Only this instance follows a change of the system's accent
    // (one application-wide event filter, not one per instance).
    static void setInstance(Appearance* appearance);
    static Appearance* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    ThemeChoice theme() const { return m_theme; }
    void setTheme(ThemeChoice theme);
    QString accent() const { return m_accent; }
    void setAccent(const QString& id);  // An unknown id is ignored.

    QVariantList accents() const;
    QColor accentLight() const;
    QColor accentDark() const;

signals:
    void themeChanged();
    void accentChanged();
    void accentColorsChanged();
    void accentsChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyTheme();
    static QColor systemAccentColor();

    QSettings* m_settings = nullptr;
    QColor m_systemAccent;  // As last seen, to tell a real change from any palette event.
    ThemeChoice m_theme = System;
    QString m_accent;
};

} // namespace mbl::presentation
