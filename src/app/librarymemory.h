// Composition (issue #30, part 3): the library the user last opened from the
// app, so that the next start opens it again rather than the default one.
// Kept in QSettings (on Windows HKCU\Software\MyBooksLibrary\MyBooksLibrary,
// value library/last). The composition root owns the QSettings; tests pass
// one backed by a temporary INI file, so they never touch the registry.
//
// Only a library the user chose in the app is remembered (Open library…,
// Open the default library, Open restored library: those starts carry
// --remember), and only once it has opened: a failed open never becomes the
// start-up library, and a --library shortcut never replaces the everyday one.
#pragma once

#include <QSettings>
#include <QString>

namespace mbl::presentation {
class LibraryController;
}

namespace mbl::app {

class LibraryMemory {
public:
    explicit LibraryMemory(QSettings& settings) : m_settings(settings) {}

    // The remembered library folder, or empty.
    QString remembered() const;
    void remember(const QString& path);
    void forget();

private:
    QSettings& m_settings;
};

// Remembers `path` in `memory` once `library` has opened it (Ready), and never
// if it fails. `memory` must outlive `library`.
void rememberWhenOpened(presentation::LibraryController& library, LibraryMemory& memory, const QString& path);

} // namespace mbl::app
