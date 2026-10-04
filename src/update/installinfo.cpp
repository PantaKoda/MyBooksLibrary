#include "update/installinfo.h"

#include "storage/exportdestination.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>

namespace mbl::update {

std::optional<Version> readReleaseMarker(const QString& folder)
{
    QFile file(QDir(folder).filePath(QString::fromLatin1(releaseMarkerName)));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return std::nullopt;
    const QJsonObject marker = QJsonDocument::fromJson(file.readAll()).object();
    if (marker.value(QLatin1String("product")).toString() != QLatin1String(productName))
        return std::nullopt;
    return Version::parse(marker.value(QLatin1String("version")).toString());
}

QByteArray releaseMarker(const Version& version)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("product"), QString::fromLatin1(productName)},
                                     {QStringLiteral("version"), version.toString()}})
        .toJson();
}

InstallCheck checkInstall(const QString& appFolder, const Version& running, const QStringList& keptFolders)
{
    const auto refuse = [](const QString& reason) { return InstallCheck{false, reason}; };
    const std::optional<Version> marker = readReleaseMarker(appFolder);
    if (!marker)
        return refuse(QCoreApplication::translate(
            "Updates", "This copy was not installed from a release, so it cannot update itself. "
                       "Download the new release from its page instead."));
    if (*marker != running)
        return refuse(QCoreApplication::translate(
                          "Updates", "This copy's release marker (%1) does not match the running version (%2).")
                          .arg(marker->toString(), running.toString()));
    // isInsideFolder resolves links and short names, but needs the parent to
    // exist; the path itself covers a folder not made yet (no first update).
    const QString app = QDir::cleanPath(QFileInfo(appFolder).absoluteFilePath());
    const auto inside = [&app, &appFolder](const QString& path) {
        const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        return clean.compare(app, Qt::CaseInsensitive) == 0 || clean.startsWith(app + u'/', Qt::CaseInsensitive)
               || storage::isInsideFolder(path, appFolder);
    };
    for (const QString& kept : keptFolders) {
        if (!kept.isEmpty() && inside(kept))
            return refuse(QCoreApplication::translate(
                              "Updates", "%1 is inside the app's folder, which an update replaces. "
                                         "Move the app's folder elsewhere (for example its own folder in "
                                         "Downloads or Programs) to update it.")
                              .arg(QDir::toNativeSeparators(kept)));
    }
    // The new copy and the previous one are made beside the app's folder.
    const QString parent = QFileInfo(QDir::cleanPath(appFolder)).absolutePath();
    QTemporaryFile probe(QDir(parent).filePath(QStringLiteral("mbl-update-probe-XXXXXX")));
    if (!probe.open())
        return refuse(QCoreApplication::translate(
                          "Updates", "The folder that holds the app (%1) cannot be written, so the app cannot "
                                     "replace itself there.")
                          .arg(QDir::toNativeSeparators(parent)));
    return InstallCheck{true, {}};
}

} // namespace mbl::update
