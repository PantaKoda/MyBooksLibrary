pragma ComponentBehavior: Bound

// Open another library (issue #30): in a new window, or instead of this one.
// Another library always runs in its own MyBooksLibrary process; "instead"
// then closes this window through its normal closing flow, which stops its
// work first. The folder is checked before anything starts (LibrarySwitcher,
// C++): it must be an existing library, not a backup, and not the library
// this window holds. Nothing is created.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

Dialog {
    id: dialog
    objectName: "openLibraryDialog"

    required property LibrarySwitcher switcher
    property bool enabledForUse: true  // False while the window closes.
    property string folder: ""         // The library to open, as shown.
    property bool openingDefault: false
    // Another library was started instead of this one: the window closes.
    signal switchRequested()

    function openFolder(url) {
        dialog.openingDefault = false
        dialog.folder = dialog.switcher.localPath(url)
        dialog.switcher.clearError()
        dialog.open()
    }
    function openDefault() {
        dialog.openingDefault = true
        dialog.folder = dialog.switcher.defaultPath
        dialog.switcher.clearError()
        dialog.open()
    }
    function choose(newWindow) {
        if (dialog.openingDefault)
            dialog.switcher.openDefault(newWindow)
        else
            dialog.switcher.openFolder(dialog.folder, newWindow)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, (parent ? parent.width : 560) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 400) - 32)
    modal: true
    title: dialog.openingDefault ? qsTr("Open the default library") : qsTr("Open library")
    // Not dismissed while the folder is checked.
    closePolicy: dialog.switcher.checking ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    Connections {
        target: dialog.switcher
        function onStarted(newWindow, folder) {
            dialog.close()
            if (!newWindow)
                dialog.switchRequested()
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        Label {
            objectName: "openLibraryFolder"
            Layout.fillWidth: true
            text: dialog.folder
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            font.weight: Theme.headingWeight
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("In a new window, this library stays open beside it. Instead of this library, this window closes once its work has stopped.")
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        RowLayout {
            visible: dialog.switcher.checking
            BusyIndicator {
                running: visible
                implicitWidth: 24
                implicitHeight: 24
            }
            Label { text: qsTr("Checking the folder…") }
        }
        Label {
            objectName: "openLibraryError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: dialog.switcher.error
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button {
                objectName: "openInNewWindowButton"
                text: qsTr("New window")
                highlighted: true
                enabled: dialog.enabledForUse && !dialog.switcher.checking && dialog.folder.length > 0
                onClicked: dialog.choose(true)
                Accessible.description: qsTr("Opens the library in a new window; this one stays open")
            }
            Button {
                objectName: "openInsteadButton"
                text: qsTr("Instead of this library")
                enabled: dialog.enabledForUse && !dialog.switcher.checking && dialog.folder.length > 0
                onClicked: dialog.choose(false)
                Accessible.description: qsTr("Opens the library in a new window and closes this one")
            }
            Button {
                text: qsTr("Cancel")
                enabled: !dialog.switcher.checking
                onClicked: dialog.close()
            }
        }
    }
}
