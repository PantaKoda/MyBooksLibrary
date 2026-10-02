#include "app/librarymemory.h"

#include "presentation/librarycontroller.h"

#include <QDir>

namespace mbl::app {

namespace {

const QString kKey = QStringLiteral("library/last");

} // namespace

QString LibraryMemory::remembered() const
{
    return m_settings.value(kKey).toString();
}

void LibraryMemory::remember(const QString& path)
{
    m_settings.setValue(kKey, QDir::cleanPath(QDir(path).absolutePath()));
    m_settings.sync();
}

void LibraryMemory::forget()
{
    m_settings.remove(kKey);
    m_settings.sync();
}

void rememberWhenOpened(presentation::LibraryController& library, LibraryMemory& memory, const QString& path)
{
    using presentation::LibraryController;
    // Once: the first time the library is ready. A failed open ends it too.
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(&library, &LibraryController::stateChanged, &library,
                                   [&library, &memory, path, connection] {
                                       if (library.ready())
                                           memory.remember(path);
                                       if (library.ready() || library.failed())
                                           QObject::disconnect(*connection);
                                   });
}

} // namespace mbl::app
