#include "storage/exportdestination.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <filesystem>
#include <optional>
#include <system_error>

namespace mbl::storage {

using namespace mbl::domain;
namespace fs = std::filesystem;

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ExportDestination", text);
}

fs::path toFs(const QString& path)
{
#ifdef Q_OS_WIN
    return fs::path(QDir::toNativeSeparators(path).toStdWString());
#else
    const QByteArray utf8 = path.toUtf8();
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.constData()), size_t(utf8.size())));
#endif
}

QString fromFs(const fs::path& path)
{
#ifdef Q_OS_WIN
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
#else
    const std::u8string utf8 = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(utf8.data()), qsizetype(utf8.size()));
#endif
}

// The real path: links, "..", short names and (on Windows) letter case resolved.
std::optional<fs::path> realPath(const QString& path)
{
    std::error_code ec;
    fs::path real = fs::canonical(toFs(path), ec);
    if (ec)
        return std::nullopt;
    return real;
}

bool within(const fs::path& path, const fs::path& folder)
{
    const Qt::CaseSensitivity cs =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    const QString p = QDir::cleanPath(fromFs(path));
    const QString f = QDir::cleanPath(fromFs(folder));
    return p.compare(f, cs) == 0 || p.startsWith(f.endsWith(u'/') ? f : f + u'/', cs);
}

} // namespace

Result<QString> validateExportDestination(const QString& destination, const LibraryLayout& layout,
                                          const ExportDestinationRules& rules)
{
    const QString trimmed = destination.trimmed();
    if (trimmed.isEmpty())
        return makeError(ErrorCode::InvalidArgument, tr("Choose where to save the copy."));
    const QFileInfo info(trimmed);
    if (!info.isAbsolute())
        return makeError(ErrorCode::InvalidArgument, tr("Choose a full path for the copy."));
    if (info.suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0)
        return makeError(ErrorCode::InvalidArgument, tr("The copy must be a .pdf file."));
    const auto folder = realPath(info.absolutePath());
    if (!folder || !fs::is_directory(*folder))
        return makeError(ErrorCode::InvalidArgument, tr("The folder %1 does not exist.").arg(QDir::toNativeSeparators(info.absolutePath())));
    const fs::path target = *folder / toFs(info.fileName());

    // Nothing inside the library folder: its catalog, sources, reports and staging.
    if (const auto root = realPath(layout.root()); root && within(target, *root))
        return makeError(ErrorCode::InvalidArgument, tr("The copy cannot be saved inside the library folder."));

    std::error_code ec;
    const bool exists = fs::exists(target, ec);
    if (exists) {
        if (fs::is_directory(target, ec))
            return makeError(ErrorCode::InvalidArgument, tr("That name belongs to a folder."));
        // A managed source or an imported original under any name, hard links included.
        for (const QString& file : rules.protectedFiles) {
            std::error_code same;
            if (fs::equivalent(target, toFs(file), same) && !same) {
                return makeError(ErrorCode::InvalidArgument,
                                 tr("That file is one of the library's PDFs or an original you imported; "
                                    "choose another name."));
            }
        }
        if (!rules.replaceExisting)
            return makeError(ErrorCode::Duplicate, tr("A file with that name already exists."));
    }
    return QDir::cleanPath(fromFs(target));
}

QString suggestedExportPath(const QString& title, const QString& folder)
{
    QString base;
    for (const QChar c : title.trimmed()) {
        const bool forbidden = c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c);
        base += forbidden ? QChar(u'_') : c;
    }
    base = base.left(150).trimmed();
    while (base.endsWith(u'.') || base.endsWith(u' '))
        base.chop(1);  // Windows drops these from names.
    if (base.isEmpty())
        base = tr("Book");
    const QDir dir(folder);
    const QString stem = tr("%1 (bookmarked)").arg(base);
    QString candidate = dir.filePath(stem + QStringLiteral(".pdf"));
    for (int n = 2; QFileInfo::exists(candidate); ++n)
        candidate = dir.filePath(QStringLiteral("%1 (%2).pdf").arg(stem).arg(n));
    return candidate;
}

} // namespace mbl::storage
