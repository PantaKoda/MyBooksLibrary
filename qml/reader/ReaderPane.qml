pragma ComponentBehavior: Bound

// The embedded reader. One PdfDocument lives as long as the pane; the view
// exists only while the ReaderController allows it (viewActive) and the
// document is ready. Each view is registered with the controller
// (attachView), which waits for its actual destruction before it changes or
// closes the document, so no document changes under a live view
// (docs/READER.md, "Teardown"). Display only.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Pdf
import MyBooksLibrary.Presentation

Pane {
    id: pane

    required property ReaderController reader
    padding: 0

    // goToPage() scrolls only once the view has a size (in the window the
    // reader is laid out as the book opens); before that it would change
    // currentPage without scrolling. So a requested page stays pending
    // until the view is laid out.
    component ReaderView: PdfMultiPageView {
        id: readerView
        property int pendingPage: -1
        function show(page) {
            pendingPage = page
            if (width > 0 && height > 0) {
                pendingPage = -1
                goToPage(page)
            }
        }
        onWidthChanged: if (pendingPage >= 0) show(pendingPage)
        onHeightChanged: if (pendingPage >= 0) show(pendingPage)
    }

    PdfDocument {
        id: document
        source: pane.reader.documentUrl
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                Button {
                    text: qsTr("Library")
                    icon.name: "go-previous"
                    onClicked: pane.reader.close()
                    Accessible.description: qsTr("Close the book and return to the library")
                }
                Label {
                    Layout.fillWidth: true
                    text: pane.reader.title
                    textFormat: Text.PlainText
                    font.weight: Theme.headingWeight
                    elide: Text.ElideRight
                }
                Button {
                    text: "‹"
                    enabled: viewLoader.item !== null && pane.reader.currentPage > 0
                    onClicked: pane.reader.goToPageNumber(pane.reader.currentPage)
                    Accessible.name: qsTr("Previous page")
                }
                SpinBox {
                    id: pageBox
                    from: 1
                    to: Math.max(1, document.pageCount)
                    value: pane.reader.currentPage + 1
                    editable: true
                    enabled: viewLoader.item !== null
                    onValueModified: pane.reader.goToPageNumber(value)
                    Accessible.name: qsTr("Page")
                }
                Label {
                    text: qsTr("of %1").arg(document.pageCount)
                }
                Button {
                    text: "›"
                    enabled: viewLoader.item !== null && pane.reader.currentPage + 1 < document.pageCount
                    onClicked: pane.reader.goToPageNumber(pane.reader.currentPage + 2)
                    Accessible.name: qsTr("Next page")
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Loader {
                id: viewLoader
                objectName: "readerViewLoader"
                anchors.fill: parent
                active: pane.reader.viewActive && document.status === PdfDocument.Ready
                sourceComponent: ReaderView {
                    id: view
                    objectName: "readerView"
                    document: document
                    onCurrentPageChanged: pane.reader.setCurrentPage(currentPage)
                    Component.onCompleted: {
                        pane.reader.attachView(view)
                        Qt.callLater(() => view.show(pane.reader.requestedPage))
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width * 0.7
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: viewLoader.item === null
                color: Theme.textSecondary
                text: pane.reader.error.length > 0 ? pane.reader.error
                      : document.status === PdfDocument.Error ? qsTr("This PDF could not be opened.")
                      : qsTr("Opening…")
            }
        }
    }

    Connections {
        target: pane.reader
        function onRequestedPageChanged() {
            if (viewLoader.item)
                (viewLoader.item as ReaderView).show(pane.reader.requestedPage)
        }
    }
}
