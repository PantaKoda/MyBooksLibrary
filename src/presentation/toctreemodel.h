// Presentation: the application's table of contents of one book as a tree
// (never the PDF's own bookmarks). GUI-thread owned; built from the catalog's
// entries in two passes, so a parent may be listed after its child. An entry
// whose parent is missing, itself, or part of a cycle is shown at the top
// level and marked, never attached to an invented parent; Unknown hierarchy
// stays at the top level too.
//
// Edited contents (catalog revisions) show what the user changed: a page or
// level the user set replaces the analysis's reasons for it, and a removed
// entry stays in the tree, marked, so it can be restored.
#pragma once

#include "domain/toc.h"

#include <QAbstractItemModel>
#include <QList>
#include <QQmlEngine>
#include <QVariantMap>

namespace mbl::presentation {

class TocTreeModel : public QAbstractItemModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BookInspector.contents.")
    Q_PROPERTY(int entryCount READ entryCount NOTIFY entriesChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        EntryIdRole,        // Run-scoped SDK entry ID; the entry's key in edited contents.
        PageTextRole,       // "Page 4", "Page 5 or 9?", "Page not found" (physical page = index + 1).
        PageRole,           // Physical page number (index + 1) of a resolved entry, else -1.
        SourcePageRole,     // Physical page the entry is printed on, else -1.
        PrintedLabelRole,   // As printed, e.g. "iv" or "12"; empty if none.
        StateTextRole,      // One line: destination and hierarchy in plain language.
        UncertainRole,      // True when the page or the level is not certain.
        InPlanRole,         // In the bookmark plan (draft or ready).
        DetailRole,         // Reasons, one per line, in plain language.
        TechnicalRole,      // Method codes and diagnostics.
        RemovedRole,        // Removed by the user (kept, not searched).
        EditedTextRole,     // What the user changed, in plain language; empty if nothing.
    };

    explicit TocTreeModel(QObject* parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replaces the tree (model reset).
    void setEntries(const QList<domain::TocEntry>& entries);
    int entryCount() const { return int(m_entries.size()); }

    // All roles of one entry, by name, for a details pane.
    Q_INVOKABLE QVariantMap entryAt(const QModelIndex& index) const;
    // The index of the entry with that key (EntryIdRole), or an invalid index.
    Q_INVOKABLE QModelIndex indexOfEntry(const QString& key) const;

    static QString pageText(const domain::TocEntry& entry);

signals:
    void entriesChanged();

private:
    struct Node {
        int parent = -1;           // Index into m_entries, or -1 for the top level.
        QList<int> children;       // In entry order.
        bool parentProblem = false;  // Named a parent that is missing, itself, or in a cycle.
    };
    int rowOf(int entry) const;

    QList<domain::TocEntry> m_entries;
    QList<Node> m_nodes;   // Parallel to m_entries.
    QList<int> m_roots;    // In entry order.
};

} // namespace mbl::presentation
