// Presentation: the selected book's details for the inspector pane. Loads the
// book (effective metadata, where each value came from, the evidence and
// candidates behind it, and the application's contents) on the database
// thread; only the newest selection's result is applied, on the GUI thread.
// All text is plain language; technical detail is available separately.
//
// Corrections: the shown book's metadata fields can be set to the user's
// value, deliberately cleared, or returned to the document's value (Auto).
// They are written on the database thread with the search index in the same
// transaction (catalog::setOverride) and never touch the PDF.
//
// Contents edits: entries of the shown book's contents can be renamed, given
// or cleared a page, moved a level, added, removed and restored; each save is
// a catalog revision (catalog/tocedits.h), checked against the contents on
// screen. After a newer analysis changed the entries, the user keeps the
// edits or uses the analysis.
#pragma once

#include "domain/book.h"
#include "domain/metadata.h"
#include "domain/result.h"
#include "presentation/toctreemodel.h"

#include <QObject>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <memory>
#include <optional>

namespace mbl::catalog {
class Library;
}

namespace mbl::presentation {

class BookInspector : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by LibraryController.inspector.")
    Q_PROPERTY(QString bookId READ bookId NOTIFY bookChanged)
    Q_PROPERTY(bool hasBook READ hasBook NOTIFY bookChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString title READ title NOTIFY detailsChanged)
    Q_PROPERTY(QString fileText READ fileText NOTIFY detailsChanged)
    Q_PROPERTY(bool inTrash READ inTrash NOTIFY detailsChanged)  // The shown book is in Trash.
    // One map per field: field (code), kind ("text", "year" or "contributors"),
    // label, value, mode ("auto", "value" or "cleared"), sourceText,
    // documentValue (what the document gives, when not shown), editText or
    // editContributors (the current value, to edit), evidence (lines),
    // alternatives (lines).
    Q_PROPERTY(QVariantList metadataFields READ metadataFields NOTIFY detailsChanged)
    // Contributor roles for an editor: {code, text}.
    Q_PROPERTY(QVariantList contributorRoles READ contributorRoles CONSTANT)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(QString correctionError READ correctionError NOTIFY correctionErrorChanged)
    Q_PROPERTY(QString contentsSummary READ contentsSummary NOTIFY detailsChanged)
    Q_PROPERTY(QStringList contentsNotes READ contentsNotes NOTIFY detailsChanged)
    Q_PROPERTY(mbl::presentation::TocTreeModel* contents READ contents CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY detailsChanged)
    Q_PROPERTY(bool contentsEdited READ contentsEdited NOTIFY detailsChanged)
    Q_PROPERTY(bool contentsNeedReconciliation READ contentsNeedReconciliation NOTIFY detailsChanged)
    // Plain-language state of edited contents; empty when not edited.
    Q_PROPERTY(QString contentsEditText READ contentsEditText NOTIFY detailsChanged)
    Q_PROPERTY(QString contentsError READ contentsError NOTIFY contentsErrorChanged)

public:
    explicit BookInspector(QObject* parent = nullptr);

    void setLibrary(std::shared_ptr<catalog::Library> library);

    // Shows the book (empty ID: nothing). Loads asynchronously.
    Q_INVOKABLE void select(const QString& bookId);
    // Reloads the shown book (after a publication or correction).
    void reload();

    // Corrections of a book's field (a code from metadataFields). The book is
    // named explicitly, so a correction never lands on a book selected while
    // it was being edited. An invalid value is refused with correctionError
    // and nothing is saved.
    Q_INVOKABLE void setText(const QString& bookId, const QString& field, const QString& value);  // Title, subtitle, edition.
    Q_INVOKABLE void setYear(const QString& bookId, const QString& field, const QString& value);  // A year from 1 to 9999.
    // Ordered list of {name, role}; role is a code from contributorRoles.
    Q_INVOKABLE void setContributors(const QString& bookId, const QVariantList& contributors);
    Q_INVOKABLE void clearField(const QString& bookId, const QString& field);        // Deliberately empty.
    Q_INVOKABLE void useDocumentValue(const QString& bookId, const QString& field);  // Remove the correction.
    Q_INVOKABLE void dismissCorrectionError() { setCorrectionError({}); }

    // Contents edits of the shown book, by entry key (TocTreeModel's
    // entryId). Page numbers are as shown (1 = first page). If the contents
    // changed since they were shown, nothing is saved, contentsError says so
    // and the contents are reloaded.
    Q_INVOKABLE void renameEntry(const QString& bookId, const QString& key, const QString& title);
    Q_INVOKABLE void setEntryPage(const QString& bookId, const QString& key, const QString& pageNumber);
    Q_INVOKABLE void clearEntryPage(const QString& bookId, const QString& key);
    Q_INVOKABLE void indentEntry(const QString& bookId, const QString& key);   // Under the entry above it at its level.
    Q_INVOKABLE void outdentEntry(const QString& bookId, const QString& key);  // Up one level.
    Q_INVOKABLE void removeEntry(const QString& bookId, const QString& key);   // With its sub-entries.
    Q_INVOKABLE void restoreEntry(const QString& bookId, const QString& key);
    // A sibling after the entry and its sub-entries; an empty page number means no page.
    Q_INVOKABLE void addEntryAfter(const QString& bookId, const QString& key, const QString& title,
                                   const QString& pageNumber);
    // After a newer analysis changed the entries: keep the edited contents,
    // or show the analysis (also "discard my edits").
    Q_INVOKABLE void keepContentsEdits(const QString& bookId);
    Q_INVOKABLE void useAnalyzedContents(const QString& bookId);
    Q_INVOKABLE bool canIndent(const QString& key) const;
    Q_INVOKABLE bool canOutdent(const QString& key) const;
    Q_INVOKABLE void dismissContentsError() { setContentsError({}); }

    QString bookId() const;
    bool hasBook() const { return m_book.has_value(); }
    bool loading() const { return m_loading; }
    QString title() const { return m_title; }
    QString fileText() const { return m_fileText; }
    bool inTrash() const { return m_inTrash; }
    QVariantList metadataFields() const { return m_metadataFields; }
    QString contentsSummary() const { return m_contentsSummary; }
    QStringList contentsNotes() const { return m_contentsNotes; }
    TocTreeModel* contents() { return &m_contents; }
    QString error() const { return m_error; }
    QVariantList contributorRoles() const;
    bool saving() const { return m_saving > 0; }
    QString correctionError() const { return m_correctionError; }
    bool contentsEdited() const { return m_contentsEdited; }
    bool contentsNeedReconciliation() const { return m_contentsNeedReconciliation; }
    QString contentsEditText() const { return m_contentsEditText; }
    QString contentsError() const { return m_contentsError; }

signals:
    void bookChanged();
    void loadingChanged();
    void detailsChanged();
    void loaded();  // A load for the current selection was applied.
    void savingChanged();
    void correctionErrorChanged();
    void contentsErrorChanged();
    // A correction of `bookId` was saved (its list row and search results change).
    void corrected(const QString& bookId);

private:
    struct Loaded;
    void load();
    void apply(const Loaded& result);
    void clear();
    void setLoading(bool loading);
    void save(const QString& bookId, const QString& field, const domain::MetadataOverride& value);
    void setCorrectionError(const QString& error);
    void setContentsError(const QString& error);
    // Saves a contents change of the shown book against the contents on screen.
    using ContentsChange = std::function<domain::Status(QSqlDatabase&, const domain::BookId&, const domain::TocEditBase&)>;
    void changeContents(const QString& bookId, const ContentsChange& change);
    void editContents(const QString& bookId, const QList<domain::TocEdit>& edits);
    std::optional<int> pageIndexOf(const QString& pageNumber);  // Sets contentsError when invalid.
    const domain::TocEntry* shownEntry(const QString& key) const;
    std::optional<QString> previousSiblingOf(const domain::TocEntry& entry) const;

    std::shared_ptr<catalog::Library> m_library;
    std::optional<domain::BookId> m_book;
    quint64 m_generation = 0;  // Tags loads; only the newest is applied.
    bool m_loading = false;
    std::optional<domain::RunId> m_shownTocRun;            // The contents shown: run and edited revision.
    std::optional<domain::TocRevisionId> m_shownTocRevision;
    QList<domain::TocEntry> m_shownEntries;  // The contents the tree shows.
    std::optional<int> m_pageCount;
    bool m_contentsShown = false;

    QString m_title;
    QString m_fileText;
    bool m_inTrash = false;
    QVariantList m_metadataFields;
    QString m_contentsSummary;
    QStringList m_contentsNotes;
    QString m_error;
    int m_saving = 0;  // Corrections and contents edits in flight.
    QString m_correctionError;
    bool m_contentsEdited = false;
    bool m_contentsNeedReconciliation = false;
    QString m_contentsEditText;
    QString m_contentsError;
    TocTreeModel m_contents;
};

} // namespace mbl::presentation
