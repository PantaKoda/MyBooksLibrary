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

    // Closing while work runs: cancel imports and stop processing (queued
    // metadata jobs resume next time), stay responsive, close when idle.
    onClosing: (close) => {
        if (window.library.busy) {
            close.accepted = false
            window.closeRequested = true
            window.library.prepareToClose()
        }
    }
    Connections {
        target: window.library
        function onBusyChanged() {
            if (window.closeRequested && !window.library.busy)
                window.close()
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
                text: qsTr("Library")
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                Layout.fillWidth: true
                text: window.library.libraryPath
                elide: Text.ElideMiddle
                opacity: 0.6
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

    ColumnLayout {
        anchors.fill: parent
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

            ListView {
                id: bookList
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                focus: true
                spacing: 2
                model: window.library.books
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

                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    onClicked: bookList.currentIndex = index

                    contentItem: ColumnLayout {
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: row.title
                            textFormat: Text.PlainText   // Extracted text is never markup.
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: row.titleFromFileName
                                  ? qsTr("From the file name · %1").arg(row.processingState)
                                  : (row.contributors.length > 0 ? row.contributors + " · " + row.processingState
                                                                 : row.processingState)
                            opacity: 0.7
                            elide: Text.ElideRight
                        }
                    }
                }

                // Keep the selection on the same book when the list refreshes.
                property string selectedBookId: ""
                onCurrentIndexChanged: selectedBookId = model ? model.bookIdAt(currentIndex) : ""
                Connections {
                    target: window.library.books
                    function onModelReset() {
                        bookList.currentIndex = window.library.books.rowOfBook(bookList.selectedBookId)
                    }
                }

                Label {
                    anchors.centerIn: parent
                    width: parent.width * 0.7
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: bookList.count === 0
                    opacity: 0.7
                    text: window.library.opening ? qsTr("Opening the library…")
                          : window.library.failed ? qsTr("The library could not be opened.")
                          : qsTr("No books yet. Choose “Import PDFs…” or drop PDF files here.")
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: dropArea.containsDrag
                color: window.palette.highlight
                opacity: 0.15
            }
        }

        // Processing activity: metadata jobs with Cancel and Retry.
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
                        Accessible.description: qsTr("Cancel every waiting and running metadata extraction")
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
                        text: qsTr("No metadata extraction yet.")
                    }
                }
            }
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
            // Metadata extraction: the SDK reports no percentage, so none is shown.
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
                                                                 : qsTr("Metadata extraction idle.")
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
                    Accessible.description: qsTr("Show or hide metadata extraction jobs")
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
