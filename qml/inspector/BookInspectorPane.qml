pragma ComponentBehavior: Bound

// The selected book: metadata with where each value came from and why, and
// the application's table of contents (never the PDF's own bookmarks).
// Metadata fields can be corrected (MetadataFieldEditor, in a dialog so a
// refresh while typing does not disturb it). All data and commands go through
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
    // Development (--inspect-first): make the first entry current when shown.
    property bool selectFirstEntry: false
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
                font.bold: true
                font.pixelSize: 16
                wrapMode: Text.Wrap
            }
            Button {
                text: qsTr("Read")
                visible: pane.inspector.hasBook && pane.inspector.error.length === 0
                onClicked: pane.readRequested()
                Accessible.description: qsTr("Open the book where you last stopped reading")
            }
            Button {
                id: moreButton
                text: qsTr("More")
                visible: pane.inspector.hasBook && pane.inspector.error.length === 0
                onClicked: moreMenu.open()
                Accessible.description: qsTr("Read this book's title, authors or contents again")
                Menu {
                    id: moreMenu
                    y: moreButton.height
                    MenuItem {
                        text: qsTr("Read title and authors again")
                        enabled: pane.rerunEnabled
                        onTriggered: pane.rerunMetadataRequested()
                    }
                    MenuItem {
                        text: qsTr("Analyze contents again")
                        enabled: pane.rerunEnabled
                        onTriggered: pane.rerunContentsRequested()
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: pane.inspector.error.length > 0 ? pane.inspector.error : pane.inspector.fileText
            textFormat: Text.PlainText
            opacity: 0.7
            elide: Text.ElideMiddle
        }

        // A correction that failed after its editor closed.
        Label {
            Layout.fillWidth: true
            visible: pane.inspector.correctionError.length > 0 && !correctionDialog.visible
            text: pane.inspector.correctionError
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: "firebrick"
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
                            property bool showWhy: false
                            readonly property bool hasWhy: modelData.evidence.length > 0 || modelData.alternatives.length > 0
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: fieldRow.modelData.label
                                    opacity: 0.7
                                    Layout.preferredWidth: 150
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: fieldRow.modelData.value
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                }
                            }
                            RowLayout {
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: fieldRow.modelData.sourceText
                                    textFormat: Text.PlainText
                                    font.italic: true
                                    opacity: 0.6
                                }
                                Button {
                                    visible: fieldRow.hasWhy
                                    flat: true
                                    padding: 2
                                    text: fieldRow.showWhy ? qsTr("Hide") : qsTr("Why?")
                                    onClicked: fieldRow.showWhy = !fieldRow.showWhy
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
                            Label {
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                visible: fieldRow.modelData.documentValue.length > 0
                                text: qsTr("The document says: %1").arg(fieldRow.modelData.documentValue)
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                opacity: 0.6
                                font.pixelSize: 11
                            }
                            Label {
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                visible: fieldRow.showWhy && fieldRow.modelData.evidence.length > 0
                                text: fieldRow.modelData.evidence.join("\n")
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                opacity: 0.6
                                font.pixelSize: 11
                            }
                            Label {
                                Layout.leftMargin: 156
                                Layout.fillWidth: true
                                visible: fieldRow.showWhy && fieldRow.modelData.alternatives.length > 0
                                text: qsTr("Other candidates: %1").arg(fieldRow.modelData.alternatives.join("; "))
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                opacity: 0.6
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 8

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
                    opacity: 0.7
                }

                TreeView {
                    id: tree
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
                            if (pane.selectFirstEntry)
                                Qt.callLater(() => tree.selectionModel.setCurrentIndex(tree.index(0, 0), ItemSelectionModel.NoUpdate))
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

                        implicitWidth: tree.width - 12
                        implicitHeight: rowContent.implicitHeight + 6

                        Rectangle {
                            anchors.fill: parent
                            visible: entry.current
                            color: pane.palette.highlight
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
                                color: entry.current ? pane.palette.highlightedText : pane.palette.windowText
                                TapHandler {
                                    enabled: entry.hasChildren
                                    onTapped: entry.treeView.toggleExpanded(entry.row)
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: entry.title
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: entry.current ? pane.palette.highlightedText : pane.palette.windowText
                            }
                            Label {
                                text: entry.pageText
                                textFormat: Text.PlainText
                                font.italic: entry.uncertain
                                color: entry.current ? pane.palette.highlightedText : pane.palette.windowText
                                opacity: entry.uncertain || entry.current ? 1.0 : 0.6
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
                    color: "transparent"
                    border.color: pane.palette.mid
                    radius: 2
                    implicitHeight: detailsColumn.implicitHeight + 16
                    Layout.fillWidth: true
                    visible: tree.visible
                    // Rebuilding the tree (another book, a new contents run) clears the
                    // current index without a signal, so also depend on entryCount, whose
                    // entriesChanged is emitted on every rebuild: never show an entry of
                    // the previous tree.
                    property var entry: pane.inspector.contents.entryCount >= 0
                                        ? pane.inspector.contents.entryAt(tree.selectionModel.currentIndex) : ({})
                    property bool showTechnical: false

                    ColumnLayout {
                        id: detailsColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 2
                        Label {
                            objectName: "entryHeading"
                            Layout.fillWidth: true
                            text: detailsFrame.entry.title !== undefined
                                  ? qsTr("%1 · %2").arg(detailsFrame.entry.title).arg(detailsFrame.entry.pageText)
                                  : qsTr("Select an entry to see where it points and why.")
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font.bold: detailsFrame.entry.title !== undefined
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: detailsFrame.entry.stateText !== undefined
                            text: detailsFrame.entry.stateText ?? ""
                            textFormat: Text.PlainText
                            opacity: 0.7
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
                            font.pixelSize: 11
                            opacity: 0.7
                        }
                    }
                }
            }
        }
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
