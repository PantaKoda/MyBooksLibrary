#include "presentation/exportcontroller.h"

#include "catalog/catalog.h"
#include "catalog/exports.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "processing/processingcoordinator.h"
#include "storage/exportdestination.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>

namespace mbl::presentation {

using namespace mbl::domain;

namespace {

QString trn(const char* text, int n)
{
    return QCoreApplication::translate("mbl::presentation::ExportController", text, nullptr, n);
}

struct Preview {
    QString title;
    std::optional<ExportPlan> plan;
    QString problem;
    QString suggestedPath;
    QString lastExport;
};

QString when(const QDateTime& time)
{
    return QLocale().toString(time.toLocalTime(), QLocale::ShortFormat);
}

// The book's most recent export, in a sentence.
QString describeLastExport(const ExportRecord& record, const JobRecord& job)
{
    const QString path = QDir::toNativeSeparators(record.destination);
    if (record.committed.value_or(false)) {
        return ExportController::tr("Last copy: saved %1 as %2 (%3).")
            .arg(when(record.finishedAt), path, trn("%n bookmark(s)", record.output.outlineItems));
    }
    if (isOpen(job.state))
        return ExportController::tr("A copy is being saved as %1.").arg(path);
    if (job.state == JobState::Interrupted && !record.committed)
        return ExportController::tr("Last copy: interrupted while %1 was being written; it may or may not have been saved.")
            .arg(path);
    return ExportController::tr("Last copy: not saved (%1).")
        .arg(job.error.isEmpty() ? ExportController::tr("cancelled") : job.error);
}

} // namespace

ExportController::ExportController(QObject* parent) : QObject(parent) {}

void ExportController::setLibrary(std::shared_ptr<catalog::Library> library)
{
    m_library = std::move(library);
}

void ExportController::setCoordinator(processing::ProcessingCoordinator* coordinator)
{
    if (m_coordinator)
        disconnect(m_coordinator, nullptr, this, nullptr);
    m_coordinator = coordinator;
    if (!coordinator)
        return;
    using processing::ProcessingCoordinator;
    connect(coordinator, &ProcessingCoordinator::exportQueued, this, &ExportController::onQueued);
    connect(coordinator, &ProcessingCoordinator::exportRefused, this, &ExportController::onRefused);
    connect(coordinator, &ProcessingCoordinator::jobChanged, this, &ExportController::onJobChanged);
    connect(coordinator, &ProcessingCoordinator::exportFinished, this, &ExportController::onFinished);
    emit previewChanged();  // canExport may change.
}

void ExportController::prepare(const QString& bookId)
{
    const BookId book = BookId::fromString(bookId);
    const quint64 generation = ++m_generation;
    m_book = book;
    m_title.clear();
    m_plan.reset();
    m_planProblem.clear();
    m_suggestedPath.clear();
    m_lastExportText.clear();
    m_loading = m_library && !book.isNull();
    m_job.reset();
    m_finished.reset();
    m_pendingPath.clear();
    m_resultPath.clear();
    setPhase(Phase::Idle, {});
    if (!m_loading) {
        m_planProblem = tr("No book is selected.");
        emit previewChanged();
        return;
    }
    emit previewChanged();

    QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (folder.isEmpty())
        folder = QDir::homePath();
    m_library
        ->run([book, folder](QSqlDatabase& db) -> Preview {
            Preview p;
            auto details = catalog::bookDetails(db, book);
            if (!details) {
                p.problem = details.error().message;
                return p;
            }
            p.title = details.value().summary.displayTitle;
            if (details.value().summary.lifecycle == Lifecycle::Trashed) {
                p.problem = ExportController::tr("The book is in Trash; restore it to save a copy.");
            } else if (!details.value().toc) {
                p.problem = ExportController::tr(
                    "The book's contents have not been analyzed yet, so there is nothing to bookmark.");
            } else if (auto plan = buildExportPlan(*details.value().toc, details.value().asset); plan) {
                p.plan = plan.value();
            } else {
                p.problem = plan.error().message;
            }
            // Stat calls for the name: on this thread, not the GUI's.
            p.suggestedPath = storage::suggestedExportPath(p.title, folder);
            if (auto exports = catalog::bookExports(db, book); exports && !exports.value().isEmpty()) {
                const ExportRecord& latest = exports.value().first();
                if (auto job = catalog::job(db, latest.job))
                    p.lastExport = describeLastExport(latest, job.value());
            }
            return p;
        })
        .then(this, [this, generation](const Preview& p) {
            if (generation != m_generation)
                return;  // Another book was prepared meanwhile.
            m_loading = false;
            m_title = p.title;
            m_plan = p.plan;
            m_planProblem = p.problem;
            m_suggestedPath = p.suggestedPath;
            m_lastExportText = p.lastExport;
            emit previewChanged();
        });
}

QString ExportController::problem() const
{
    if (m_loading)
        return {};
    if (!m_planProblem.isEmpty())
        return m_planProblem;
    if (!m_coordinator)
        return tr("Saving bookmarked copies is not available in this build.");
    return {};
}

QString ExportController::summary() const
{
    if (!m_plan)
        return {};
    QStringList parts;
    parts << trn("The copy gets %n bookmark(s).", int(m_plan->nodes.size()));
    if (m_plan->complete()) {
        parts << tr("Every contents entry becomes a bookmark at its own level.");
    } else {
        if (!m_plan->omitted.isEmpty())
            parts << trn("%n contents entry(s) have no confirmed page and are left out.", int(m_plan->omitted.size()));
        if (!m_plan->promotions.isEmpty())
            parts << trn("%n entry(s) are placed at another level because their parent has no bookmark.",
                         int(m_plan->promotions.size()));
        if (!m_plan->uncertainLevels.isEmpty())
            parts << trn("%n entry(s) of uncertain level are placed at the top level.",
                         int(m_plan->uncertainLevels.size()));
    }
    if (m_plan->removedByUser > 0)
        parts << trn("%n entry(s) you removed are not included.", m_plan->removedByUser);
    parts << tr("The book in your library is not changed.");
    return parts.join(u' ');
}

QStringList ExportController::notes() const
{
    QStringList out;
    if (!m_plan)
        return out;
    const auto titleOf = [this](const QString& id) {
        for (const ExportNode& n : m_plan->nodes) {
            if (n.id == id)
                return n.title;
        }
        return id;
    };
    for (const ExportOmission& o : m_plan->omitted)
        out << tr("Left out: “%1”. %2").arg(o.title.trimmed().isEmpty() ? tr("(no title)") : o.title.trimmed(), o.reason);
    for (const ExportPromotion& p : m_plan->promotions)
        out << tr("Moved: “%1”. %2").arg(titleOf(p.nodeId), p.reason);
    for (const ExportUncertainLevel& u : m_plan->uncertainLevels)
        out << tr("Top level: “%1”. %2").arg(u.title, u.reason);
    return out;
}

QUrl ExportController::suggestedFolder() const
{
    return m_suggestedPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(QFileInfo(m_suggestedPath).absolutePath());
}

bool ExportController::running() const
{
    return m_phase == Phase::Requesting || m_phase == Phase::Waiting || m_phase == Phase::Writing
           || m_phase == Phase::Cancelling;
}

QUrl ExportController::resultFolder() const
{
    return m_resultPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(QFileInfo(m_resultPath).absolutePath());
}

QString ExportController::displayPath(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

void ExportController::exportTo(const QString& destination, bool replace)
{
    if (!canExport() || running())
        return;
    const QString path = destination.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)
                             ? QUrl(destination).toLocalFile()
                             : destination.trimmed();
    m_job.reset();
    m_finished.reset();
    m_pendingPath.clear();
    m_resultPath.clear();
    if (path.isEmpty()) {
        setPhase(Phase::NotSaved, tr("Choose where to save the copy."));
        return;
    }
    m_requestPath = path;
    setPhase(Phase::Requesting, tr("Checking where to save the copy…"));
    m_coordinator->enqueueExport(m_book, path, replace);
}

void ExportController::confirmReplace()
{
    if (m_phase != Phase::NeedsReplace)
        return;
    const QString path = m_pendingPath;
    setPhase(Phase::Idle, {});
    exportTo(path, true);
}

void ExportController::declineReplace()
{
    if (m_phase != Phase::NeedsReplace)
        return;
    m_pendingPath.clear();
    setPhase(Phase::Idle, {});
}

void ExportController::cancel()
{
    if (m_job && m_coordinator && (m_phase == Phase::Waiting || m_phase == Phase::Writing))
        m_coordinator->cancelJob(*m_job);
}

void ExportController::setPhase(Phase phase, const QString& text)
{
    if (m_phase == phase && m_phaseText == text)
        return;
    m_phase = phase;
    m_phaseText = text;
    emit phaseChanged();
}

void ExportController::onQueued(const ExportRecord& record)
{
    if (record.book != m_book || m_phase != Phase::Requesting)
        return;
    m_job = record.job;
    m_resultPath = record.destination;
    setPhase(Phase::Waiting, tr("Waiting for the current work to finish…"));
}

void ExportController::onRefused(const BookId& book, const QString& error, bool fileExists)
{
    if (book != m_book || m_phase != Phase::Requesting)
        return;
    if (fileExists) {
        m_pendingPath = m_requestPath;
        setPhase(Phase::NeedsReplace, tr("%1 already exists. Replace it?").arg(displayPath(m_pendingPath)));
        return;
    }
    setPhase(Phase::NotSaved, tr("Not saved: %1").arg(error));
}

void ExportController::onJobChanged(const JobRecord& job)
{
    if (!m_job || job.id != *m_job)
        return;
    switch (job.state) {
    case JobState::Queued:
        if (m_phase == Phase::Waiting)
            setPhase(Phase::Waiting, tr("Waiting for the current work to finish…"));
        return;
    case JobState::Running:
        setPhase(Phase::Writing, tr("Writing the bookmarked copy…"));
        return;
    case JobState::CancelRequested:
        setPhase(Phase::Cancelling, tr("Cancelling…"));
        return;
    case JobState::Succeeded: {
        const int bookmarks = m_finished ? m_finished->output.outlineItems : bookmarkCount();
        setPhase(Phase::Saved, tr("Saved as %1, with %2.").arg(displayPath(m_resultPath), trn("%n bookmark(s)", bookmarks)));
        return;
    }
    case JobState::Failed:
        setPhase(Phase::NotSaved, tr("Not saved: %1").arg(job.error));
        return;
    case JobState::Cancelled:
        setPhase(Phase::Cancelled, job.outcome == QLatin1String("trashed")
                                       ? tr("The book was moved to Trash; nothing was written.")
                                       : tr("Cancelled; nothing was written."));
        return;
    case JobState::Interrupted:
        setPhase(Phase::NotSaved, job.error);
        return;
    }
}

void ExportController::onFinished(const ExportRecord& record)
{
    if (m_job && record.job == *m_job)
        m_finished = record;
}

} // namespace mbl::presentation
