#include "presentation/joblistmodel.h"

#include <QCoreApplication>

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
        return tr("Reading the first pages…");
    case JobState::CancelRequested:
        return tr("Cancelling…");
    case JobState::Succeeded:
        return tr("Done");
    case JobState::Failed:
        if (job.outcome == QLatin1String("source_mismatch"))
            return tr("Failed: the library copy no longer matches the imported file");
        if (job.outcome == QLatin1String("unsupported"))
            return tr("Not available yet");
        return tr("Failed");
    case JobState::Cancelled:
        if (job.outcome == QLatin1String("superseded"))
            return tr("Replaced by a newer request");
        if (job.outcome == QLatin1String("trashed"))
            return tr("Book moved to Trash");
        return tr("Cancelled");
    case JobState::Interrupted:
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
        return job.kind == JobKind::Metadata ? tr("Title and authors") : tr("Contents");
    case StateRole:
        return toCode(job.state);
    case StateTextRole:
        return stateText(job);
    case DetailRole:
        return job.state == JobState::Failed ? job.error : QString();
    case RunningRole:
        return job.state == JobState::Running || job.state == JobState::CancelRequested;
    case CanCancelRole:
        return job.state == JobState::Queued || job.state == JobState::Running;
    case CanRetryRole:
        return (job.state == JobState::Failed || job.state == JobState::Cancelled)
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
    const JobRecord* running = nullptr;
    int waiting = 0;
    for (const JobRecord& j : m_jobs) {
        if (j.state == JobState::Running || j.state == JobState::CancelRequested)
            running = &j;
        else if (j.state == JobState::Queued)
            ++waiting;
    }
    QString text;
    if (running) {
        const QString title = m_titleOf ? m_titleOf(running->book) : QString();
        text = running->state == JobState::CancelRequested ? tr("Cancelling metadata extraction…")
               : title.isEmpty()                           ? tr("Reading title and authors…")
                                                           : tr("Reading title and authors: %1").arg(title);
    }
    if (waiting > 0) {
        const QString more = trn("%n book(s) waiting", waiting);
        text = text.isEmpty() ? more : text + QStringLiteral(" · ") + more;
    }
    return text;
}

} // namespace mbl::presentation
