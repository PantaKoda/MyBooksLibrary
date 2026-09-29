// Presentation: the Export dialog's session for one book (M09). It shows what
// a bookmarked copy will contain -- how many bookmarks, and every contents
// entry left out or moved, with the reason -- suggests a file name, asks the
// ProcessingCoordinator to write the copy, and follows that export: waiting
// (the worker runs one SDK call at a time, so it can wait behind an
// analysis), writing, then saved, not saved, or cancelled. An existing file
// is replaced only after the user confirms it (NeedsReplace).
//
// Catalog reads run on the database thread; nothing here touches files or
// the SDK on the GUI thread.
#pragma once

#include "domain/export.h"
#include "domain/ids.h"
#include "domain/jobs.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QStringList>
#include <QUrl>

#include <memory>

namespace mbl::catalog {
class Library;
}
namespace mbl::processing {
class ProcessingCoordinator;
}

namespace mbl::presentation {

class ExportController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The library session owns the export session.")
    Q_PROPERTY(QString bookId READ bookId NOTIFY previewChanged)
    Q_PROPERTY(QString bookTitle READ bookTitle NOTIFY previewChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY previewChanged)
    // A copy can be requested: the plan has bookmarks and exports run here.
    Q_PROPERTY(bool canExport READ canExport NOTIFY previewChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY previewChanged)  // Why not, in plain words.
    Q_PROPERTY(int bookmarkCount READ bookmarkCount NOTIFY previewChanged)
    Q_PROPERTY(bool complete READ complete NOTIFY previewChanged)   // Nothing left out or moved.
    Q_PROPERTY(QString summary READ summary NOTIFY previewChanged)
    Q_PROPERTY(QStringList notes READ notes NOTIFY previewChanged)  // One line per entry left out or moved.
    Q_PROPERTY(QString suggestedPath READ suggestedPath NOTIFY previewChanged)
    Q_PROPERTY(QUrl suggestedFolder READ suggestedFolder NOTIFY previewChanged)
    Q_PROPERTY(QString lastExportText READ lastExportText NOTIFY previewChanged)
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool running READ running NOTIFY phaseChanged)  // Requested, waiting or writing.
    Q_PROPERTY(QString phaseText READ phaseText NOTIFY phaseChanged)
    Q_PROPERTY(QString pendingPath READ pendingPath NOTIFY phaseChanged)  // The file NeedsReplace asks about.
    Q_PROPERTY(QString resultPath READ resultPath NOTIFY phaseChanged)
    Q_PROPERTY(QUrl resultFolder READ resultFolder NOTIFY phaseChanged)

public:
    enum class Phase { Idle, Requesting, Waiting, Writing, Cancelling, NeedsReplace, Saved, NotSaved, Cancelled };
    Q_ENUM(Phase)

    explicit ExportController(QObject* parent = nullptr);

    // The library for previews; the coordinator (may be null: exports are
    // then unavailable) runs them. Called by the library session.
    void setLibrary(std::shared_ptr<catalog::Library> library);
    void setCoordinator(processing::ProcessingCoordinator* coordinator);

    // Loads the preview for a book and resets the export state. A previous
    // request keeps running; its result is no longer shown here.
    Q_INVOKABLE void prepare(const QString& bookId);
    // Asks for the copy at `destination` (a path or a file URL). With
    // `replace`, an existing file there is replaced -- only after the user
    // confirmed it. A refusal because the file exists moves to NeedsReplace.
    Q_INVOKABLE void exportTo(const QString& destination, bool replace = false);
    Q_INVOKABLE void confirmReplace();   // NeedsReplace: replace that file.
    Q_INVOKABLE void declineReplace();   // NeedsReplace: choose another name.
    Q_INVOKABLE void cancel();           // The running export.
    // A file dialog's URL as a path to show and type in (native separators).
    Q_INVOKABLE QString localPath(const QUrl& url) const { return displayPath(url.toLocalFile()); }

    QString bookId() const { return m_book.toString(); }
    QString bookTitle() const { return m_title; }
    bool loading() const { return m_loading; }
    bool canExport() const { return m_plan.has_value() && m_coordinator && !m_loading; }
    QString problem() const;
    int bookmarkCount() const { return m_plan ? int(m_plan->nodes.size()) : 0; }
    bool complete() const { return m_plan && m_plan->complete(); }
    QString summary() const;
    QStringList notes() const;
    QString suggestedPath() const { return m_suggestedPath; }
    QUrl suggestedFolder() const;
    QString lastExportText() const { return m_lastExportText; }
    Phase phase() const { return m_phase; }
    bool running() const;
    QString phaseText() const { return m_phaseText; }
    QString pendingPath() const { return m_pendingPath; }
    QString resultPath() const { return m_resultPath; }
    QUrl resultFolder() const;

signals:
    void previewChanged();
    void phaseChanged();

private:
    void setPhase(Phase phase, const QString& text);
    void onQueued(const domain::ExportRecord& record);
    void onRefused(const domain::BookId& book, const QString& error, bool fileExists);
    void onJobChanged(const domain::JobRecord& job);
    void onFinished(const domain::ExportRecord& record);
    static QString displayPath(const QString& path);

    std::shared_ptr<catalog::Library> m_library;
    QPointer<processing::ProcessingCoordinator> m_coordinator;
    quint64 m_generation = 0;  // Drops previews of an earlier prepare().

    domain::BookId m_book;
    QString m_title;
    bool m_loading = false;
    std::optional<domain::ExportPlan> m_plan;
    QString m_planProblem;
    QString m_suggestedPath;
    QString m_lastExportText;

    Phase m_phase = Phase::Idle;
    QString m_phaseText;
    QString m_requestPath;  // The destination of the request being answered.
    QString m_pendingPath;
    std::optional<domain::JobId> m_job;
    std::optional<domain::ExportRecord> m_finished;
    QString m_resultPath;
};

} // namespace mbl::presentation
