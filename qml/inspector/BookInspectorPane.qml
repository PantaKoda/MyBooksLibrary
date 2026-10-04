pragma ComponentBehavior: Bound

// The selected book: metadata with where each value came from and why, and
// the application's table of contents (never the PDF's own bookmarks).
// Metadata fields can be corrected (MetadataFieldEditor, in a dialog so a
// refresh while typing does not disturb it), and contents entries edited
// (entryDialog); edited contents show a banner, with the choice to keep them
// or use the analysis when a newer analysis differs. All data and commands go through
// BookInspector (C++); extracted text is always plain text.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

Pane {
    id: pane

    required property BookInspector inspector
    // Reading: the book where it was last read, or a page as shown (1 = first).
    signal readRequested()
    signal openPageRequested(int pageNumber)
    // Run the extraction or the contents analysis again for this book.
    signal rerunMetadataRequested()
    signal rerunContentsRequested()
    property bool rerunEnabled: true
    // Save a copy of the PDF with the contents as bookmarks (the Export dialog).
    signal exportRequested()
    property bool exportEnabled: true
    // Organization: the library's collections, the collection the list shows
    // (empty: none), and requests for the shown book.
    property CollectionListModel collections: null
    property string currentCollectionId: ""
    property string currentCollectionName: ""
    property bool organizeEnabled: true
    signal addToCollectionRequested(string collectionId)
    signal removeFromCollectionRequested(string collectionId)
    signal moveToTrashRequested()
    signal restoreRequested()
    // Development (--inspect-first): make the first entry current when shown.
    property bool selectFirstEntry: false
    // The entry to make current again once the tree is rebuilt after an edit.
    property string reselectKey: ""
    padding: 12

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: pane.inspector.title
                textFormat: Text.PlainText
                font.pixelSize: Theme.subtitleSize
                font.weight: Theme.headingWeight
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "inTrashLabel"
                visible: pane.inspector.inTrash
                text: qsTr("In Trash")
                color: Theme.caution
            }
            Button {
                objectName: "restoreBookButton"
                visible: pane.inspector.inTrash
                enabled: pane.organizeEnabled
                text: qsTr("Restore")
                onClicked: pane.restoreRequested()
                Accessible.description: qsTr("Bring the book back from Trash")
            }
            Button {
                text: qsTr("Read")
                highlighted: true  // The main action: an accent button.
                visible: pane.inspector.hasBook && pane.inspector.error.length === 0
                onClicked: pane.readRequested()
                Accessible.description: qsTr("Open the book where you last stopped reading")
            }
            Button {
                id: moreButton
                text: qsTr("More")
                visible: pane.inspector.hasBook && pane.inspector.error.length === 0
                onClicked: moreMenu.open()
                Accessible.description: qsTr("Read this book's title, authors or contents again, save a copy with bookmarks, or organize it")
                Menu {
                    id: moreMenu
                    y: moreButton.height
                    MenuItem {
                        text: qsTr("Read title and authors again")
                        enabled: pane.rerunEnabled && !pane.inspector.inTrash
                        onTriggered: pane.rerunMetadataRequested()
                    }
                    MenuItem {
                        text: qsTr("Analyze contents again")
                        enabled: pane.rerunEnabled && !pane.inspector.inTrash
                        onTriggered: pane.rerunContentsRequested()
                    }
                    MenuItem {
                        objectName: "exportItem"
                        text: qsTr("Save a copy with bookmarks…")
                        enabled: pane.exportEnabled && !pane.inspector.inTrash
                        onTriggered: pane.exportRequested()
                    }
                    MenuSeparator {}
                    Menu {
                        id: addToMenu
                        objectName: "addToCollectionMenu"
                        title: qsTr("Add to collection")
                        enabled: pane.organizeEnabled && !pane.inspector.inTrash
                                 && pane.collections !== null && pane.collections.count > 0
                        Instantiator {
                            model: pane.collections
                            delegate: MenuItem {
                                id: collectionItem
                                required property string collectionId
                                required property string name
                                text: name
                                // A user's name is never markup.
                                contentItem: Label {
                                    leftPadding: collectionItem.indicator ? collectionItem.indicator.width : 0
                                    text: collectionItem.text
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onTriggered: pane.addToCollectionRequested(collectionId)
                            }
                            onObjectAdded: (index, object) => addToMenu.insertItem(index, object as MenuItem)
                            onObjectRemoved: (index, object) => addToMenu.removeItem(object as MenuItem)
                        }
                    }
                    MenuItem {
                        objectName: "removeFromCollectionItem"
                        visible: pane.currentCollectionId.length > 0
                        height: visible ? implicitHeight : 0
                        enabled: pane.organizeEnabled
                        text: qsTr("Remove from this collection")
                        onTriggered: pane.removeFromCollectionRequested(pane.currentCollectionId)
                    }
                    MenuItem {
                        objectName: "moveToTrashItem"
                        enabled: pane.organizeEnabled && !pane.inspector.inTrash
                        text: qsTr("Move to Trash")
                        onTriggered: pane.moveToTrashRequested()
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: pane.inspector.error.length > 0 ? pane.inspector.error : pane.inspector.fileText
            textFormat: Text.PlainText
            color: pane.inspector.error.length > 0 ? Theme.critical : Theme.textSecondary
            elide: Text.ElideMiddle
        }

        // A correction that failed after its editor closed.
        Label {
            Layout.fillWidth: true
            visible: pane.inspector.correctionError.length > 0 && !correctionDialog.visible
            text: pane.inspector.correctionError
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.critical
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            currentIndex: pane.selectFirstEntry ? 1 : 0
            TabButton { text: qsTr("Title and authors") }
            TabButton { text: qsTr("Contents") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ScrollView {
                id: metadataScroll
                clip: true
                contentWidth: availableWidth

                // Metadata: value, where it came from, and the evidence behind it.
                GridLayout {
                    width: metadataScroll.availableWidth
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 4

                    Repeater {
                        model: pane.inspector.metadataFields
                        delegate: ColumnLayout {
                            id: fieldRow
                            required property var modelData
                            required property int index
                            property bool showDetails: false
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: fieldRow.modelData.label
                                    color: Theme.textSecondary
                                    Layout.preferredWidth: 150
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: fieldRow.modelData.value
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                }
                            }
                            // The source line, then the buttons beside it, or under
                            // it when they would leave the line too little room
                            // (a narrow inspector): it wraps, never under a button.
                            GridLayout {
                                id: sourceRow
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                columns: width - fieldButtons.implicitWidth - columnSpacing >= 140 ? 2 : 1
                                rowSpacing: 0
                                Label {
                                    objectName: "source_" + fieldRow.modelData.field
                                    Layout.fillWidth: true
                                    text: fieldRow.modelData.sourceText
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    font.italic: true
                                    color: Theme.textSecondary
                                }
                                RowLayout {
                                    id: fieldButtons
                                    // Named for what it shows ("Show candidates (2)",
                                    // "Show evidence"); absent when there is nothing.
                                    Button {
                                        objectName: "details_" + fieldRow.modelData.field
                                        visible: fieldRow.modelData.detailsLabel.length > 0
                                        flat: true
                                        padding: 2
                                        text: fieldRow.showDetails ? qsTr("Hide") : fieldRow.modelData.detailsLabel
                                        onClicked: fieldRow.showDetails = !fieldRow.showDetails
                                        Accessible.description: qsTr("Show the evidence and candidates for this field")
                                    }
                                    Button {
                                        objectName: "correct_" + fieldRow.modelData.field
                                        flat: true
                                        padding: 2
                                        text: qsTr("Correct")
                                        onClicked: pane.correct(fieldRow.modelData)
                                        Accessible.description: qsTr("Correct %1").arg(fieldRow.modelData.label)
                                    }
                                }
                            }
                            Label {
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                visible: fieldRow.modelData.documentValue.length > 0
                                text: qsTr("The document says: %1").arg(fieldRow.modelData.documentValue)
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: Theme.textSecondary
                                font.pixelSize: Theme.captionSize
                            }
                            // Why the document gave no value, always shown.
                            Label {
                                objectName: "note_" + fieldRow.modelData.field
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                visible: fieldRow.modelData.note.length > 0
                                text: fieldRow.modelData.note
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: Theme.textSecondary
                                font.pixelSize: Theme.captionSize
                            }
                            Label {
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                visible: fieldRow.showDetails && fieldRow.modelData.evidence.length > 0
                                text: fieldRow.modelData.evidence.join("\n")
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: Theme.textSecondary
                                font.pixelSize: Theme.captionSize
                            }
                            Label {
                                objectName: "candidates_" + fieldRow.modelData.field
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                visible: fieldRow.showDetails && fieldRow.modelData.alternatives.length > 0
                                text: qsTr("Other candidates: %1").arg(fieldRow.modelData.alternatives.join("; "))
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: Theme.textSecondary
                                font.pixelSize: Theme.captionSize
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                id: contentsTab
                spacing: Theme.spacingS

                // Contents: the catalog's TOC as a tree, with the reasons per entry.
                Label {
                    Layout.fillWidth: true
                    text: pane.inspector.contentsSummary
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: pane.inspector.contentsNotes.length > 0
                    text: pane.inspector.contentsNotes.join("\n")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                }
                // The analysis's reasons, on request, in a popup over the pane: a
                // long list (one line per entry without a page) never moves the
                // contents tree, whatever the window's size.
                Button {
                    id: reasonsButton
                    objectName: "contentsReasonsButton"
                    visible: pane.inspector.contentsReasons.length > 0
                    flat: true
                    text: qsTr("Analysis notes (%1)").arg(pane.inspector.contentsReasons.length)
                    onClicked: reasonsPopup.opened ? reasonsPopup.close() : reasonsPopup.open()
                    Accessible.description: qsTr("Show what the analysis reported about these contents")
                    Connections {
                        target: pane.inspector
                        function onBookChanged() { reasonsPopup.close() }
                    }
                    Popup {
                        id: reasonsPopup
                        objectName: "contentsReasonsPopup"
                        y: reasonsButton.height
                        width: Math.min(560, pane.width - 24)
                        height: Math.min(reasonsColumn.implicitHeight + topPadding + bottomPadding, pane.height * 0.6)
                        padding: 10
                        contentItem: ScrollView {
                            objectName: "contentsReasons"
                            contentWidth: availableWidth
                            clip: true
                            ColumnLayout {
                                id: reasonsColumn
                                width: parent ? parent.width : implicitWidth
                                spacing: 6
                                Label {
                                    Layout.fillWidth: true
                                    // The analysis's own words; edits made since are not reflected.
                                    text: qsTr("What the analysis reported:")
                                    font.weight: Theme.headingWeight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: pane.inspector.contentsReasons.join("\n")
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }

                // Edited contents: which version is shown, and the user's choices.
                Frame {
                    objectName: "contentsEditBanner"
                    Layout.fillWidth: true
                    visible: pane.inspector.contentsEdited
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            text: pane.inspector.contentsEditText
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font.weight: pane.inspector.contentsNeedReconciliation ? Theme.headingWeight : Font.Normal
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            Button {
                                objectName: "keepEditsButton"
                                visible: pane.inspector.contentsNeedReconciliation
                                text: qsTr("Keep my edits")
                                onClicked: pane.inspector.keepContentsEdits(pane.inspector.bookId)
                                Accessible.description: qsTr("Keep your edited contents instead of the newer analysis")
                            }
                            Button {
                                objectName: "useAnalysisButton"
                                visible: pane.inspector.contentsNeedReconciliation
                                text: qsTr("Use the new analysis")
                                onClicked: pane.inspector.useAnalyzedContents(pane.inspector.bookId)
                                Accessible.description: qsTr("Show the newer analysis instead of your edited contents")
                            }
                            Button {
                                objectName: "discardEditsButton"
                                visible: !pane.inspector.contentsNeedReconciliation
                                flat: true
                                text: qsTr("Discard my edits…")
                                onClicked: discardDialog.open()
                            }
                        }
                    }
                }
                Label {
                    objectName: "contentsErrorLabel"
                    Layout.fillWidth: true
                    visible: pane.inspector.contentsError.length > 0
                    text: pane.inspector.contentsError
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.critical
                }

                TreeView {
                    id: tree
                    objectName: "contentsTree"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 120
                    visible: pane.inspector.contents.entryCount > 0
                    clip: true
                    model: pane.inspector.contents
                    selectionModel: ItemSelectionModel {}
                    keyNavigationEnabled: true
                    ScrollBar.vertical: ScrollBar {}

                    // Everything open by default: a book's contents are short.
                    Connections {
                        target: pane.inspector.contents
                        function onModelReset() {
                            Qt.callLater(tree.expandRecursively)
                            if (pane.reselectKey.length > 0) {
                                const key = pane.reselectKey
                                pane.reselectKey = ""
                                Qt.callLater(() => tree.selectionModel.setCurrentIndex(
                                                 pane.inspector.contents.indexOfEntry(key), ItemSelectionModel.NoUpdate))
                            } else if (pane.selectFirstEntry) {
                                Qt.callLater(() => tree.selectionModel.setCurrentIndex(tree.index(0, 0), ItemSelectionModel.NoUpdate))
                            }
                        }
                    }
                    Component.onCompleted: Qt.callLater(tree.expandRecursively)

                    // A compact row: indentation by depth, an expand toggle for
                    // entries with children, the title, and the page on the right.
                    delegate: Item {
                        id: entry
                        required property TreeView treeView
                        required property bool isTreeNode
                        required property bool expanded
                        required property bool hasChildren
                        required property int depth
                        required property int row
                        required property bool current
                        required property string title
                        required property string pageText
                        required property bool uncertain
                        required property bool removed

                        implicitWidth: tree.width - 12
                        implicitHeight: rowContent.implicitHeight + 6

                        // The current entry as the style shows a selected list row:
                        // a subtle fill, an accent bar, and the text unchanged.
                        Rectangle {
                            anchors.fill: parent
                            visible: entry.current
                            radius: Theme.controlRadius
                            color: Theme.selectedFill
                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.selectionBarWidth
                                height: Math.min(16, parent.height - 4)
                                radius: width / 2
                                color: pane.palette.accent
                            }
                        }
                        RowLayout {
                            id: rowContent
                            anchors.fill: parent
                            anchors.leftMargin: 4 + entry.depth * 16
                            anchors.rightMargin: 4
                            spacing: 6
                            Label {
                                Layout.preferredWidth: 12
                                text: entry.hasChildren ? (entry.expanded ? "\u25BE" : "\u25B8") : ""
                                TapHandler {
                                    enabled: entry.hasChildren
                                    onTapped: entry.treeView.toggleExpanded(entry.row)
                                }
                            }
                            Label {
                                objectName: "entryTitle"
                                Layout.fillWidth: true
                                text: entry.title
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                font.strikeout: entry.removed
                                color: entry.removed ? Theme.textSecondary : pane.palette.windowText
                            }
                            Label {
                                objectName: "entryPage"
                                // At most half the row, so the title always shows.
                                Layout.maximumWidth: rowContent.width / 2
                                text: entry.pageText
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                font.italic: entry.uncertain
                                // An uncertain page stands out; a confirmed one is quieter.
                                color: entry.uncertain ? Theme.caution : Theme.textSecondary
                            }
                        }
                        TapHandler {
                            onTapped: tree.selectionModel.setCurrentIndex(tree.index(entry.row, 0), ItemSelectionModel.NoUpdate)
                        }
                    }
                }

                // The current entry's reasons.
                Rectangle {
                    id: detailsFrame
                    objectName: "entryDetails"
                    color: Theme.cardFill
                    border.color: Theme.cardStroke
                    radius: Theme.controlRadius
                    implicitHeight: detailsColumn.implicitHeight + 2 * Theme.spacingS
                    Layout.fillWidth: true
                    // At most half the tab (and at least room for a few lines):
                    // a long list of reasons or many actions scroll inside the
                    // card, never under the window's status bar.
                    Layout.maximumHeight: Math.max(140, contentsTab.height / 2)
                    visible: tree.visible
                    // Rebuilding the tree (another book, a new contents run) clears the
                    // current index without a signal, so also depend on entryCount, whose
                    // entriesChanged is emitted on every rebuild: never show an entry of
                    // the previous tree.
                    property var entry: pane.inspector.contents.entryCount >= 0
                                        ? pane.inspector.contents.entryAt(tree.selectionModel.currentIndex) : ({})
                    property bool showTechnical: false

                    ScrollView {
                        id: detailsScroll
                        anchors.fill: parent
                        anchors.margins: Theme.spacingS
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            id: detailsColumn
                            width: detailsScroll.availableWidth
                            spacing: 2
                            Label {
                                objectName: "entryHeading"
                                Layout.fillWidth: true
                                text: detailsFrame.entry.title !== undefined
                                      ? qsTr("%1 · %2").arg(detailsFrame.entry.title).arg(detailsFrame.entry.pageText)
                                      : qsTr("Select an entry to see where it points and why.")
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                font.weight: detailsFrame.entry.title !== undefined ? Theme.headingWeight : Font.Normal
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: detailsFrame.entry.stateText !== undefined
                                text: detailsFrame.entry.stateText ?? ""
                                textFormat: Text.PlainText
                                color: Theme.textSecondary
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: (detailsFrame.entry.editedText ?? "").length > 0
                                text: detailsFrame.entry.editedText ?? ""
                                textFormat: Text.PlainText
                                font.italic: true
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: (detailsFrame.entry.detail ?? "").length > 0
                                text: detailsFrame.entry.detail ?? ""
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                            }
                            // Two different actions: the chapter itself, or the page
                            // where the contents list it (for entries without a page).
                            RowLayout {
                                spacing: 8
                                Button {
                                    objectName: "openChapterButton"
                                    visible: (detailsFrame.entry.page ?? -1) > 0
                                    text: qsTr("Open chapter")
                                    onClicked: pane.openPageRequested(detailsFrame.entry.page)
                                    Accessible.description: qsTr("Open the book at this chapter's page")
                                }
                                Button {
                                    objectName: "showSourcePageButton"
                                    visible: (detailsFrame.entry.sourcePage ?? -1) > 0
                                    flat: (detailsFrame.entry.page ?? -1) > 0
                                    text: qsTr("Show contents page %1").arg(detailsFrame.entry.sourcePage ?? 0)
                                    onClicked: pane.openPageRequested(detailsFrame.entry.sourcePage)
                                    Accessible.description: qsTr("Open the page where the contents list this entry")
                                }
                            }
                            // Editing the entry: saved as a new version of the contents.
                            Flow {
                                id: entryActions
                                Layout.fillWidth: true
                                visible: detailsFrame.entry.entryId !== undefined
                                spacing: 4
                                readonly property string key: detailsFrame.entry.entryId ?? ""
                                readonly property bool removed: detailsFrame.entry.removed ?? false
                                Button {
                                    objectName: "renameEntryButton"
                                    visible: !entryActions.removed
                                    text: qsTr("Rename…")
                                    onClicked: pane.editEntry("rename", detailsFrame.entry)
                                }
                                Button {
                                    objectName: "setPageButton"
                                    visible: !entryActions.removed
                                    text: (detailsFrame.entry.page ?? -1) > 0 ? qsTr("Change page…") : qsTr("Set page…")
                                    onClicked: pane.editEntry("page", detailsFrame.entry)
                                }
                                Button {
                                    objectName: "clearPageButton"
                                    visible: !entryActions.removed && (detailsFrame.entry.page ?? -1) > 0
                                    text: qsTr("No page")
                                    onClicked: {
                                        pane.reselectKey = entryActions.key
                                        pane.inspector.clearEntryPage(pane.inspector.bookId, entryActions.key)
                                    }
                                }
                                Button {
                                    objectName: "indentButton"
                                    visible: !entryActions.removed
                                    enabled: pane.inspector.contents.entryCount >= 0 && pane.inspector.canIndent(entryActions.key)
                                    text: qsTr("Indent")
                                    onClicked: {
                                        pane.reselectKey = entryActions.key
                                        pane.inspector.indentEntry(pane.inspector.bookId, entryActions.key)
                                    }
                                    Accessible.description: qsTr("Make it a sub-entry of the entry above it")
                                }
                                Button {
                                    objectName: "outdentButton"
                                    visible: !entryActions.removed
                                    enabled: pane.inspector.contents.entryCount >= 0 && pane.inspector.canOutdent(entryActions.key)
                                    text: qsTr("Outdent")
                                    onClicked: {
                                        pane.reselectKey = entryActions.key
                                        pane.inspector.outdentEntry(pane.inspector.bookId, entryActions.key)
                                    }
                                    Accessible.description: qsTr("Move it up one level")
                                }
                                Button {
                                    objectName: "addEntryButton"
                                    visible: !entryActions.removed
                                    text: qsTr("Add after…")
                                    onClicked: pane.editEntry("add", detailsFrame.entry)
                                    Accessible.description: qsTr("Add a new entry after this one, at the same level")
                                }
                                Button {
                                    objectName: "removeEntryButton"
                                    text: entryActions.removed ? qsTr("Restore") : qsTr("Remove")
                                    onClicked: {
                                        pane.reselectKey = entryActions.key
                                        if (entryActions.removed)
                                            pane.inspector.restoreEntry(pane.inspector.bookId, entryActions.key)
                                        else
                                            pane.inspector.removeEntry(pane.inspector.bookId, entryActions.key)
                                    }
                                    Accessible.description: entryActions.removed ? qsTr("Bring the entry back")
                                                                           : qsTr("Remove the entry and its sub-entries from the contents and search")
                                }
                            }
                            Button {
                                visible: detailsFrame.entry.technical !== undefined
                                flat: true
                                text: detailsFrame.showTechnical ? qsTr("Hide technical details") : qsTr("Technical details")
                                onClicked: detailsFrame.showTechnical = !detailsFrame.showTechnical
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: detailsFrame.showTechnical && detailsFrame.entry.technical !== undefined
                                text: detailsFrame.entry.technical ?? ""
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                font.family: "monospace"
                                font.pixelSize: Theme.captionSize
                                color: Theme.textSecondary
                            }
                        }
                    }
                }
            }
        }
    }

    // Opens the entry editor: "rename", "page" or "add", on a copy of the entry.
    function editEntry(mode, entryData) {
        pane.inspector.dismissContentsError()
        entryDialog.mode = mode
        entryDialog.bookId = pane.inspector.bookId
        entryDialog.entryKey = entryData.entryId
        entryDialog.entryTitle = entryData.title
        entryDialog.open()
        entryTitleField.text = mode === "rename" ? entryData.title : ""
        entryPageField.text = mode === "page" && entryData.page > 0 ? String(entryData.page) : ""
        if (mode === "page")
            entryPageField.forceActiveFocus()
        else
            entryTitleField.forceActiveFocus()
    }

    Dialog {
        id: entryDialog
        objectName: "entryDialog"
        property string mode: "rename"
        property string bookId
        property string entryKey
        property string entryTitle
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, (parent ? parent.width : 480) - 32)
        modal: true
        title: mode === "rename" ? qsTr("Rename entry")
             : mode === "page" ? qsTr("Page of “%1”").arg(entryTitle)
             : qsTr("Add an entry after “%1”").arg(entryTitle)

        function save() {
            pane.reselectKey = entryDialog.entryKey
            if (entryDialog.mode === "rename")
                pane.inspector.renameEntry(entryDialog.bookId, entryDialog.entryKey, entryTitleField.text)
            else if (entryDialog.mode === "page")
                pane.inspector.setEntryPage(entryDialog.bookId, entryDialog.entryKey, entryPageField.text)
            else
                pane.inspector.addEntryAfter(entryDialog.bookId, entryDialog.entryKey, entryTitleField.text, entryPageField.text)
            // A refused value keeps the dialog open with the reason.
            if (pane.inspector.contentsError.length === 0)
                entryDialog.close()
        }

        contentItem: ColumnLayout {
            spacing: 6
            Label {
                visible: entryDialog.mode !== "page"
                text: qsTr("Title")
            }
            TextField {
                id: entryTitleField
                objectName: "entryTitleField"
                visible: entryDialog.mode !== "page"
                Layout.fillWidth: true
                Accessible.name: qsTr("Title")
                Keys.onReturnPressed: entryDialog.save()
                Keys.onEnterPressed: entryDialog.save()
            }
            Label {
                visible: entryDialog.mode !== "rename"
                text: entryDialog.mode === "add" ? qsTr("Page (optional, as shown in the reader)")
                                                 : qsTr("Page, as shown in the reader (1 = first page)")
            }
            TextField {
                id: entryPageField
                objectName: "entryPageField"
                visible: entryDialog.mode !== "rename"
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
                Accessible.name: qsTr("Page")
                Keys.onReturnPressed: entryDialog.save()
                Keys.onEnterPressed: entryDialog.save()
            }
            Label {
                Layout.fillWidth: true
                visible: pane.inspector.contentsError.length > 0
                text: pane.inspector.contentsError
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.critical
            }
            RowLayout {
                Button {
                    objectName: "entrySaveButton"
                    text: qsTr("Save")
                    highlighted: true
                    onClicked: entryDialog.save()
                }
                Button {
                    text: qsTr("Cancel")
                    onClicked: entryDialog.close()
                }
            }
        }
    }

    Dialog {
        id: discardDialog
        objectName: "discardDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, (parent ? parent.width : 440) - 32)
        modal: true
        title: qsTr("Discard your edits to the contents?")
        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                text: qsTr("The analyzed contents will be shown and searched again. Your earlier versions stay in the library.")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "confirmDiscardButton"
                text: qsTr("Discard edits")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: pane.inspector.useAnalyzedContents(pane.inspector.bookId)
    }

    // Opens the editor for one field of the shown book, on a copy of its data.
    function correct(fieldData) {
        pane.inspector.dismissCorrectionError()
        correctionDialog.bookId = pane.inspector.bookId
        correctionDialog.fieldData = Object.assign({}, fieldData)
        correctionDialog.open()
    }

    Dialog {
        id: correctionDialog
        objectName: "correctionDialog"
        property string bookId
        property var fieldData: ({})
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, (parent ? parent.width : 520) - 32)
        modal: true
        title: qsTr("Correct: %1").arg(fieldData.label ?? "")
        // A new editor for every opening, from the data copied at that time.
        contentItem: Loader {
            active: correctionDialog.visible
            sourceComponent: MetadataFieldEditor {
                inspector: pane.inspector
                bookId: correctionDialog.bookId
                fieldData: correctionDialog.fieldData
                // Room left in the window for the rows after the dialog's
                // title, other fields and buttons (about 260 px).
                maximumRowsHeight: Math.max(120, (correctionDialog.parent ? correctionDialog.parent.height : 640) - 260)
                onDone: correctionDialog.close()
            }
        }
    }
}
