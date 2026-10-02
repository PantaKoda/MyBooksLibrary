#include "presentation/libraryswitcher.h"

#include "catalog/library.h"
#include "storage/exportdestination.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>

namespace mbl::presentation {

namespace {

// The same folder on the real file system: letter case, "..", short names
// and links resolved (A1's check). False if either does not exist.
bool sameFolder(const QString& a, const QString& b)
{
    return storage::isInsideFolder(a, b) && storage::isInsideFolder(b, a);
}

// The same path as written, for what the window shows (no file access).
bool samePath(const QString& a, const QString& b)
{
    const Qt::CaseSensitivity cs =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QDir::fromNativeSeparators(a)).compare(QDir::cleanPath(QDir::fromNativeSeparators(b)), cs) == 0;
}

} // namespace

LibrarySwitcher::LibrarySwitcher(QObject* parent)
    : QObject(parent), m_launcher([](const QString& program, const QStringList& arguments) {
          return QProcess::startDetached(program, arguments);
      })
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("mbl-library-switcher"));
}

LibrarySwitcher::~LibrarySwitcher()
{
    m_pool.waitForDone();
}

void LibrarySwitcher::setStartup(const QString& defaultPath, const QString& source)
{
    m_defaultPath = defaultPath;
    m_source = source;
    emit currentChanged();
}

void LibrarySwitcher::setLauncher(Launcher launcher)
{
    m_launcher = std::move(launcher);
}

void LibrarySwitcher::setCurrent(const QString& path, bool held)
{
    if (m_currentPath == path && m_currentHeld == held)
        return;
    m_currentPath = path;
    m_currentHeld = held;
    emit currentChanged();
}

QString LibrarySwitcher::currentPath() const
{
    return QDir::toNativeSeparators(m_currentPath);
}

QString LibrarySwitcher::currentName() const
{
    const QString name = QFileInfo(QDir::cleanPath(QDir::fromNativeSeparators(m_currentPath))).fileName();
    return name.isEmpty() ? currentPath() : name;  // A drive's root has no name.
}

QString LibrarySwitcher::sourceText() const
{
    if (m_source == QLatin1String("default"))
        return tr("your default library");
    if (m_source == QLatin1String("command line"))
        return tr("opened with --library");
    if (m_source == QLatin1String("environment"))
        return tr("set by MYBOOKSLIBRARY_ROOT");
    return {};
}

bool LibrarySwitcher::currentIsDefault() const
{
    return !m_currentPath.isEmpty() && !m_defaultPath.isEmpty() && samePath(m_currentPath, m_defaultPath);
}

QString LibrarySwitcher::defaultPath() const
{
    return QDir::toNativeSeparators(m_defaultPath);
}

QString LibrarySwitcher::localPath(const QUrl& url) const
{
    return QDir::toNativeSeparators(url.toLocalFile());
}

void LibrarySwitcher::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit requestChanged();
}

void LibrarySwitcher::refuse(const QString& reason)
{
    m_checking = false;
    m_error = reason;
    emit requestChanged();
}

void LibrarySwitcher::openFolder(const QString& folderOrUrl, bool newWindow)
{
    if (m_checking)
        return;
    const QString trimmed = folderOrUrl.trimmed();
    const QString chosen = trimmed.startsWith(QLatin1String("file:"), Qt::CaseInsensitive) ? QUrl(trimmed).toLocalFile()
                                                                                           : trimmed;
    if (chosen.isEmpty()) {
        refuse(tr("Choose the folder of the library to open."));
        return;
    }
    if (QDir::isRelativePath(chosen)) {
        refuse(tr("Choose the library's folder with its full path."));
        return;
    }
    const QString folder = QDir::cleanPath(QDir::fromNativeSeparators(chosen));
    const QString current = m_currentHeld ? m_currentPath : QString();
    m_checking = true;
    m_error.clear();
    emit requestChanged();
    // File lookups, which can be slow (a network or removable drive), so not
    // on the GUI thread. Nothing is created or changed.
    m_pool.start([this, folder, current, newWindow] {
        QString reason;
        if (auto existing = catalog::Library::checkExisting(folder); !existing)
            reason = existing.error().message;
        else if (!current.isEmpty() && sameFolder(folder, current))
            reason = LibrarySwitcher::tr("That library is already open in this window.");
        QMetaObject::invokeMethod(this, [this, folder, reason, newWindow] {
            m_checking = false;
            if (!reason.isEmpty()) {
                refuse(reason);
                return;
            }
            launch(folder,
                   {QStringLiteral("--library"), QDir::toNativeSeparators(folder), QStringLiteral("--existing-library")},
                   newWindow);
        }, Qt::QueuedConnection);
    });
}

void LibrarySwitcher::openDefault(bool newWindow)
{
    if (m_checking)
        return;
    if (m_defaultPath.isEmpty()) {
        refuse(tr("The default library's folder is not known."));
        return;
    }
    if (m_currentHeld && currentIsDefault()) {
        refuse(tr("The default library is already open in this window."));
        return;
    }
    // Made there if it does not exist yet, as on a first start.
    launch(m_defaultPath, {QStringLiteral("--library"), QDir::toNativeSeparators(m_defaultPath)}, newWindow);
}

void LibrarySwitcher::launch(const QString& folder, const QStringList& arguments, bool newWindow)
{
    if (!m_launcher || !m_launcher(QCoreApplication::applicationFilePath(), arguments)) {
        refuse(tr("MyBooksLibrary could not be started for %1.").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    m_checking = false;
    m_error.clear();
    emit requestChanged();
    emit started(newWindow, QDir::toNativeSeparators(folder));
}

} // namespace mbl::presentation
