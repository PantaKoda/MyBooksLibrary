#include "presentation/joblistmodel.h"

#include <QCoreApplication>
#include <QSet>

namespace mbl::presentation {

using namespace mbl::domain;

namespace {

constexpr qsizetype kMaxRows = 200;  // Finished jobs beyond this are dropped from the view.

QString tr(const char* text)
{
    return QCoreApplication::translate("JobListModel", text);
}

QString trn(const char* text, int n)
{
    return QCoreApplication::translate("JobListModel", text, nullptr, n);
}

} // namespace

JobListModel::JobListModel(QObject* parent) : QAbstractListModel(parent) {}

int JobListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_jobs.size());
}

QString JobListModel::stateText(const JobRecord& job)
{
    switch (job.state) {
    case JobState::Queued:
        return tr("Waiting");
    case JobState::Running:
        if (job.kind == JobKind::Export)
            return tr("Writing the bookmarked copy…");
        return job.kind == JobKind::Metadata ? tr("Reading the first pages…") : tr("Analyzing the contents…");
    case JobState::CancelRequested:
        return tr("Cancelling…");
    case JobState::Succeeded:
        return job.kind == JobKind::Export ? tr("Copy saved") : tr("Done");
    case JobState::Failed:
        if (job.outcome == QLatin1String("source_mismatch"))
            return tr("Failed: the library copy no longer matches the imported file");
        if (job.outcome == QLatin1String("unsupported"))
            return tr("Not available yet");
        if (job.outcome == QLatin1String("output_exists"))
            return tr("Failed: a file with that name already exists");
        return tr("Failed");
    case JobState::Cancelled:
        if (job.outcome == QLatin1String("superseded"))
            return tr("Replaced by a newer request");
        if (job.outcome == QLatin1String("trashed"))
            return tr("Book moved to Trash");
        return tr("Cancelled");
    case JobState::Interrupted:
        if (job.kind == JobKind::Export)  // Never requeued: the user decides.
            return tr("Interrupted when the application closed; the copy may not have been saved");
        return tr("Interrupted when the application closed; queued again");
    }
    return {};
}

QVariant JobListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size())
        return {};
    const JobRecord& job = m_jobs.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case BookTitleRole: {
        const QString title = m_titleOf ? m_titleOf(job.book) : QString();
        return title.isEmpty() ? tr("(book not in the library list)") : title;
    }
    case JobIdRole:
        return job.id.toString();
    case BookIdRole:
        return job.book.toString();
    case KindTextRole:
        if (job.kind == JobKind::Export)
            return tr("Bookmarked copy");
        return job.kind == JobKind::Metadata ? tr("Title and authors") : tr("Contents");
    case StateRole:
        return toCode(job.state);
    case StateTextRole:
        return stateTextWithProgress(job);
    case DetailRole:
        return job.state == JobState::Failed || (job.kind == JobKind::Export && job.state == JobState::Interrupted)
                   ? job.error
                   : QString();
    case RunningRole:
        return job.state == JobState::Running || job.state == JobState::CancelRequested;
    case CanCancelRole:
        return job.state == JobState::Queued || job.state == JobState::Running;
    case CanRetryRole:
        // An export is asked for again with its destination (the Export dialog).
        return job.kind != JobKind::Export && (job.state == JobState::Failed || job.state == JobState::Cancelled)
               && job.outcome != QLatin1String("trashed") && job.outcome != QLatin1String("unsupported")
               && !hasNewerJob(job);
    }
    return {};
}

QHash<int, QByteArray> JobListModel::roleNames() const
{
    return {
        {JobIdRole, "jobId"},         {BookIdRole, "bookId"},       {BookTitleRole, "bookTitle"},
        {KindTextRole, "kindText"},   {StateRole, "state"},         {StateTextRole, "stateText"},
        {DetailRole, "detail"},       {RunningRole, "running"},     {CanCancelRole, "canCancel"},
        {CanRetryRole, "canRetry"},
    };
}

QString JobListModel::stateTextWithProgress(const JobRecord& job) const
{
    const QString text = stateText(job);
    const auto progress = m_progress.constFind(job.id);
    if (job.state != JobState::Running || progress == m_progress.cend() || progress->pages <= 0)
        return text;
    return text + QStringLiteral(" · ") + trn("%n page(s) read", progress->pages);
}

void JobListModel::setProgress(const JobId& id, const QString& stage, int pagesAcquired)
{
    for (qsizetype row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row).id != id)
            continue;
        if (m_jobs.at(row).state != JobState::Running)
            return;  // A late update for a job that already stopped.
        m_progress.insert(id, Progress{stage, pagesAcquired});
        emit dataChanged(index(int(row)), index(int(row)), {StateTextRole});
        emit summaryChanged();
        return;
    }
}

bool JobListModel::hasNewerJob(const JobRecord& job) const
{
    for (const JobRecord& other : m_jobs) {
        if (other.id != job.id && other.book == job.book && other.kind == job.kind && other.createdAt > job.createdAt)
            return true;
    }
    return false;
}

void JobListModel::upsert(const JobRecord& job)
{
    for (qsizetype row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row).id != job.id)
            continue;
        if (job.updatedAt < m_jobs.at(row).updatedAt)
            return;  // Older than what is shown.
        m_jobs[row] = job;
        if (job.state != JobState::Running)
            m_progress.remove(job.id);
        emit dataChanged(index(int(row)), index(int(row)));
        // Retry availability of the book's other jobs may change too.
        if (!m_jobs.isEmpty())
            emit dataChanged(index(0), index(int(m_jobs.size() - 1)), {CanRetryRole});
        emit summaryChanged();
        return;
    }
    // Insert in order, newest first (a snapshot may bring older jobs).
    qsizetype at = 0;
    while (at < m_jobs.size() && m_jobs.at(at).createdAt >= job.createdAt)
        ++at;
    beginInsertRows({}, int(at), int(at));
    m_jobs.insert(at, job);
    endInsertRows();
    if (m_jobs.size() > 1)
        emit dataChanged(index(0), index(int(m_jobs.size() - 1)), {CanRetryRole});
    trim();
    emit countChanged();
    emit summaryChanged();
}

void JobListModel::trim()
{
    // Drop the oldest finished rows beyond the limit; pending jobs always stay.
    for (qsizetype row = m_jobs.size() - 1; row >= 0 && m_jobs.size() > kMaxRows; --row) {
        if (isOpen(m_jobs.at(row).state))
            continue;
        beginRemoveRows({}, int(row), int(row));
        m_jobs.removeAt(row);
        endRemoveRows();
    }
}

void JobListModel::titlesChanged()
{
    if (!m_jobs.isEmpty())
        emit dataChanged(index(0), index(int(m_jobs.size() - 1)), {BookTitleRole, Qt::DisplayRole});
    emit summaryChanged();
}

std::optional<JobRecord> JobListModel::job(const JobId& id) const
{
    for (const JobRecord& j : m_jobs) {
        if (j.id == id)
            return j;
    }
    return std::nullopt;
}

int JobListModel::pendingCount() const
{
    int n = 0;
    for (const JobRecord& j : m_jobs)
        n += j.state == JobState::Queued || j.state == JobState::Running ? 1 : 0;
    return n;
}

QString JobListModel::summary() const
{
    // A book has a metadata and a contents job, and a paired run has both
    // running: show the metadata job while its stage runs, and count waiting
    // BOOKS (not jobs), leaving out the book being processed.
    const JobRecord* running = nullptr;
    QSet<BookId> waitingBooks;
    for (const JobRecord& j : m_jobs) {
        if (j.state == JobState::Running || j.state == JobState::CancelRequested) {
            if (!running || (j.kind == JobKind::Metadata && running->kind != JobKind::Metadata))
                running = &j;
        } else if (j.state == JobState::Queued) {
            waitingBooks.insert(j.book);
        }
    }
    if (running)
        waitingBooks.remove(running->book);
    const int waiting = int(waitingBooks.size());
    QString text;
    if (running) {
        const QString title = m_titleOf ? m_titleOf(running->book) : QString();
        const bool metadata = running->kind == JobKind::Metadata;
        if (running->kind == JobKind::Export) {
            if (running->state == JobState::CancelRequested)
                text = tr("Cancelling the bookmarked copy…");
            else
                text = title.isEmpty() ? tr("Writing a bookmarked copy…") : tr("Writing a bookmarked copy: %1").arg(title);
        } else if (running->state == JobState::CancelRequested)
            text = metadata ? tr("Cancelling metadata extraction…") : tr("Cancelling contents analysis…");
        else if (title.isEmpty())
            text = metadata ? tr("Reading title and authors…") : tr("Analyzing contents…");
        else
            text = metadata ? tr("Reading title and authors: %1").arg(title) : tr("Analyzing contents: %1").arg(title);
        const auto progress = m_progress.constFind(running->id);
        if (running->state == JobState::Running && progress != m_progress.cend() && progress->pages > 0)
            text += QStringLiteral(" · ") + trn("%n page(s) read", progress->pages);
    }
    if (waiting > 0) {
        const QString more = trn("%n book(s) waiting", waiting);
        text = text.isEmpty() ? more : text + QStringLiteral(" · ") + more;
    }
    return text;
}

} // namespace mbl::presentation
