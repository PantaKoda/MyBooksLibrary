pragma ComponentBehavior: Bound

// The selected book: metadata with where each value came from and why, and
// the application's table of contents (never the PDF's own bookmarks).
// Display only: all data comes from BookInspector (C++); extracted text is
// always plain text.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

Pane {
    id: pane

    required property BookInspector inspector
    // Development (--inspect-first): make the first entry current when shown.
    property bool selectFirstEntry: false
    padding: 12

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: pane.inspector.title
            textFormat: Text.PlainText
            font.bold: true
            font.pixelSize: 16
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: pane.inspector.error.length > 0 ? pane.inspector.error : pane.inspector.fileText
            textFormat: Text.PlainText
            opacity: 0.7
            elide: Text.ElideMiddle
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
}
