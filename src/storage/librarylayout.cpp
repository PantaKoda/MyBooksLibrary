#include "storage/librarylayout.h"

#include <QDir>

namespace mbl::storage {

LibraryLayout::LibraryLayout(QString root) : m_root(QDir::cleanPath(QDir(root).absolutePath())) {}

QString LibraryLayout::absolute(const QString& relativePath) const
{
    return QDir(m_root).filePath(relativePath);
}

QString LibraryLayout::managedSourcePath(const domain::AssetId& asset)
{
    return QStringLiteral("files/%1/source.pdf").arg(asset.toString());
}

QString LibraryLayout::stagingDirectory(const domain::ImportId& import)
{
    return QStringLiteral("staging/%1").arg(import.toString());
}

QString LibraryLayout::stagedSourcePath(const domain::ImportId& import)
{
    return stagingDirectory(import) + QStringLiteral("/source.pdf");
}

bool LibraryLayout::ensureDirectories(QString* error) const
{
    for (const char* dir : {"files", "staging", "reports", "derivatives", "cache"}) {
        if (!QDir(m_root).mkpath(QLatin1StringView(dir))) {
            if (error)
                *error = QStringLiteral("Cannot create %1.").arg(absolute(QLatin1StringView(dir)));
            return false;
        }
    }
    return true;
}

} // namespace mbl::storage
