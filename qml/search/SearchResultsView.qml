pragma ComponentBehavior: Bound

// Search results: books best first, each with its best matching contents
// entries. A resolved entry shows its physical page; an unresolved one says
// so and where it is listed. Display only; the query runs in SearchController.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBooksLibrary.Presentation

ListView {
    id: view

    required property SearchController search
    // Development (--inspect-first): pick the first result when results arrive.
    property bool selectFirst: false
    // The book the user picked (its ID), for the inspector.
    signal bookChosen(string bookId)

    clip: true
    spacing: 6
    model: view.search.results
    keyNavigationEnabled: true
    currentIndex: -1
    ScrollBar.vertical: ScrollBar {}

    onCountChanged: {
        if (selectFirst && count > 0 && currentIndex < 0)
            currentIndex = 0
    }
    onCurrentIndexChanged: {
        if (currentIndex >= 0 && currentItem)
            view.bookChosen((currentItem as ResultRow).bookId)
    }

    component ResultRow: ItemDelegate {
        id: row
        required property int index
        required property string bookId
        required property string title
        required property string matchText
        required property string processingState
        required property var chapters
        required property string moreChapters

        width: ListView.view.width
        highlighted: ListView.isCurrentItem
        onClicked: view.currentIndex = index

        contentItem: ColumnLayout {
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: row.title
                textFormat: Text.PlainText
                font.bold: true
                elide: Text.ElideRight
                color: row.highlighted ? row.palette.highlightedText : row.palette.windowText
            }
            Label {
                Layout.fillWidth: true
                text: row.processingState.length > 0 ? qsTr("Matched: %1 · %2").arg(row.matchText).arg(row.processingState)
                                                     : qsTr("Matched: %1").arg(row.matchText)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                opacity: 0.7
                color: row.highlighted ? row.palette.highlightedText : row.palette.windowText
            }
            Repeater {
                model: row.chapters
                delegate: RowLayout {
                    id: hit
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: hit.modelData.title
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: row.highlighted ? row.palette.highlightedText : row.palette.windowText
                    }
                    Label {
                        text: hit.modelData.stateText.length > 0
                              ? qsTr("%1 · %2").arg(hit.modelData.pageText).arg(hit.modelData.stateText)
                              : hit.modelData.pageText
                        textFormat: Text.PlainText
                        font.italic: hit.modelData.page < 0
                        opacity: 0.75
                        color: row.highlighted ? row.palette.highlightedText : row.palette.windowText
                    }
                }
            }
            Label {
                Layout.leftMargin: 12
                visible: row.moreChapters.length > 0
                text: row.moreChapters
                textFormat: Text.PlainText
                opacity: 0.6
                color: row.highlighted ? row.palette.highlightedText : row.palette.windowText
            }
        }
    }

    delegate: ResultRow {}

    footer: ColumnLayout {
        width: view.width
        spacing: 4
        Label {
            Layout.fillWidth: true
            Layout.margins: 8
            text: view.search.searching ? qsTr("Searching…") : view.search.statusText
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            horizontalAlignment: view.count === 0 ? Text.AlignHCenter : Text.AlignLeft
            opacity: 0.7
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: view.search.canLoadMore
            text: qsTr("Show more")
            onClicked: view.search.loadMore()
        }
    }
}
