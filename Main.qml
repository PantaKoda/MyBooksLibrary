pragma ComponentBehavior: Bound

// The library window: list-first. All work happens in LibraryController
// (C++); this file only shows its state and calls its commands.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBooksLibrary.Presentation

ApplicationWindow {
    id: window

    required property LibraryController library
    property bool closeRequested: false

    width: 900
    height: 640
    minimumWidth: 480
    minimumHeight: 360
    visible: true
    title: qsTr("MyBooksLibrary")

    property bool showActivity: false
    // Development (--inspect-first): select the first book once the list has one.
    property bool inspectFirst: false
    // Development (--correct <field>): open that field's correction editor
    // once the inspector shows a book.
    property string correctFirst: ""
    Connections {
        target: window.library.inspector
        enabled: window.correctFirst.length > 0
        function onLoaded() {
            const field = window.library.inspector.metadataFields.find(f => f.field === window.correctFirst)
            if (field) {
                inspectorPane.correct(field)
                window.correctFirst = ""
            }
        }
    }

    // Closing while work runs: cancel imports and stop processing (queued
    // metadata jobs resume next time), stay responsive, close when idle.
    // An open book is closed through the reader first (its view is destroyed,
    // then its document closed), never by the engine's teardown, which would
    // destroy the document before the view (docs/READER.md, "Teardown").
    onClosing: (close) => {
        if (window.library.busy || window.library.reader.open) {
            close.accepted = false
            window.closeRequested = true
            window.library.prepareToClose()  // Also saves the reading position.
            window.library.reader.close()
        }
    }
    function closeWhenIdle() {
        if (window.closeRequested && !window.library.busy && !window.library.reader.open)
            window.close()
    }
    // Deferred: never close again from inside the closing handler.
    Connections {
        target: window.library
        function onBusyChanged() { Qt.callLater(window.closeWhenIdle) }
    }
    Connections {
        target: window.library.reader
        function onOpenChanged() { Qt.callLater(window.closeWhenIdle) }
    }

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: searchField.forceActiveFocus()
    }
    // Leaving search shows the book selected in the list again.
    Connections {
        target: window.library.search
        function onTextChanged() {
            if (!window.library.search.active)
                window.library.inspector.select(bookList.selectedBookId)
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import PDF files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: window.library.importUrls(selectedFiles)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            Label {
                objectName: "viewTitleLabel"
                text: window.library.viewTitle
                textFormat: Text.PlainText
                font.bold: true
                font.pixelSize: 16
                elide: Text.ElideRight
                Layout.maximumWidth: 220
            }
            Label {
                Layout.preferredWidth: 160
                text: window.library.libraryPath
                elide: Text.ElideMiddle
                opacity: 0.6
            }
            TextField {
                id: searchField
                Layout.fillWidth: true
                Layout.minimumWidth: 160
                placeholderText: qsTr("Search titles, authors and contents")
                enabled: window.library.ready
                text: window.library.search.text
                onTextEdited: window.library.search.text = text
                Keys.onEscapePressed: window.library.search.clear()
                Accessible.name: qsTr("Search")
                Accessible.description: qsTr("Searches book titles, authors and contents entries, not the full text")
            }
            ComboBox {
                id: scopeBox
                enabled: window.library.ready
                model: [qsTr("All"), qsTr("Titles"), qsTr("Authors"), qsTr("Contents")]
                currentIndex: window.library.search.scope
                onActivated: (index) => window.library.search.scope = index
                Accessible.name: qsTr("Search in")
            }
            Button {
                id: importButton
                text: qsTr("Import PDFs…")
                enabled: window.library.ready && !window.closeRequested
                onClicked: importDialog.open()
                Accessible.description: qsTr("Choose PDF files to copy into the library")
            }
        }
    }

    // The library, or the open book in the reader.
    StackLayout {
        anchors.fill: parent
        currentIndex: window.library.reader.open ? 1 : 0

        ColumnLayout {
            spacing: 0

            DropArea {
                id: dropArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                enabled: window.library.ready && !window.closeRequested
                onDropped: (drop) => {
                    if (drop.hasUrls) {
                        window.library.importUrls(drop.urls)
                        drop.acceptProposedAction()
                    }
                }

                // The views (library, collections, Trash), the book list, and
                // the selected book's inspector beside it.
                SplitView {
                    anchors.fill: parent
                    orientation: Qt.Horizontal

                LibrarySidebar {
                    library: window.library
                    enabledActions: window.library.ready && !window.closeRequested
                    SplitView.preferredWidth: 190
                    SplitView.minimumWidth: 140
                }

                Item {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 240

                    BookListView {
                        id: bookList
                        anchors.fill: parent
                        library: window.library
                        inspectFirst: window.inspectFirst
                        visible: !window.library.search.active
                        drivesInspector: !window.library.search.active
                        onOpenRequested: (bookId) => window.library.reader.openBook(bookId)
                    }

                    SearchResultsView {
                        id: searchResults
                        anchors.fill: parent
                        anchors.margins: 8
                        visible: window.library.search.active
                        search: window.library.search
                        selectFirst: window.inspectFirst
                        onBookChosen: (bookId) => window.library.inspector.select(bookId)
                        onOpenPageRequested: (bookId, pageNumber) => window.library.reader.openPageNumber(bookId, pageNumber)
                    }
                }

                BookInspectorPane {
                    id: inspectorPane
                    inspector: window.library.inspector
                    onReadRequested: window.library.reader.openBook(window.library.inspector.bookId)
                    onOpenPageRequested: (pageNumber) => window.library.reader.openPageNumber(window.library.inspector.bookId, pageNumber)
                    onRerunMetadataRequested: window.library.rerunMetadata(window.library.inspector.bookId)
                    onRerunContentsRequested: window.library.rerunContents(window.library.inspector.bookId)
                    rerunEnabled: window.library.processingAvailable && !window.closeRequested
                    collections: window.library.collections
                    currentCollectionId: window.library.viewCollectionId
                    currentCollectionName: window.library.view === LibraryController.Collection ? window.library.viewTitle : ""
                    organizeEnabled: window.library.ready && !window.closeRequested
                    onAddToCollectionRequested: (collectionId) => window.library.addToCollection(collectionId, window.library.inspector.bookId)
                    onRemoveFromCollectionRequested: (collectionId) => window.library.removeFromCollection(collectionId, window.library.inspector.bookId)
                    onMoveToTrashRequested: window.library.moveToTrash(window.library.inspector.bookId)
                    onRestoreRequested: window.library.restoreFromTrash(window.library.inspector.bookId)
                    selectFirstEntry: window.inspectFirst
                    visible: window.library.inspector.hasBook
                    SplitView.preferredWidth: Math.max(320, window.width * 0.55)
                    SplitView.minimumWidth: 280
                }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: dropArea.containsDrag
                    color: window.palette.highlight
                    opacity: 0.15
                }
            }

            // Processing activity: metadata and contents jobs with Cancel and Retry.
            Pane {
                id: activityPane
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                visible: window.showActivity
                padding: 8

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Activity")
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            text: window.library.processingAvailable && !window.library.ocrAvailable
                                  ? qsTr("OCR models were not found: title pages that are scanned images cannot be read.")
                                  : ""
                            wrapMode: Text.WordWrap
                            opacity: 0.7
                        }
                        Button {
                            text: qsTr("Cancel all")
                            visible: window.library.jobs.pendingCount > 0
                            enabled: !window.closeRequested
                            onClicked: window.library.cancelAllJobs()
                            Accessible.description: qsTr("Cancel every waiting and running job")
                        }
                    }

                    ListView {
                        id: jobList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: window.library.jobs
                        keyNavigationEnabled: true
                        ScrollBar.vertical: ScrollBar {}

                        delegate: ItemDelegate {
                            id: jobRow
                            required property int index
                            required property string jobId
                            required property string bookTitle
                            required property string kindText
                            required property string stateText
                            required property string detail
                            required property bool running
                            required property bool canCancel
                            required property bool canRetry

                            width: ListView.view.width
                            highlighted: ListView.isCurrentItem
                            onClicked: jobList.currentIndex = index

                            contentItem: RowLayout {
                                spacing: 8
                                BusyIndicator {
                                    running: jobRow.running
                                    visible: jobRow.running
                                    implicitWidth: 20
                                    implicitHeight: 20
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: jobRow.bookTitle
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: jobRow.detail.length > 0
                                              ? qsTr("%1 · %2: %3").arg(jobRow.kindText).arg(jobRow.stateText).arg(jobRow.detail)
                                              : qsTr("%1 · %2").arg(jobRow.kindText).arg(jobRow.stateText)
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        opacity: 0.7
                                    }
                                }
                                Button {
                                    visible: jobRow.canCancel
                                    enabled: !window.closeRequested
                                    text: qsTr("Cancel")
                                    onClicked: window.library.cancelJob(jobRow.jobId)
                                }
                                Button {
                                    visible: jobRow.canRetry
                                    enabled: !window.closeRequested
                                    text: qsTr("Retry")
                                    onClicked: window.library.retryJob(jobRow.jobId)
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: jobList.count === 0
                            opacity: 0.7
                            text: qsTr("No processing yet.")
                        }
                    }
                }
            }
        }

        ReaderPane {
            reader: window.library.reader
        }
    }

    footer: ToolBar {
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                BusyIndicator {
                    running: window.library.opening
                    visible: running
                    implicitWidth: 24
                    implicitHeight: 24
                }
                Label {
                    Layout.fillWidth: true
                    text: window.closeRequested ? qsTr("Finishing before closing…") : window.library.statusText
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }
                Label {
                    visible: window.library.importing
                    text: qsTr("%1 of %2").arg(window.library.importDone).arg(window.library.importTotal)
                }
                ProgressBar {
                    visible: window.library.importing
                    from: 0
                    to: 1
                    value: window.library.fileProgress
                    implicitWidth: 160
                }
                Button {
                    visible: window.library.importing
                    text: qsTr("Cancel")
                    onClicked: window.library.cancelImports()
                }
            }
            // Processing: the SDK reports no totals, so no percentage is shown.
            RowLayout {
                Layout.fillWidth: true
                visible: window.library.processingAvailable && window.library.ready
                BusyIndicator {
                    running: window.library.jobs.pendingCount > 0 && !window.closeRequested
                    visible: running
                    implicitWidth: 24
                    implicitHeight: 24
                }
                Label {
                    Layout.fillWidth: true
                    text: window.library.jobs.summary.length > 0 ? window.library.jobs.summary
                                                                 : qsTr("Processing idle.")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    opacity: 0.8
                }
                Button {
                    flat: true
                    checkable: true
                    checked: window.showActivity
                    text: qsTr("Activity")
                    onToggled: window.showActivity = checked
                    Accessible.description: qsTr("Show or hide processing jobs")
                }
            }
            // At most a few problem lines here, so the book list keeps its
            // space; the full list is one click away.
            Repeater {
                model: window.library.problems.slice(0, window.maxFooterProblems)
                delegate: Label {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    opacity: 0.8
                }
            }
            Button {
                objectName: "restoreDuplicatesButton"
                visible: window.library.trashedDuplicateCount > 0
                text: qsTr("Restore %n book(s) from Trash", "", window.library.trashedDuplicateCount)
                onClicked: window.library.restoreTrashedDuplicates()
                Accessible.description: qsTr("The imported files are already in the library, in Trash: bring those books back")
            }
            Button {
                id: moreProblemsButton
                visible: window.library.problems.length > window.maxFooterProblems
                flat: true
                text: qsTr("…and %n more", "", window.library.problems.length - window.maxFooterProblems)
                onClicked: problemsDialog.open()
                Accessible.description: qsTr("Show every file that was not imported")
            }
        }
    }

    readonly property int maxFooterProblems: 3

    Dialog {
        id: problemsDialog
        title: qsTr("Files not imported (%1)").arg(window.library.problems.length)
        modal: true
        standardButtons: Dialog.Close
        anchors.centerIn: parent
        width: Math.min(window.width - 48, 640)
        height: Math.min(window.height - 48, 480)

        ListView {
            anchors.fill: parent
            clip: true
            model: window.library.problems
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Label {
                required property string modelData
                width: ListView.view.width
                text: modelData
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                bottomPadding: 4
            }
        }
    }
}
