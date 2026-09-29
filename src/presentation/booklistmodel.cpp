#include "presentation/booklistmodel.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSet>
#include <QStringList>

namespace mbl::presentation {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("BookListModel", text);
}

QString trn(const char* text, int n)
{
    return QCoreApplication::translate("BookListModel", text, nullptr, n);
}

// What the catalog holds and what the latest job is doing are separate
// states: a published result stays shown while a rerun waits, runs or fails.
QString metadataText(const domain::BookSummary& book, const domain::JobRecord* job)
{
    using domain::JobState;
    if (job && job->state == JobState::Queued)
        return tr("Waiting to read title and authors");
    if (job && job->state == JobState::Running)
        return tr("Reading title and authors…");
    if (job && job->state == JobState::CancelRequested)
        return tr("Cancelling…");
    if (book.hasMetadataRun) {
        if (!book.displayTitleFromFileName)
            return tr("Metadata ready");
        if (book.metadata.titleSource == domain::ValueSource::Cleared)
            return tr("Title cleared");
        if (book.extractedTitleStatus == domain::FieldStatus::Ambiguous)
            return tr("Title uncertain: several candidates");
        return tr("No title found in the document");
    }
    if (job && job->state == JobState::Failed)
        return tr("Title and authors could not be read");
    if (job && job->state == JobState::Cancelled)
        return tr("Metadata extraction cancelled");
    if (job && job->state == JobState::Interrupted)
        return tr("Metadata extraction interrupted");
    return {};
}

QString contentsText(const domain::BookSummary& book, const domain::JobRecord* job)
{
    using domain::JobState;
    if (job && job->state == JobState::Queued)
        return tr("contents waiting");
    if (job && job->state == JobState::Running)
        return tr("analyzing contents…");
    if (job && job->state == JobState::CancelRequested)
        return tr("cancelling contents analysis…");
    if (book.hasTocRun) {
        if (book.tocNeedsReconciliation)
            return trn("%n contents entries (edited; a new analysis to review)", book.tocEntryCount);
        if (book.tocEdited)
            return trn("%n contents entries (edited)", book.tocEntryCount);
        return book.tocEntryCount > 0 ? trn("%n contents entries", book.tocEntryCount)
                                      : tr("no printed contents found");
    }
    if (job && job->state == JobState::Failed && job->outcome != QLatin1String("unsupported"))
        return tr("contents could not be analyzed");
    if (job && job->state == JobState::Cancelled)
        return tr("contents analysis cancelled");
    if (job && job->state == JobState::Interrupted)
        return tr("contents analysis interrupted");
    return tr("contents not analyzed");
}

QString stateText(const domain::BookSummary& book, const domain::JobRecord* metadataJob,
                  const domain::JobRecord* contentsJob)
{
    const QString metadata = metadataText(book, metadataJob);
    if (metadata.isEmpty() && !contentsJob && !book.hasTocRun)
        return tr("Imported · not analyzed yet");
    const QString contents = contentsText(book, contentsJob);
    return metadata.isEmpty() ? tr("Imported") + QStringLiteral(" · ") + contents
                              : metadata + QStringLiteral(" · ") + contents;
}

} // namespace

BookListModel::BookListModel(QObject* parent) : QAbstractListModel(parent) {}

int BookListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_books.size());
}

QVariant BookListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_books.size())
        return {};
    const domain::BookSummary& book = m_books.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return book.displayTitle;
    case BookIdRole:
        return book.id.toString();
    case TitleFromFileNameRole:
        return book.displayTitleFromFileName;
    case ContributorsRole: {
        QStringList names;
        for (const auto& c : book.metadata.contributors)
            names << c.name;
        return names.join(QStringLiteral(", "));
    }
    case ProcessingStateRole:
        return stateOf(book);
    }
    return {};
}

QHash<int, QByteArray> BookListModel::roleNames() const
{
    return {
        {BookIdRole, "bookId"},
        {TitleRole, "title"},
        {TitleFromFileNameRole, "titleFromFileName"},
        {ContributorsRole, "contributors"},
        {ProcessingStateRole, "processingState"},
    };
}

void BookListModel::setBooks(QList<domain::BookSummary> books)
{
    // Apply the new list as row-level changes keyed by book ID (removals,
    // moves, insertions, dataChanged) rather than a model reset, so views
    // keep their scroll position, current item and delegates.
    const qsizetype oldCount = m_books.size();

    QSet<domain::BookId> wanted;
    for (const domain::BookSummary& book : books)
        wanted.insert(book.id);
    for (qsizetype row = m_books.size() - 1; row >= 0; --row) {
        if (!wanted.contains(m_books.at(row).id)) {
            beginRemoveRows({}, int(row), int(row));
            m_books.removeAt(row);
            endRemoveRows();
        }
    }

    for (qsizetype target = 0; target < books.size(); ++target) {
        domain::BookSummary& incoming = books[target];
        if (target < m_books.size() && m_books.at(target).id == incoming.id) {
            // Same book in place: replace it, and notify if it changed.
            const bool changed = m_books.at(target).revision != incoming.revision;
            m_books[target] = std::move(incoming);
            if (changed)
                emit dataChanged(index(int(target)), index(int(target)));
            continue;
        }
        qsizetype from = -1;
        for (qsizetype row = target + 1; row < m_books.size(); ++row) {
            if (m_books.at(row).id == incoming.id) {
                from = row;
                break;
            }
        }
        if (from >= 0) {
            // Later in the list: move it up to its new position.
            const bool changed = m_books.at(from).revision != incoming.revision;
            beginMoveRows({}, int(from), int(from), {}, int(target));
            m_books.move(from, target);
            endMoveRows();
            m_books[target] = std::move(incoming);
            if (changed)
                emit dataChanged(index(int(target)), index(int(target)));
        } else {
            beginInsertRows({}, int(target), int(target));
            m_books.insert(target, std::move(incoming));
            endInsertRows();
        }
    }

    if (m_books.size() != oldCount)
        emit countChanged();
}

bool BookListModel::acceptJob(const domain::JobRecord& job)
{
    auto& jobs = job.kind == domain::JobKind::Metadata ? m_metadataJobs : m_contentsJobs;
    const auto current = jobs.constFind(job.book);
    if (current != jobs.cend()) {
        // A different, older job never replaces the latest one; the same
        // job only moves forward in time.
        if (current->id != job.id ? job.createdAt < current->createdAt : job.updatedAt < current->updatedAt)
            return false;
    }
    jobs.insert(job.book, job);
    return true;
}

void BookListModel::setLatestJobs(const QList<domain::JobRecord>& jobs)
{
    bool changed = false;
    for (const domain::JobRecord& job : jobs)
        changed = acceptJob(job) || changed;
    if (changed && !m_books.isEmpty())
        emit dataChanged(index(0), index(int(m_books.size() - 1)), {ProcessingStateRole});
}

void BookListModel::updateJob(const domain::JobRecord& job)
{
    if (acceptJob(job))
        emitStateChanged(job.book);
}

void BookListModel::emitStateChanged(const domain::BookId& id)
{
    for (qsizetype row = 0; row < m_books.size(); ++row) {
        if (m_books.at(row).id == id) {
            emit dataChanged(index(int(row)), index(int(row)), {ProcessingStateRole});
            return;
        }
    }
}

QString BookListModel::stateOf(const domain::BookSummary& book) const
{
    if (book.lifecycle == domain::Lifecycle::Trashed) {
        return book.trashedAt.isValid()
                   ? tr("In Trash since %1").arg(QLocale().toString(book.trashedAt.toLocalTime().date(), QLocale::ShortFormat))
                   : tr("In Trash");
    }
    const auto metadata = m_metadataJobs.constFind(book.id);
    const auto contents = m_contentsJobs.constFind(book.id);
    return stateText(book, metadata == m_metadataJobs.cend() ? nullptr : &metadata.value(),
                     contents == m_contentsJobs.cend() ? nullptr : &contents.value());
}

void BookListModel::setKnownBooks(const QList<domain::BookSummary>& books)
{
    m_known.clear();
    for (const domain::BookSummary& book : books)
        m_known.insert(book.id, book);
}

// Lookups use every known book (one hash lookup, and the newest values);
// the rows only when setKnownBooks was never called.
const domain::BookSummary* BookListModel::find(const domain::BookId& id) const
{
    if (const auto known = m_known.constFind(id); known != m_known.cend())
        return &known.value();
    for (const domain::BookSummary& book : m_books) {
        if (book.id == id)
            return &book;
    }
    return nullptr;
}

QString BookListModel::processingStateOf(const domain::BookId& id) const
{
    const domain::BookSummary* book = find(id);
    return book ? stateOf(*book) : QString();
}

QString BookListModel::titleOf(const domain::BookId& id) const
{
    const domain::BookSummary* book = find(id);
    return book ? book->displayTitle : QString();
}

int BookListModel::rowOfBook(const QString& bookId) const
{
    for (qsizetype i = 0; i < m_books.size(); ++i) {
        if (m_books.at(i).id.toString() == bookId)
            return int(i);
    }
    return -1;
}

QString BookListModel::bookIdAt(int row) const
{
    return row >= 0 && row < m_books.size() ? m_books.at(row).id.toString() : QString();
}

} // namespace mbl::presentation
