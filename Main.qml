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

    // Fluent's controls are larger than the classic style's: room for the
    // three panes without crowding the toolbar.
    width: 1100
    height: 720
    minimumWidth: 480
    minimumHeight: 360
    visible: true
    // Which library, so two windows (a restored library, Open library…) can
    // be told apart in the taskbar.
    title: window.library.switcher.currentName.length > 0
           ? qsTr("%1 – MyBooksLibrary").arg(window.library.switcher.currentName)
           : qsTr("MyBooksLibrary")
    // The accent chosen in the Appearance dialog, for the style's controls
    // and their popups (selection bars, focus, accent buttons) as for ours.
    palette.accent: Theme.accent

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

    // Back up… and Restore… (M10), from the toolbar's Backup menu.
    BackupDialog {
        id: backupDialog
        backup: window.library.backup
        enabledForUse: window.library.ready && !window.closeRequested
    }

    // Save a copy with bookmarks (M09); opened from the inspector's More menu.
    ExportDialog {
        id: exportDialog
        exporter: window.library.exporter
        enabledForUse: window.library.ready && !window.closeRequested
    }

    // Theme and accent, from the toolbar's Appearance button.
    AppearanceDialog {
        id: appearanceDialog
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import PDF files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: window.library.importUrls(selectedFiles)
    }

    // Open library… (issue #30), from the toolbar's Library menu or the
    // notice shown when this window's library could not be opened.
    FolderDialog {
        id: libraryFolderDialog
        title: qsTr("Choose the library's folder")
        onAccepted: openLibraryDialog.openFolder(selectedFolder)
    }
    OpenLibraryDialog {
        id: openLibraryDialog
        switcher: window.library.switcher
        enabledForUse: !window.closeRequested
        onSwitchRequested: window.close()  // The normal closing flow: work stops first.
    }

    header: ToolBar {
        // Tinted with the accent, with a divider below.
        background: Rectangle {
            color: Qt.tint(window.palette.window, Theme.barTint)
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.divider
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            Label {
                objectName: "viewTitleLabel"
                text: window.library.viewTitle
                textFormat: Text.PlainText
                font.pixelSize: Theme.bodyLargeSize
                font.weight: Theme.headingWeight
                elide: Text.ElideRight
                Layout.maximumWidth: 220
            }
            Label {
                objectName: "libraryPathLabel"
                Layout.preferredWidth: 160
                text: window.library.libraryPath
                elide: Text.ElideMiddle
                color: Theme.textSecondary
                // The whole path, and how the library was chosen.
                HoverHandler { id: pathHover }
                ToolTip.visible: pathHover.hovered && text.length > 0
                ToolTip.delay: 500
                ToolTip.text: window.library.switcher.sourceText.length > 0
                              ? qsTr("%1 (%2)").arg(window.library.libraryPath, window.library.switcher.sourceText)
                              : window.library.libraryPath
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
                id: libraryButton
                objectName: "libraryMenuButton"
                text: qsTr("Library")
                // Also when this window's library could not be opened: the way out.
                enabled: !window.closeRequested
                onClicked: libraryMenu.open()
                Accessible.description: qsTr("Open another library, in a new window or instead of this one")
                Menu {
                    id: libraryMenu
                    y: libraryButton.height
                    MenuItem {
                        objectName: "openLibraryItem"
                        text: qsTr("Open library…")
                        onTriggered: libraryFolderDialog.open()
                    }
                    MenuItem {
                        objectName: "openDefaultLibraryItem"
                        text: qsTr("Open the default library")
                        enabled: !window.library.switcher.currentIsDefault
                        onTriggered: openLibraryDialog.openDefault()
                    }
                }
            }
            Button {
                id: importButton
                text: qsTr("Import PDFs…")
                highlighted: true  // The main action: an accent button.
                enabled: window.library.ready && !window.closeRequested
                onClicked: importDialog.open()
                Accessible.description: qsTr("Choose PDF files to copy into the library")
            }
            Button {
                id: backupButton
                objectName: "backupMenuButton"
                text: qsTr("Backup")
                enabled: window.library.ready && !window.closeRequested
                onClicked: backupMenu.open()
                Accessible.description: qsTr("Back up this library, or restore a backup as a new library")
                Menu {
                    id: backupMenu
                    y: backupButton.height
                    MenuItem {
                        objectName: "backUpItem"
                        text: qsTr("Back up the library…")
                        onTriggered: backupDialog.openForBackup()
                    }
                    MenuItem {
                        objectName: "restoreItem"
                        text: qsTr("Restore a backup…")
                        onTriggered: backupDialog.openForRestore()
                    }
                }
            }
            // A swatch of the current accent: a ring and a dot.
            ToolButton {
                id: appearanceButton
                objectName: "appearanceButton"
                onClicked: appearanceDialog.open()
                Accessible.name: qsTr("Appearance")
                Accessible.description: qsTr("Choose the theme and the accent colour")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Appearance: theme and accent")
                contentItem: Item {
                    implicitWidth: 20
                    implicitHeight: 20
                    Rectangle {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        radius: 9
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.accent
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 4
                            radius: width / 2
                            color: Theme.accent
                        }
                    }
                }
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
                    // A thin divider with a wider grab area (FluentWinUI3 has no
                    // SplitView of its own, and Fusion's handle is a thick bar).
                    handle: Item {
                        implicitWidth: 7
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.SplitHandle.hovered || parent.SplitHandle.pressed ? 3 : 1
                            height: parent.height
                            radius: width / 2
                            color: parent.SplitHandle.hovered || parent.SplitHandle.pressed ? window.palette.accent
                                                                                             : Theme.divider
                        }
                    }

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
                    onExportRequested: exportDialog.openFor(window.library.inspector.bookId)
                    exportEnabled: window.library.ready && !window.closeRequested
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
                    color: window.palette.accent
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
                            font.weight: Theme.headingWeight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: window.library.processingAvailable && !window.library.ocrAvailable
                                  ? qsTr("OCR models were not found: title pages that are scanned images cannot be read.")
                                  : ""
                            wrapMode: Text.WordWrap
                            color: Theme.textSecondary
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
                                        color: Theme.textSecondary
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
                            color: Theme.textSecondary
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

    // This window's library could not be opened (locked in another window,
    // moved, a backup, a newer catalog…): the reason, and a way out. Over the
    // library view, which stays disabled.
    Pane {
        objectName: "libraryFailedNotice"
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 32)
        visible: window.library.failed
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("This library could not be opened")
                font.pixelSize: Theme.subtitleSize
                font.weight: Theme.headingWeight
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "libraryFailedReason"
                Layout.fillWidth: true
                text: window.library.openError
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            RowLayout {
                Button {
                    objectName: "failedOpenLibraryButton"
                    text: qsTr("Open library…")
                    enabled: !window.closeRequested
                    onClicked: libraryFolderDialog.open()
                }
                Button {
                    objectName: "failedOpenDefaultButton"
                    text: qsTr("Open the default library")
                    visible: !window.library.switcher.currentIsDefault
                    enabled: !window.closeRequested
                    onClicked: openLibraryDialog.openDefault()
                }
            }
        }
    }

    footer: ToolBar {
        background: Rectangle {
            color: Qt.tint(window.palette.window, Theme.barTint)
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.divider
            }
        }
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
                    color: Theme.textSecondary
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
                    color: Theme.critical  // A file that was not imported.
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
