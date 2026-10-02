// Presentation (issue #30): which library this window holds, and opening
// another one, for the window's Library menu (Open library…, Open the default
// library) and its title. Another library is always started in a new
// MyBooksLibrary process, never swapped into this session (DECISIONS.md, M10
// part 3): "instead of this library" is that new process plus this window's
// normal closing flow. Each library has its own writer lock, so two windows
// on two libraries work, and a second window on the same library is refused.
//
// A chosen folder is checked first, off the GUI thread, and never created:
// it must be an existing library (catalog::Library::checkExisting), not a
// backup, and not the library this window already holds. The new process
// gets --existing-library, so it refuses the folder too if it has changed
// since.
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>

#include <functional>

namespace mbl::presentation {

class LibrarySwitcher : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The library session owns the switcher.")
    // The library this window holds (while opening or open), for the title and
    // the toolbar: its folder, its name, and how it was chosen.
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY currentChanged)
    Q_PROPERTY(bool currentIsDefault READ currentIsDefault NOTIFY currentChanged)
    Q_PROPERTY(QString defaultPath READ defaultPath NOTIFY currentChanged)
    // Opening another library: a folder being checked, and why it was refused.
    Q_PROPERTY(bool checking READ checking NOTIFY requestChanged)
    Q_PROPERTY(QString error READ error NOTIFY requestChanged)

public:
    // Starts `program` with `arguments` as a separate process; false if it
    // could not be started. Tests substitute one that records the call.
    using Launcher = std::function<bool(const QString& program, const QStringList& arguments)>;

    explicit LibrarySwitcher(QObject* parent = nullptr);
    ~LibrarySwitcher() override;  // Waits for a folder check (a few file lookups).

    // Composition root: the default library folder, and how this window's
    // library was chosen ("default", "command line" or "environment", as
    // app::resolveLibraryRoot says).
    void setStartup(const QString& defaultPath, const QString& source);
    void setLauncher(Launcher launcher);
    // The library session: the folder this window holds, and whether it holds
    // it (opening or open) or failed to open it.
    void setCurrent(const QString& path, bool held);

    // Checks `folderOrUrl` (a path or a folder dialog's URL), then starts it
    // in a new process. On success started() is emitted; with `newWindow`
    // false, the window then closes itself. On a refusal, error says why and
    // nothing is started.
    Q_INVOKABLE void openFolder(const QString& folderOrUrl, bool newWindow);
    // Starts the default library (created there if it does not exist yet, as
    // on a first start). Refused when this window already holds it.
    Q_INVOKABLE void openDefault(bool newWindow);
    Q_INVOKABLE void clearError();
    // A folder dialog's URL as a path to show (native separators).
    Q_INVOKABLE QString localPath(const QUrl& url) const;

    QString currentPath() const;
    QString currentName() const;
    QString sourceText() const;
    bool currentIsDefault() const;
    QString defaultPath() const;
    bool checking() const { return m_checking; }
    QString error() const { return m_error; }

signals:
    void currentChanged();
    void requestChanged();
    // Another library was started; `newWindow` false: close this window.
    void started(bool newWindow, const QString& folder);

private:
    void launch(const QString& folder, const QStringList& arguments, bool newWindow);
    void refuse(const QString& reason);

    QThreadPool m_pool;  // Folder checks, off the GUI thread.
    Launcher m_launcher;
    QString m_defaultPath;
    QString m_source;
    QString m_currentPath;
    bool m_currentHeld = false;
    bool m_checking = false;
    QString m_error;
};

} // namespace mbl::presentation
