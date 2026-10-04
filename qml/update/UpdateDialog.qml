pragma ComponentBehavior: Bound

// Updates (docs/UPDATES.md): this version, a check for newer releases with
// their notes, and Install update. Opened from the toolbar's Update button
// (shown when a newer release exists) and from Library → Check for updates….
// All work goes through UpdateController (C++); release notes are shown as
// plain text, never as markup.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

Dialog {
    id: dialog
    objectName: "updateDialog"

    required property UpdateController updates
    readonly property bool busy: updates.state === UpdateController.Checking
                                 || updates.state === UpdateController.Installing
                                 || updates.state === UpdateController.ReadyToRestart

    function openAndCheck() {
        dialog.open()
        dialog.updates.check()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(620, (parent ? parent.width : 620) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 32)
    modal: true
    // Not dismissed while downloading: Cancel stops it first.
    closePolicy: dialog.busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    title: qsTr("Updates")

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Label {
            Layout.fillWidth: true
            text: qsTr("This is MyBooksLibrary %1.").arg(dialog.updates.currentVersion)
        }
        Label {
            objectName: "updateStatus"
            Layout.fillWidth: true
            visible: text.length > 0
            text: dialog.updates.statusText
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: dialog.updates.state === UpdateController.CheckFailed
                   || dialog.updates.state === UpdateController.InstallFailed ? Theme.critical : palette.windowText
        }
        ProgressBar {
            objectName: "updateProgress"
            Layout.fillWidth: true
            visible: dialog.updates.state === UpdateController.Installing
            from: 0
            to: 1
            value: Math.max(0, dialog.updates.progress)
            indeterminate: dialog.updates.progress < 0
        }

        // What's new: every release since this version, newest first.
        ScrollView {
            id: notesView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 260
            Layout.minimumHeight: 80
            visible: dialog.updates.available
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: notesView.availableWidth
                spacing: Theme.spacingM

                Repeater {
                    model: dialog.updates.releases
                    delegate: ColumnLayout {
                        id: release
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.spacingXS
                        Label {
                            Layout.fillWidth: true
                            text: release.modelData.name
                            textFormat: Text.PlainText
                            font.weight: Theme.headingWeight
                            wrapMode: Text.Wrap
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: release.modelData.date
                            color: Theme.textSecondary
                        }
                        // Selectable, plain text: notes are untrusted.
                        TextEdit {
                            Layout.fillWidth: true
                            text: release.modelData.notes
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            font: dialog.font
                            color: dialog.palette.windowText
                        }
                    }
                }
            }
        }

        Label {
            objectName: "cannotInstallReason"
            Layout.fillWidth: true
            visible: dialog.updates.available && !dialog.updates.canInstall
            text: dialog.updates.cannotInstallReason
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.caution
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.updates.available && dialog.updates.canInstall && !dialog.busy
            text: qsTr("Install update downloads the release from GitHub, checks it, then closes the app and starts the new version. Your library and settings are kept.")
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        CheckBox {
            objectName: "autoCheckBox"
            text: qsTr("Check for updates once a day")
            checked: dialog.updates.autoCheck
            onToggled: dialog.updates.autoCheck = checked
        }
    }

    // A row rather than a DialogButtonBox, which gives every button the same
    // width and cut "Install update" short (or pushed Close past the edge).
    footer: Pane {
        padding: Theme.spacingL
        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingS
            Button {
                objectName: "checkUpdatesButton"
                text: qsTr("Check again")
                enabled: !dialog.busy
                onClicked: dialog.updates.check()
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                objectName: "releasePageButton"
                text: qsTr("Open release page")
                visible: dialog.updates.available && !dialog.updates.canInstall
                onClicked: Qt.openUrlExternally(dialog.updates.releasePage)
            }
            Button {
                objectName: "installUpdateButton"
                text: qsTr("Install update")
                highlighted: true
                visible: dialog.updates.available && dialog.updates.canInstall
                         && dialog.updates.state !== UpdateController.Installing
                enabled: !dialog.busy
                onClicked: dialog.updates.install()
            }
            Button {
                objectName: "cancelInstallButton"
                text: qsTr("Cancel")
                visible: dialog.updates.state === UpdateController.Installing
                onClicked: dialog.updates.cancel()
            }
            Button {
                objectName: "closeUpdatesButton"
                text: qsTr("Close")
                visible: !dialog.busy
                onClicked: dialog.close()
            }
        }
    }
}
