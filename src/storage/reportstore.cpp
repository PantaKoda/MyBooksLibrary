#include "storage/reportstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace mbl::storage {

QString reportPath(const domain::RunId& run)
{
    return QStringLiteral("reports/%1.json").arg(run.toString());
}

domain::Result<QString> writeReport(const LibraryLayout& layout, const domain::RunId& run, const QByteArray& json)
{
    const QString relative = reportPath(run);
    const QString absolute = layout.absolute(relative);
    if (QFileInfo::exists(absolute))
        return domain::makeError(domain::ErrorCode::Io, QStringLiteral("Report %1 already exists.").arg(relative));
    QDir().mkpath(QFileInfo(absolute).absolutePath());
    QSaveFile file(absolute);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        return domain::makeError(domain::ErrorCode::Io,
                                 QStringLiteral("Cannot write report %1: %2").arg(relative, file.errorString()));
    }
    return relative;
}

QStringList removeUnreferencedReports(const LibraryLayout& layout, const QStringList& referenced)
{
    QStringList removed;
    const QDir dir(layout.absolute(QStringLiteral("reports")));
    const QSet<QString> keep(referenced.cbegin(), referenced.cend());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        const auto run = domain::RunId::fromString(info.completeBaseName());
        if (run.isNull())
            continue;  // Not a report this application wrote.
        const QString relative = reportPath(run);
        if (keep.contains(relative) || info.fileName() != QFileInfo(relative).fileName())
            continue;
        if (QFile::remove(info.absoluteFilePath()))
            removed << relative;
    }
    return removed;
}

bool removeUnpublishedReport(const LibraryLayout& layout, const domain::RunId& run)
{
    return QFile::remove(layout.absolute(reportPath(run)));
}

} // namespace mbl::storage
