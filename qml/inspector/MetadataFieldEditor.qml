pragma ComponentBehavior: Bound

// Corrects one metadata field of one book: the user's value, a deliberately
// empty field (Leave empty), or the document's value again. BookInspector
// (C++) checks and saves it; the PDF is never changed. `fieldData` is a copy
// of one entry of BookInspector.metadataFields taken when editing began, so
// a refresh while typing does not disturb the editor.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

ColumnLayout {
    id: editor

    required property BookInspector inspector
    required property string bookId
    required property var fieldData
    signal done()

    readonly property bool isContributors: fieldData.kind === "contributors"
    // Contributors as [{name, role}]; changed only by add, move and remove.
    // Names typed into the rows are read back with people(), so every row
    // stays instantiated (a Repeater, not a ListView).
    property var rows: []
    property int focusRow: 0  // The row that takes focus when the rows are rebuilt.
    // Beyond this height the rows scroll, so the buttons stay on screen.
    property real maximumRowsHeight: 280
    // After Add, the rows follow their end while they are laid out (the new
    // row's height arrives later on a slow machine), until the user scrolls.
    property bool followEnd: false
    spacing: 8

    function people() {
        const list = []
        for (let i = 0; i < rowRepeater.count; ++i) {
            const row = rowRepeater.itemAt(i) as ContributorRow
            list.push({ name: row.nameText, role: row.roleCode })
        }
        return list
    }
    function restructure(change) {
        const list = editor.people()
        change(list)
        editor.rows = list
    }
    function addPerson() {
        editor.focusRow = editor.rows.length
        editor.restructure(list => list.push({ name: "", role: "author" }))
        // Show the new row: it is the last one.
        editor.followEnd = true
        Qt.callLater(editor.showEnd)
    }
    function showEnd() {
        const flick = rowScroll.contentItem as Flickable
        flick.contentY = Math.max(0, flick.contentHeight - flick.height)
    }
    function save() {
        if (editor.isContributors)
            editor.inspector.setContributors(editor.bookId, editor.people())
        else if (editor.fieldData.kind === "year")
            editor.inspector.setYear(editor.bookId, editor.fieldData.field, valueField.text)
        else
            editor.inspector.setText(editor.bookId, editor.fieldData.field, valueField.text)
        // A refused value keeps the editor open with the reason shown.
        if (editor.inspector.correctionError.length === 0)
            editor.done()
    }

    Component.onCompleted: {
        if (editor.isContributors) {
            const list = editor.fieldData.editContributors.map(p => ({ name: p.name, role: p.role }))
            editor.rows = list.length > 0 ? list : [{ name: "", role: "author" }]
        } else {
            valueField.forceActiveFocus()
        }
    }

    component ContributorRow: RowLayout {
        id: row
        required property var modelData
        required property int index
        property alias nameText: nameField.text
        readonly property string roleCode: editor.inspector.contributorRoles[roleBox.currentIndex].code
        Layout.fillWidth: true

        TextField {
            id: nameField
            objectName: "contributorName_" + row.index
            Layout.fillWidth: true
            text: row.modelData.name
            placeholderText: qsTr("Name")
            Accessible.name: qsTr("Name %1").arg(row.index + 1)
            Keys.onReturnPressed: editor.save()
            Keys.onEnterPressed: editor.save()
            Component.onCompleted: if (row.index === editor.focusRow) forceActiveFocus()
        }
        ComboBox {
            id: roleBox
            model: editor.inspector.contributorRoles
            textRole: "text"
            valueRole: "code"
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(row.modelData.role))
            Accessible.name: qsTr("Role of %1").arg(nameField.text)
        }
        Button {
            objectName: "moveUp_" + row.index
            text: "↑"
            enabled: row.index > 0
            onClicked: editor.restructure(list => list.splice(row.index - 1, 0, list.splice(row.index, 1)[0]))
            Accessible.name: qsTr("Move up")
        }
        Button {
            text: "↓"
            enabled: row.index + 1 < editor.rows.length
            onClicked: editor.restructure(list => list.splice(row.index + 1, 0, list.splice(row.index, 1)[0]))
            Accessible.name: qsTr("Move down")
        }
        Button {
            text: "✕"
            onClicked: editor.restructure(list => list.splice(row.index, 1))
            Accessible.name: qsTr("Remove %1").arg(nameField.text)
        }
    }

    Label {
        Layout.fillWidth: true
        visible: editor.fieldData.documentValue.length > 0
        text: qsTr("The document says: %1").arg(editor.fieldData.documentValue)
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.textSecondary
    }

    TextField {
        id: valueField
        objectName: "correctionValueField"
        visible: !editor.isContributors
        Layout.fillWidth: true
        text: editor.fieldData.editText
        inputMethodHints: editor.fieldData.kind === "year" ? Qt.ImhDigitsOnly : Qt.ImhNone
        maximumLength: editor.fieldData.kind === "year" ? 4 : 32767
        Accessible.name: editor.fieldData.label
        Keys.onReturnPressed: editor.save()
        Keys.onEnterPressed: editor.save()
    }

    ColumnLayout {
        visible: editor.isContributors
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: qsTr("In the order printed:")
            color: Theme.textSecondary
        }
        ScrollView {
            id: rowScroll
            objectName: "contributorRows"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(rowColumn.implicitHeight, editor.maximumRowsHeight)
            contentWidth: availableWidth
            clip: true
            Connections {
                target: rowScroll.contentItem
                enabled: editor.followEnd
                function onContentHeightChanged() { editor.showEnd() }
                function onHeightChanged() { editor.showEnd() }
                function onMovementStarted() { editor.followEnd = false }
            }
            ColumnLayout {
                id: rowColumn
                width: rowScroll.availableWidth
                spacing: 4
                Repeater {
                    id: rowRepeater
                    model: editor.rows
                    delegate: ContributorRow {}
                }
            }
        }
        Button {
            objectName: "addPersonButton"
            text: qsTr("Add a person")
            onClicked: editor.addPerson()
        }
    }

    Label {
        objectName: "correctionErrorLabel"
        Layout.fillWidth: true
        visible: editor.inspector.correctionError.length > 0
        text: editor.inspector.correctionError
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.critical
    }

    RowLayout {
        Layout.fillWidth: true
        Button {
            objectName: "correctionSaveButton"
            text: qsTr("Save")
            highlighted: true
            onClicked: editor.save()
        }
        Button {
            text: qsTr("Cancel")
            onClicked: editor.done()
        }
        Item { Layout.fillWidth: true }
        Button {
            objectName: "correctionClearButton"
            text: qsTr("Leave empty")
            onClicked: {
                editor.inspector.clearField(editor.bookId, editor.fieldData.field)
                editor.done()
            }
            Accessible.description: qsTr("Show this field as empty, even if the document has a value")
        }
        Button {
            objectName: "correctionAutoButton"
            visible: editor.fieldData.mode !== "auto"
            text: qsTr("Use the document's value")
            onClicked: {
                editor.inspector.useDocumentValue(editor.bookId, editor.fieldData.field)
                editor.done()
            }
            Accessible.description: qsTr("Remove your correction")
        }
    }

    Keys.onEscapePressed: editor.done()
}
