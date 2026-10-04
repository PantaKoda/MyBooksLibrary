pragma ComponentBehavior: Bound

// Appearance: the theme (as Windows is set, light or dark) and the accent
// colour, from the toolbar's Appearance button. A choice applies at once and
// is remembered for the next start; Appearance (C++) keeps it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

Dialog {
    id: dialog
    objectName: "appearanceDialog"

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, (parent ? parent.width : 440) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 400) - 32)
    modal: true
    title: qsTr("Appearance")
    standardButtons: Dialog.Close

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Label {
            text: qsTr("Theme")
            font.weight: Theme.headingWeight
        }
        ButtonGroup {
            id: themeGroup
        }
        // In a row, so the dialog fits the smallest window.
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingM

            Repeater {
                model: [
                    { value: Appearance.System, name: "themeSystem", text: qsTr("As Windows is set") },
                    { value: Appearance.Light, name: "themeLight", text: qsTr("Light") },
                    { value: Appearance.Dark, name: "themeDark", text: qsTr("Dark") }
                ]
                delegate: RadioButton {
                    required property var modelData
                    objectName: modelData.name
                    text: modelData.text
                    ButtonGroup.group: themeGroup
                    checked: Appearance.theme === modelData.value
                    onClicked: Appearance.theme = modelData.value
                }
            }
        }

        Label {
            Layout.topMargin: Theme.spacingS
            text: qsTr("Accent")
            font.weight: Theme.headingWeight
        }
        Flow {
            id: swatches
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Repeater {
                model: Appearance.accents
                delegate: AbstractButton {
                    id: swatch
                    required property var modelData
                    readonly property color shown: Theme.dark ? modelData.dark : modelData.light
                    objectName: "accent_" + modelData.id
                    checkable: true
                    checked: Appearance.accent === modelData.id
                    onClicked: Appearance.accent = modelData.id
                    focusPolicy: Qt.StrongFocus
                    implicitWidth: 36
                    implicitHeight: 36
                    Accessible.name: modelData.name
                    ToolTip.visible: hovered || visualFocus
                    ToolTip.delay: 300
                    ToolTip.text: modelData.name

                    // A colour dot; the chosen one has a ring around it, and
                    // keyboard focus a ring in the text colour.
                    background: Rectangle {
                        radius: width / 2
                        color: "transparent"
                        border.width: 2
                        border.color: swatch.visualFocus ? swatch.palette.windowText
                                                         : swatch.checked ? swatch.shown : "transparent"
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 5
                            radius: width / 2
                            color: swatch.shown
                            scale: swatch.hovered && !swatch.checked ? 1.1 : 1
                            Behavior on scale {
                                NumberAnimation { duration: 100 }
                            }
                        }
                    }
                }
            }
        }
        Label {
            objectName: "accentNameLabel"
            Layout.fillWidth: true
            text: {
                const chosen = Appearance.accents.find(a => a.id === Appearance.accent)
                return chosen ? chosen.name : ""
            }
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }
}
