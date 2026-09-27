// Reader: the real ReaderPane.qml with Qt PDF, offscreen, on real managed
// copies of the fixtures. Opens the requested physical page, reports the
// page shown, and survives repeated opening, switching and closing of books
// (docs/READER.md, "Teardown").
#include "catalog/library.h"
#include "catalog/reading.h"
#include "reader/readercontroller.h"
#include "storage/importservice.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

Q_IMPORT_QML_PLUGIN(MyBooksLibrary_PresentationPlugin)

using namespace mbl::domain;
using mbl::catalog::Library;
using mbl::reader::ReaderController;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

} // namespace

class TestReaderPane : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void opensThePhysicalPage();
    void opensThePageWhenLaidOutLate();
    void survivesRepeatedOpeningAndClosing();

private:
    QQuickItem* viewItem() const
    {
        auto* loader = m_pane->findChild<QQuickItem*>(QStringLiteral("readerViewLoader"));
        return loader ? qvariant_cast<QQuickItem*>(loader->property("item")) : nullptr;
    }
    int shownPage() const { return viewItem() ? viewItem()->property("currentPage").toInt() : -1; }
    // How far the view is actually scrolled: the view's currentPage can
    // change before (or without) the scroll happening.
    qreal scrolled() const
    {
        qreal y = -1;
        if (viewItem()) {
            for (QQuickItem* child : viewItem()->findChildren<QQuickItem*>()) {
                if (child->inherits("QQuickFlickable"))
                    y = qMax(y, child->property("contentY").toReal());
            }
        }
        return y;
    }
    // Opens and waits until the view shows `pageIndex`.
    void openAndWait(const BookId& book, int pageNumber)
    {
        m_reader->openPageNumber(book.toString(), pageNumber);
        QTRY_VERIFY_WITH_TIMEOUT(m_reader->viewActive() && m_reader->bookId() == book.toString(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(viewItem() != nullptr, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(shownPage(), pageNumber - 1, 10000);
        if (pageNumber > 1)  // Actually scrolled there, not only reported.
            QTRY_VERIFY2_WITH_TIMEOUT(scrolled() > 0, qPrintable(QString::number(scrolled())), 10000);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::shared_ptr<Library> m_library;
    BookId m_contents;
    BookId m_title;
    std::unique_ptr<ReaderController> m_reader;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QObject> m_object;
    QQuickItem* m_pane = nullptr;
};

void TestReaderPane::initTestCase()
{
    m_dir = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_dir->path());
    QVERIFY(opened);
    m_library = std::shared_ptr<Library>(std::move(opened.value()));
    mbl::storage::ImportService importer(*m_library);
    const auto contents = importer.importFile(fixture("contents-book.pdf"));
    const auto title = importer.importFile(fixture("title-page.pdf"));
    QVERIFY(contents.book && title.book);
    m_contents = *contents.book;
    m_title = *title.book;

    m_reader = std::make_unique<ReaderController>();
    m_reader->setLibrary(m_library);
    m_engine = std::make_unique<QQmlEngine>();
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/reader/ReaderPane.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    m_object.reset(component.createWithInitialProperties(
        {{QStringLiteral("reader"), QVariant::fromValue<QObject*>(m_reader.get())}}));
    m_pane = qobject_cast<QQuickItem*>(m_object.get());
    QVERIFY2(m_pane, qPrintable(component.errorString()));
    m_window = std::make_unique<QQuickWindow>();
    m_window->resize(600, 800);
    m_pane->setParentItem(m_window->contentItem());
    m_pane->setSize(QSizeF(600, 800));
    m_window->show();
}

void TestReaderPane::cleanupTestCase()
{
    // Close through the controller, so the view goes before the document.
    if (m_reader && m_reader->isOpen()) {
        m_reader->close();
        QTRY_VERIFY_WITH_TIMEOUT(!m_reader->isOpen(), 10000);
    }
    m_object.reset();
    m_window.reset();
    m_engine.reset();
    m_reader.reset();
    m_library.reset();
    m_dir.reset();
}

void TestReaderPane::opensThePhysicalPage()
{
    // "3 Networking with TCP/IP" is printed as page 12 and is physical page 15.
    openAndWait(m_contents, 15);
    QCOMPARE(m_reader->currentPage(), 14);
    QTRY_VERIFY2_WITH_TIMEOUT(scrolled() > 5000, qPrintable(QString::number(scrolled())), 10000);  // Page 15, not the top.
    m_reader->goToPageNumber(4);  // "1 Introduction".
    QTRY_COMPARE_WITH_TIMEOUT(shownPage(), 3, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(m_reader->currentPage(), 3, 5000);
    m_reader->goToPageNumber(1);  // Page index 0.
    QTRY_COMPARE_WITH_TIMEOUT(shownPage(), 0, 10000);
}

// In the window the reader only gets its size as a book opens (it replaces
// the library view), so the view is created before its pages are laid out.
// The requested page must still be reached.
void TestReaderPane::opensThePageWhenLaidOutLate()
{
    m_reader->close();
    QTRY_VERIFY_WITH_TIMEOUT(!m_reader->isOpen(), 10000);
    m_pane->setSize(QSizeF(0, 0));
    m_reader->openPageNumber(m_contents.toString(), 15);
    QTRY_VERIFY_WITH_TIMEOUT(viewItem() != nullptr, 10000);
    QTest::qWait(100);
    m_pane->setSize(QSizeF(600, 800));  // Laid out after the view exists.
    QTRY_COMPARE_WITH_TIMEOUT(shownPage(), 14, 10000);
    QCOMPARE(m_reader->currentPage(), 14);
    QTRY_VERIFY2_WITH_TIMEOUT(scrolled() > 5000, qPrintable(QString::number(scrolled())), 10000);
}

void TestReaderPane::survivesRepeatedOpeningAndClosing()
{
    for (int round = 0; round < 25; ++round) {
        openAndWait(m_contents, 1 + round % 27);
        openAndWait(m_title, 1 + round % 3);
        if (round % 5 == 4) {
            m_reader->close();
            QTRY_VERIFY_WITH_TIMEOUT(!m_reader->isOpen(), 10000);
            QVERIFY(viewItem() == nullptr);
            QVERIFY(m_reader->documentUrl().isEmpty());
        }
    }
    // The last page shown in each book was saved as its reading position.
    m_reader->close();
    QTRY_VERIFY_WITH_TIMEOUT(!m_reader->isOpen(), 10000);
    const auto position = [this](const BookId& book) {
        return m_library->run([book](QSqlDatabase& db) { return mbl::catalog::readingPosition(db, book); }).result().value();
    };
    QTRY_COMPARE_WITH_TIMEOUT(position(m_title), std::optional<int>(24 % 3), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(position(m_contents), std::optional<int>(24 % 27), 5000);
    // Reopening resumes there.
    m_reader->openBook(m_contents.toString());
    QTRY_VERIFY_WITH_TIMEOUT(viewItem() != nullptr, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(shownPage(), 24, 10000);
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestReaderPane test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_readerpane.moc"
