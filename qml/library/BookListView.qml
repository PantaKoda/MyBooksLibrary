pragma ComponentBehavior: Bound

// The book list of the current view (the library, a collection or Trash).
// The selection is kept by book ID: whenever rows change (a refresh, a book
// moved to Trash or out of the shown collection, another view), the
// selected book is found again or the selection is cleared, so the list and
// the inspector never show different books. Display only.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

ListView {
    id: bookList

    required property LibraryController library
    // Whether the selection drives the inspector (not while search results do).
    property bool drivesInspector: true
    // Development (--inspect-first): select the first book once there is one.
    property bool inspectFirst: false
    property string selectedBookId: ""
    signal openRequested(string bookId)

    leftMargin: 8
    topMargin: 8
    clip: true
    focus: true
    spacing: 2
    model: library.books
    keyNavigationEnabled: true
    currentIndex: -1
    ScrollBar.vertical: ScrollBar {}

    delegate: ItemDelegate {
        id: row
        required property int index
        required property string bookId
        required property string title
        required property bool titleFromFileName
        required property string contributors
        required property string processingState
        required property string activity  // "running", "waiting" or "".

        objectName: "bookRow_" + index
        width: ListView.view.width
        highlighted: ListView.isCurrentItem
        onClicked: bookList.currentIndex = index
        onDoubleClicked: bookList.openRequested(row.bookId)

        contentItem: RowLayout {
            spacing: Theme.spacingS
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: row.title
                    textFormat: Text.PlainText   // Extracted text is never markup.
                    // The style draws the selection (Fluent: a subtle fill and an
                    // accent bar) and keeps the text colour.
                    font.weight: Theme.headingWeight
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: row.titleFromFileName
                          ? qsTr("From the file name · %1").arg(row.processingState)
                          : (row.contributors.length > 0 ? row.contributors + " · " + row.processingState
                                                         : row.processingState)
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }
            }
            // This book's activity: a spinning arc while its title or contents
            // are being read, a still ring while it waits its turn. Words
            // (processingState) say the same; this only makes it visible at a glance.
            Item {
                objectName: "bookActivity_" + row.index
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignVCenter
                visible: row.activity.length > 0
                ToolTip.visible: activityHover.hovered
                ToolTip.delay: 300
                ToolTip.text: row.activity === "running" ? qsTr("Being processed") : qsTr("Waiting its turn")
                HoverHandler { id: activityHover }
                Accessible.role: Accessible.Indicator
                Accessible.name: ToolTip.text

                // Waiting: a faint ring.
                Rectangle {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    radius: 7
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.tint(0.45)
                    visible: row.activity === "waiting"
                }
                // Running: an accent arc that turns.
                Canvas {
                    id: arc
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    visible: row.activity === "running"
                    readonly property color stroke: Theme.accent
                    onStrokeChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.lineWidth = 2
                        ctx.lineCap = "round"
                        ctx.strokeStyle = Theme.tint(0.2)
                        ctx.beginPath()
                        ctx.arc(width / 2, height / 2, width / 2 - 1.5, 0, 2 * Math.PI)
                        ctx.stroke()
                        ctx.strokeStyle = stroke
                        ctx.beginPath()
                        ctx.arc(width / 2, height / 2, width / 2 - 1.5, 0, Math.PI * 0.6)
                        ctx.stroke()
                    }
                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 900
                        loops: Animation.Infinite
                        running: arc.visible
                    }
                }
            }
        }
    }

    onCurrentIndexChanged: {
        selectedBookId = model ? model.bookIdAt(currentIndex) : ""
        if (drivesInspector)
            library.inspector.select(selectedBookId)
    }
    onCountChanged: {
        if (inspectFirst && count > 0 && currentIndex < 0)
            currentIndex = 0
    }

    // The row of the selected book, or none if it is no longer listed. Rows
    // change without a model reset (row-level updates keep the scroll
    // position), and ListView may keep the same index for a different book.
    function resync() {
        const row = library.books.rowOfBook(selectedBookId)
        if (row !== currentIndex)
            currentIndex = row
    }
    Connections {
        target: bookList.library.books
        function onModelReset() { bookList.resync() }
        function onRowsRemoved() { Qt.callLater(bookList.resync) }
        function onRowsInserted() { Qt.callLater(bookList.resync) }
        function onRowsMoved() { Qt.callLater(bookList.resync) }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width * 0.7
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        visible: bookList.count === 0
        color: Theme.textSecondary
        text: bookList.library.opening ? qsTr("Opening the library…")
              : bookList.library.failed ? qsTr("The library could not be opened.")
              : bookList.library.view === LibraryController.Trash ? qsTr("Trash is empty.")
              : bookList.library.view === LibraryController.Collection
                ? qsTr("No books in this collection yet. Select a book and use More → Add to collection.")
              : qsTr("No books yet. Choose “Import PDFs…” or drop PDF files here.")
    }
}
