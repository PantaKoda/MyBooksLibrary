// Used by `appMyBooksLibrary --reader-check` to prove that the QtQuick.Pdf
// module and its viewer load. Kept as a registered QML file so that
// qmlimportscanner (windeployqt --qmldir) sees the QtQuick.Pdf import.
import QtQuick
import QtQuick.Pdf

Item {
    id: root

    required property url sourceUrl
    readonly property int status: document.status
    readonly property int pageCount: document.pageCount
    // Set to false to destroy the view (and its page loads) before the
    // document is destroyed; see "Teardown" in docs/READER.md.
    property bool viewActive: true

    width: 400
    height: 600

    PdfDocument {
        id: document
        source: root.sourceUrl
    }

    Loader {
        anchors.fill: parent
        active: root.viewActive
        sourceComponent: Component {
            PdfMultiPageView {
                document: document
            }
        }
    }
}
