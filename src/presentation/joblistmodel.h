// Presentation: the processing activity list (metadata and contents jobs),
// newest first. GUI-thread owned; filled with copied JobRecord values from
// the database thread and updated from ProcessingCoordinator::jobChanged.
// Rows are not identifiers: actions take the job ID.
#pragma once

#include "domain/ids.h"
#include "domain/jobs.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QQmlEngine>

#include <functional>
#include <optional>

namespace mbl::presentation {

class JobListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by LibraryController.jobs.")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY summaryChanged)  // Queued or running.
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)       // One line for the status bar.

public:
    enum Role {
        JobIdRole = Qt::UserRole + 1,
        BookIdRole,
        BookTitleRole,
        KindTextRole,     // "Title and authors" / "Contents".
        StateRole,        // Stable state code (JobState).
        StateTextRole,    // Plain-language state and outcome.
        DetailRole,       // Error message, if any.
        RunningRole,
        CanCancelRole,
        CanRetryRole,     // Ended without a result and no newer job of that book and kind exists.
    };

    using TitleLookup = std::function<QString(const domain::BookId&)>;

    explicit JobListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTitleLookup(TitleLookup lookup) { m_titleOf = std::move(lookup); }
    // Inserts (ordered newest first) or updates one job. An update older than
    // the row (by updatedAt) is ignored, so a late snapshot cannot undo a
    // newer state delivered by jobChanged.
    void upsert(const domain::JobRecord& job);
    void titlesChanged();  // Book titles changed: refresh the title column.
    // Latest SDK progress of a running job (pages acquired in its stage; no
    // total exists). Cleared when the job stops running.
    void setProgress(const domain::JobId& id, const QString& stage, int pagesAcquired);

    std::optional<domain::JobRecord> job(const domain::JobId& id) const;
    int pendingCount() const;
    QString summary() const;

    static QString stateText(const domain::JobRecord& job);
    QString stateTextWithProgress(const domain::JobRecord& job) const;

signals:
    void countChanged();
    void summaryChanged();

private:
    bool hasNewerJob(const domain::JobRecord& job) const;
    void trim();

    struct Progress {
        QString stage;
        int pages = 0;
    };
    QList<domain::JobRecord> m_jobs;
    QHash<domain::JobId, Progress> m_progress;
    TitleLookup m_titleOf;
};

} // namespace mbl::presentation
