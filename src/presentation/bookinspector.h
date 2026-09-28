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
#pragma once

#include "domain/book.h"
#include "domain/metadata.h"
#include "presentation/toctreemodel.h"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

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

    QString bookId() const;
    bool hasBook() const { return m_book.has_value(); }
    bool loading() const { return m_loading; }
    QString title() const { return m_title; }
    QString fileText() const { return m_fileText; }
    QVariantList metadataFields() const { return m_metadataFields; }
    QString contentsSummary() const { return m_contentsSummary; }
    QStringList contentsNotes() const { return m_contentsNotes; }
    TocTreeModel* contents() { return &m_contents; }
    QString error() const { return m_error; }
    QVariantList contributorRoles() const;
    bool saving() const { return m_saving > 0; }
    QString correctionError() const { return m_correctionError; }

signals:
    void bookChanged();
    void loadingChanged();
    void detailsChanged();
    void loaded();  // A load for the current selection was applied.
    void savingChanged();
    void correctionErrorChanged();
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

    std::shared_ptr<catalog::Library> m_library;
    std::optional<domain::BookId> m_book;
    quint64 m_generation = 0;  // Tags loads; only the newest is applied.
    bool m_loading = false;
    std::optional<domain::RunId> m_shownTocRun;
    bool m_contentsShown = false;

    QString m_title;
    QString m_fileText;
    QVariantList m_metadataFields;
    QString m_contentsSummary;
    QStringList m_contentsNotes;
    QString m_error;
    int m_saving = 0;  // Corrections in flight.
    QString m_correctionError;
    TocTreeModel m_contents;
};

} // namespace mbl::presentation
