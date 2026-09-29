pragma ComponentBehavior: Bound

// Back up the library, or restore a backup (M10). A backup copies the
// catalog and every book's PDF into a new, verified folder; it can be made
// while books are being processed. A restore makes a new library from a
// backup (never over this one) and can open it in a new window. The work
// runs in the background; closing the window cancels it and leaves nothing
// half-made. All work goes through BackupController (C++).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBooksLibrary.Presentation

Dialog {
    id: dialog
    objectName: "backupDialog"

    required property BackupController backup
    property bool enabledForUse: true  // False while the window closes.
    property bool restoring: false     // Restore mode; otherwise back up.

    // While a backup or restore runs, the dialog shows that one, whichever
    // menu item opened it, and its fields are left as they were.
    function showRunning() {
        dialog.restoring = dialog.backup.operation === BackupController.Restore
        dialog.open()
    }
    function openForBackup() {
        if (dialog.backup.running)
            return dialog.showRunning()
        dialog.restoring = false
        dialog.backup.reset()
        backupFolderField.text = dialog.backup.suggestedBackupFolder
        dialog.open()
    }
    function openForRestore() {
        if (dialog.backup.running)
            return dialog.showRunning()
        dialog.restoring = true
        dialog.backup.reset()
        restoreFromField.text = ""
        restoreToField.text = dialog.backup.suggestedRestoreFolder
        dialog.open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(620, (parent ? parent.width : 620) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 32)
    modal: true
    // Like Close: not dismissed while its job runs (Cancel stops it first).
    closePolicy: dialog.backup.running ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    title: dialog.restoring ? qsTr("Restore a backup") : qsTr("Back up the library")

    contentItem: ColumnLayout {
        spacing: 8

        // What happens, in a sentence; scrolls in a small window.
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 32
            Layout.preferredHeight: explanation.implicitHeight
            contentWidth: availableWidth
            clip: true
            Label {
                id: explanation
                width: parent ? parent.width : implicitWidth
                wrapMode: Text.Wrap
                text: dialog.restoring
                      ? qsTr("Choose a backup folder (named “MyBooksLibrary backup …”). It is checked, then restored as a new library in a new folder. This library is not changed.")
                      : qsTr("Saves a copy of the catalog (titles, corrections, contents, collections) and every book's PDF into a new folder, and checks it. You can keep working while it runs.")
            }
        }

        // Back up: where.
        Label { visible: !dialog.restoring; text: qsTr("Save the backup in") }
        RowLayout {
            Layout.fillWidth: true
            visible: !dialog.restoring
            TextField {
                id: backupFolderField
                objectName: "backupFolderField"
                Layout.fillWidth: true
                enabled: !dialog.backup.running
                selectByMouse: true
                Accessible.name: qsTr("Folder to save the backup in")
            }
            Button {
                text: qsTr("Choose…")
                enabled: !dialog.backup.running
                onClicked: backupFolderDialog.open()
            }
        }

        // Restore: from where, and into which new folder.
        Label { visible: dialog.restoring; text: qsTr("Backup folder") }
        RowLayout {
            Layout.fillWidth: true
            visible: dialog.restoring
            TextField {
                id: restoreFromField
                objectName: "restoreFromField"
                Layout.fillWidth: true
                enabled: !dialog.backup.running
                selectByMouse: true
                Accessible.name: qsTr("Backup folder to restore")
            }
            Button {
                text: qsTr("Choose…")
                enabled: !dialog.backup.running
                onClicked: restoreFromDialog.open()
            }
        }
        Label { visible: dialog.restoring; text: qsTr("Restore as a new library in") }
        RowLayout {
            Layout.fillWidth: true
            visible: dialog.restoring
            TextField {
                id: restoreToField
                objectName: "restoreToField"
                Layout.fillWidth: true
                enabled: !dialog.backup.running
                selectByMouse: true
                Accessible.name: qsTr("New folder for the restored library")
            }
            Button {
                text: qsTr("Choose…")
                enabled: !dialog.backup.running
                onClicked: restoreToDialog.open()
                Accessible.description: qsTr("Choose the folder in which a new library folder is made")
            }
        }

        // Progress and result.
        ProgressBar {
            Layout.fillWidth: true
            visible: dialog.backup.running
            indeterminate: dialog.backup.total === 0
            from: 0
            to: Math.max(1, dialog.backup.total)
            value: dialog.backup.done
        }
        Label {
            objectName: "backupStatus"
            Layout.fillWidth: true
            visible: text.length > 0
            text: dialog.backup.statusText
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button {
                text: qsTr("Show folder")
                visible: dialog.backup.succeeded
                onClicked: Qt.openUrlExternally(dialog.backup.resultFolderUrl)
            }
            Button {
                objectName: "openRestoredButton"
                text: qsTr("Open restored library")
                visible: dialog.backup.succeeded && dialog.backup.operation === BackupController.Restore
                onClicked: {
                    if (dialog.backup.openRestoredLibrary())
                        dialog.close()
                }
                Accessible.description: qsTr("Opens the restored library in a new window")
            }
            Button {
                text: qsTr("Cancel")
                visible: dialog.backup.running
                onClicked: dialog.backup.cancel()
            }
            Button {
                objectName: "startBackupButton"
                text: dialog.restoring ? qsTr("Restore") : qsTr("Back up")
                highlighted: true
                visible: !dialog.backup.running && !dialog.backup.succeeded
                enabled: dialog.enabledForUse
                         && (dialog.restoring ? restoreFromField.text.trim().length > 0 && restoreToField.text.trim().length > 0
                                              : backupFolderField.text.trim().length > 0)
                onClicked: {
                    if (dialog.restoring)
                        dialog.backup.restore(restoreFromField.text, restoreToField.text)
                    else
                        dialog.backup.backUp(backupFolderField.text)
                }
            }
            Button {
                text: qsTr("Close")
                enabled: !dialog.backup.running
                onClicked: dialog.close()
                Accessible.description: qsTr("Close; cancel first to stop a backup or restore that is running")
            }
        }
    }

    FolderDialog {
        id: backupFolderDialog
        title: qsTr("Folder to save the backup in")
        onAccepted: backupFolderField.text = dialog.backup.localPath(selectedFolder)
    }
    FolderDialog {
        id: restoreFromDialog
        title: qsTr("Backup folder to restore")
        onAccepted: restoreFromField.text = dialog.backup.localPath(selectedFolder)
    }
    FolderDialog {
        id: restoreToDialog
        title: qsTr("Folder in which to make the restored library")
        onAccepted: restoreToField.text = dialog.backup.restoreFolderIn(selectedFolder)
    }
}
