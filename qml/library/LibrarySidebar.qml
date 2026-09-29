pragma ComponentBehavior: Bound

// The library's views: the whole library, each collection, and Trash, with
// their book counts. Collections are created, renamed and deleted here:
// with the mouse (right-click, press and hold) or the keyboard (Menu key or
// Shift+F10 for the menu, F2 to rename, Delete to delete). Everything goes
// through LibraryController (C++); names are always plain text.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

Pane {
    id: sidebar

    required property LibraryController library
    property bool enabledActions: true
    padding: 6

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        ItemDelegate {
            objectName: "libraryViewItem"
            Layout.fillWidth: true
            text: qsTr("Library (%1)").arg(sidebar.library.libraryCount)
            highlighted: sidebar.library.view === LibraryController.Library
            onClicked: sidebar.library.showLibrary()
        }

        Label {
            Layout.topMargin: 8
            Layout.leftMargin: 8
            text: qsTr("Collections")
            opacity: 0.7
        }
        ListView {
            id: collectionList
            objectName: "collectionList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 40
            clip: true
            model: sidebar.library.collections
            keyNavigationEnabled: true
            activeFocusOnTab: true
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: collectionRow
                required property string collectionId
                required property string name
                required property int bookCount
                required property int index
                objectName: "collectionRow_" + index
                width: ListView.view.width
                text: qsTr("%1 (%2)").arg(name).arg(bookCount)
                // A user's name is never markup.
                contentItem: Label {
                    text: collectionRow.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: collectionRow.highlighted ? collectionRow.palette.highlightedText : collectionRow.palette.windowText
                }
                highlighted: sidebar.library.viewCollectionId === collectionId
                onClicked: sidebar.library.showCollection(collectionId)
                onPressAndHold: collectionMenu.popup()
                Keys.onPressed: (event) => {
                    const menuKey = event.key === Qt.Key_Menu
                                    || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))
                    if (menuKey) {
                        collectionMenu.popup()
                    } else if (event.key === Qt.Key_F2 && sidebar.enabledActions) {
                        sidebar.askName(collectionRow.collectionId, collectionRow.name)
                    } else if (event.key === Qt.Key_Delete && sidebar.enabledActions) {
                        sidebar.askDelete(collectionRow.collectionId, collectionRow.name)
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        sidebar.library.showCollection(collectionRow.collectionId)
                    } else {
                        return
                    }
                    event.accepted = true
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: collectionMenu.popup()
                }
                Menu {
                    id: collectionMenu
                    MenuItem {
                        text: qsTr("Rename…")
                        enabled: sidebar.enabledActions
                        onTriggered: sidebar.askName(collectionRow.collectionId, collectionRow.name)
                    }
                    MenuItem {
                        text: qsTr("Delete…")
                        enabled: sidebar.enabledActions
                        onTriggered: sidebar.askDelete(collectionRow.collectionId, collectionRow.name)
                    }
                }
                Accessible.description: qsTr("Show the books in this collection. Right-click, or press the Menu key, to rename or delete it.")
            }
            Label {
                anchors.fill: parent
                anchors.margins: 8
                visible: collectionList.count === 0
                text: qsTr("No collections yet.")
                wrapMode: Text.Wrap
                opacity: 0.6
            }
        }
        Button {
            objectName: "newCollectionButton"
            Layout.fillWidth: true
            flat: true
            text: qsTr("New collection…")
            enabled: sidebar.enabledActions && sidebar.library.ready
            onClicked: sidebar.askName("", "")
        }

        ItemDelegate {
            objectName: "trashViewItem"
            Layout.fillWidth: true
            text: qsTr("Trash (%1)").arg(sidebar.library.trashCount)
            highlighted: sidebar.library.view === LibraryController.Trash
            onClicked: sidebar.library.showTrash()
        }

        Label {
            objectName: "organizeErrorLabel"
            Layout.fillWidth: true
            visible: sidebar.library.organizeError.length > 0
            text: sidebar.library.organizeError
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: "firebrick"
        }
    }

    // New collection (empty ID) or rename.
    function askName(collectionId, currentName) {
        sidebar.library.dismissOrganizeError()
        nameDialog.collectionId = collectionId
        nameField.text = currentName
        nameDialog.open()
        nameField.forceActiveFocus()
    }
    function askDelete(collectionId, name) {
        deleteDialog.collectionId = collectionId
        deleteDialog.collectionName = name
        deleteDialog.open()
    }

    Dialog {
        id: nameDialog
        objectName: "collectionNameDialog"
        property string collectionId
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(400, (parent ? parent.width : 400) - 32)
        modal: true
        title: collectionId.length > 0 ? qsTr("Rename collection") : qsTr("New collection")
        function save() {
            if (nameDialog.collectionId.length > 0)
                sidebar.library.renameCollection(nameDialog.collectionId, nameField.text)
            else
                sidebar.library.createCollection(nameField.text)
            nameDialog.close()
        }
        contentItem: ColumnLayout {
            TextField {
                id: nameField
                objectName: "collectionNameField"
                Layout.fillWidth: true
                placeholderText: qsTr("Name")
                Accessible.name: qsTr("Collection name")
                Keys.onReturnPressed: nameDialog.save()
                Keys.onEnterPressed: nameDialog.save()
            }
            RowLayout {
                Button {
                    objectName: "collectionNameSaveButton"
                    text: qsTr("Save")
                    highlighted: true
                    enabled: nameField.text.trim().length > 0
                    onClicked: nameDialog.save()
                }
                Button {
                    text: qsTr("Cancel")
                    onClicked: nameDialog.close()
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        objectName: "deleteCollectionDialog"
        property string collectionId
        property string collectionName
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, (parent ? parent.width : 420) - 32)
        modal: true
        title: qsTr("Delete this collection?")
        contentItem: ColumnLayout {
            Label {
                objectName: "deleteCollectionName"
                Layout.fillWidth: true
                text: deleteDialog.collectionName
                textFormat: Text.PlainText   // A user's name is never markup.
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("The collection is removed. Its books stay in the library.")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "confirmDeleteCollectionButton"
                text: qsTr("Delete collection")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: sidebar.library.deleteCollection(deleteDialog.collectionId)
    }
}
