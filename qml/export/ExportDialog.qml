pragma ComponentBehavior: Bound

// Save a bookmarked copy of the selected book (M09). Shows what the copy gets
// before anything is written: the number of bookmarks, and every contents
// entry left out or moved with the reason, so partial coverage is never
// hidden. The book in the library is not changed. An existing file is
// replaced only after the user confirms it. The export runs in the
// background; the dialog follows it (waiting, writing, saved or not) and can
// be closed at any time. All data and commands go through ExportController
// (C++); extracted titles are plain text.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBooksLibrary.Presentation

Dialog {
    id: dialog
    objectName: "exportDialog"

    required property ExportController exporter
    property bool enabledForUse: true  // False while the window closes.

    // Opens the dialog for a book.
    function openFor(bookId) {
        dialog.exporter.prepare(bookId)
        pathField.text = ""
        dialog.open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(620, (parent ? parent.width : 620) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 32)
    modal: true
    title: qsTr("Save a copy with bookmarks")

    // The suggested name, once the preview is in (the user may already have typed).
    Connections {
        target: dialog.exporter
        function onPreviewChanged() {
            if (pathField.text.length === 0)
                pathField.text = dialog.exporter.suggestedPath
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        // The preview scrolls, so the path, the progress and the buttons
        // below always stay in the window, whatever its size and however
        // many entries are left out.
        ScrollView {
            id: previewScroll
            objectName: "exportPreview"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 48
            Layout.preferredHeight: previewColumn.implicitHeight
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: previewColumn
                width: previewScroll.availableWidth
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: dialog.exporter.bookTitle
                    textFormat: Text.PlainText
                    font.weight: Theme.headingWeight
                    wrapMode: Text.Wrap
                }
                BusyIndicator {
                    visible: dialog.exporter.loading
                    running: visible
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    objectName: "exportProblem"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: dialog.exporter.problem
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                Label {
                    objectName: "exportSummary"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: dialog.exporter.summary
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                // Every entry left out or moved, with the reason.
                Repeater {
                    objectName: "exportNotes"
                    model: dialog.exporter.notes
                    delegate: Label {
                        required property string modelData
                        Layout.fillWidth: true
                        leftPadding: 12
                        text: modelData
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: dialog.exporter.lastExportText
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.italic: true
                }
            }
        }

        Label {
            text: qsTr("Save as")
            visible: dialog.exporter.canExport
        }
        RowLayout {
            Layout.fillWidth: true
            visible: dialog.exporter.canExport
            TextField {
                id: pathField
                objectName: "exportPathField"
                Layout.fillWidth: true
                enabled: !dialog.exporter.running
                selectByMouse: true
                Accessible.name: qsTr("File to save the copy as")
                // Only what the Save button itself allows (not while closing,
                // asking to replace, or without a name).
                Keys.onReturnPressed: if (saveButton.enabled) saveButton.clicked()
                Keys.onEnterPressed: if (saveButton.enabled) saveButton.clicked()
            }
            Button {
                text: qsTr("Choose…")
                enabled: !dialog.exporter.running
                onClicked: fileDialog.open()
                Accessible.description: qsTr("Choose the folder and name of the copy")
            }
        }

        // What the export is doing now, and how it ended.
        RowLayout {
            Layout.fillWidth: true
            visible: dialog.exporter.phaseText.length > 0
            BusyIndicator {
                visible: dialog.exporter.running
                running: visible
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }
            Label {
                objectName: "exportPhase"
                Layout.fillWidth: true
                text: dialog.exporter.phaseText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            visible: dialog.exporter.phase === ExportController.NeedsReplace
            Button {
                objectName: "replaceButton"
                text: qsTr("Replace")
                onClicked: dialog.exporter.confirmReplace()
            }
            Button {
                text: qsTr("Choose another name")
                onClicked: {
                    dialog.exporter.declineReplace()
                    pathField.forceActiveFocus()
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button {
                text: qsTr("Show folder")
                visible: dialog.exporter.phase === ExportController.Saved
                onClicked: Qt.openUrlExternally(dialog.exporter.resultFolder)
            }
            Button {
                objectName: "cancelExportButton"
                text: qsTr("Cancel saving")
                visible: dialog.exporter.running
                enabled: dialog.exporter.phase === ExportController.Waiting
                         || dialog.exporter.phase === ExportController.Writing
                onClicked: dialog.exporter.cancel()
            }
            Button {
                id: saveButton
                objectName: "saveCopyButton"
                text: qsTr("Save copy")
                visible: dialog.exporter.canExport
                enabled: dialog.enabledForUse && !dialog.exporter.running
                         && dialog.exporter.phase !== ExportController.NeedsReplace
                         && pathField.text.trim().length > 0
                highlighted: true
                onClicked: dialog.exporter.exportTo(pathField.text, false)
            }
            Button {
                objectName: "closeExportButton"
                text: qsTr("Close")
                onClicked: dialog.close()
                Accessible.description: qsTr("Close; a copy being saved continues in the background")
            }
        }
    }

    // The copy's name and folder. Overwriting is confirmed by this dialog
    // (NeedsReplace), not by the system's, so it is always explicit.
    FileDialog {
        id: fileDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PDF files (*.pdf)")]
        defaultSuffix: "pdf"
        options: FileDialog.DontConfirmOverwrite
        currentFolder: dialog.exporter.suggestedFolder
        onAccepted: pathField.text = dialog.exporter.localPath(selectedFile)
    }
}
