// appMyBooksLibrary
//   appMyBooksLibrary [--library <dir>]      the window (Main.qml) on the library folder from
//                                            --library, MYBOOKSLIBRARY_ROOT or the default
//                                            (src/app/libraryroot.h).
//                     [--import <pdf>]...    development: import files once the library is open
//                     [--screenshot <png>]   development: save the window when idle (imports and
//                                            metadata jobs finished), then quit
//                     [--activity]           development: open with the activity panel shown
//                     [--inspect-first]      development: select the first book (inspector shown)
//                     [--search <text>]      development: search once the library is ready
//                     [--read-page <n>]      development: open the first book at page n when idle (before
//                                            --screenshot or --close, if given)
//                     [--close]              development: instead of --screenshot, close the window
//                                            when idle (as its close button does) and quit; exit 1
//                                            if the reader was still open when the window closed
//   appMyBooksLibrary --sdk-check [<pdf>]    no window: print the pdfbookmark SDK identity and,
//                                            with a PDF, its identity and extracted title.
//                                            Exit 0 on success, 1 on an SDK error.
//   appMyBooksLibrary --sqlite-check         no window: probe FTS5 through the QSQLITE driver.
//                                            Exit 0 when search prerequisites are met, 1 otherwise.
//   appMyBooksLibrary --reader-check <pdf> [--rounds N] [--view churn|persistent|none]
//                                     [--require-ocr] [--no-models] [--no-control] [--timeout S]
//                                     [--ocr-threads N] [--qml-cycles N] [--qml-naive-teardown]
//                                            no window: Qt PDF availability and coexistence with
//                                            concurrent SDK work (docs/READER.md). Exit = number
//                                            of checks not passed; 2 for bad arguments; 3 if the
//                                            SDK worker times out.
//                                            The exe is a GUI-subsystem app on Windows, so redirect
//                                            or pipe stdout to see the output.
#include "app/libraryroot.h"
#include "infrastructure/sqlitecapabilities.h"
#include "processing/sdk/sdkinfo.h"
#include "processing/sdk/sdkcontentsanalyzer.h"
#include "processing/sdk/sdkmetadataextractor.h"
#include "presentation/librarycontroller.h"
#include "reader/readercheck.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>

Q_IMPORT_QML_PLUGIN(MyBooksLibrary_PresentationPlugin)

namespace {

int sdkCheck()
{
    // QCoreApplication::arguments() is Unicode-safe on Windows.
    const QStringList args = QCoreApplication::arguments();
    QTextStream out(stdout);

    const mbl::sdk::SdkIdentity sdk = mbl::sdk::querySdkIdentity();
    out << "sdk.header_version=" << sdk.headerVersion << Qt::endl
        << "sdk.loaded_version=" << sdk.loadedVersion << Qt::endl
        << "sdk.models_found=" << (sdk.modelsFound ? "true" : "false") << Qt::endl
        << "sdk.ocr_threads_auto=" << sdk.ocrThreadsAuto << Qt::endl;
    if (sdk.modelsFound)
        out << "sdk.detector_model=" << sdk.detectorModel << Qt::endl;
    if (sdk.headerVersion != sdk.loadedVersion)
        out << "warning=loaded SDK version differs from headers" << Qt::endl;

    if (args.size() < 3)
        return 0;

    // No GUI exists in this mode, so the blocking SDK call may run here.
    std::atomic_bool cancel{false};
    const mbl::sdk::PdfProbe probe = mbl::sdk::probePdf(args.at(2), &cancel);
    if (!probe.ok) {
        out << "pdf.error=" << probe.error << Qt::endl;
        return 1;
    }
    out << "pdf.sha256=" << probe.sha256 << Qt::endl
        << "pdf.page_count=" << probe.pageCount << Qt::endl
        << "metadata.title_status=" << probe.titleStatus << Qt::endl
        << "metadata.title=" << probe.title << Qt::endl
        << "metadata.pages_searched=" << probe.pagesSearched << Qt::endl
        << "metadata.cancelled=" << (probe.metadataCancelled ? "true" : "false") << Qt::endl
        << "metadata.report_json_bytes=" << probe.metadataJsonBytes << Qt::endl;
    return 0;
}

const char* yesNo(bool value)
{
    return value ? "true" : "false";
}

int sqliteCheck()
{
    QTextStream out(stdout);
    const auto caps = mbl::infrastructure::probeSqliteCapabilities();
    out << "sqlite.driver_available=" << yesNo(caps.driverAvailable) << Qt::endl
        << "sqlite.version=" << caps.sqliteVersion << Qt::endl
        << "sqlite.compile_options=" << caps.compileOptions.join(u' ') << Qt::endl
        << "sqlite.fts5=" << yesNo(caps.fts5) << Qt::endl
        << "sqlite.fts5_bm25=" << yesNo(caps.fts5Bm25) << Qt::endl
        << "sqlite.fts5_remove_diacritics=" << yesNo(caps.fts5RemoveDiacritics) << Qt::endl
        << "sqlite.fts5_prefix=" << yesNo(caps.fts5Prefix) << Qt::endl;
    if (!caps.error.isEmpty())
        out << "sqlite.error=" << caps.error << Qt::endl;
    return caps.searchReady() ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc >= 2 && std::strcmp(argv[1], "--sdk-check") == 0) {
        QCoreApplication app(argc, argv);
        return sdkCheck();
    }
    if (argc >= 2 && std::strcmp(argv[1], "--sqlite-check") == 0) {
        QCoreApplication app(argc, argv);
        return sqliteCheck();
    }

    if (argc >= 3 && std::strcmp(argv[1], "--reader-check") == 0) {
        QGuiApplication app(argc, argv);
        const QStringList args = QCoreApplication::arguments();
        mbl::reader::ReaderCheckOptions options;
        options.pdfPath = args.at(2);
        QTextStream out(stdout);
        for (qsizetype i = 3; i < args.size(); ++i) {
            const QString a = args.at(i);
            const QString next = args.value(i + 1);
            if (a == QLatin1String("--rounds") && next.toInt() > 0) {
                options.rounds = next.toInt();
                ++i;
            } else if (a == QLatin1String("--view") && (next == QLatin1String("churn") || next == QLatin1String("persistent")
                                                       || next == QLatin1String("none"))) {
                options.view = next == QLatin1String("churn")        ? mbl::reader::ViewMode::Churn
                               : next == QLatin1String("persistent") ? mbl::reader::ViewMode::Persistent
                                                                     : mbl::reader::ViewMode::None;
                ++i;
            } else if (a == QLatin1String("--qml-cycles") && next.toInt() > 0) {
                options.qmlCycles = next.toInt();
                ++i;
            } else if (a == QLatin1String("--qml-naive-teardown")) {
                options.qmlNaiveTeardown = true;
            } else if (bool ok = false; a == QLatin1String("--ocr-threads") && next.toInt(&ok) >= 0 && ok) {
                options.ocrThreads = next.toInt();  // 0 = automatic.
                ++i;
            } else if (a == QLatin1String("--timeout") && next.toInt() > 0) {
                options.timeoutSeconds = next.toInt();
                ++i;
            } else if (a == QLatin1String("--require-ocr")) {
                options.requireOcr = true;
            } else if (a == QLatin1String("--no-models")) {
                options.useModels = false;
            } else if (a == QLatin1String("--no-control")) {
                options.qtControl = false;
            } else {
                out << "unknown or incomplete argument: " << a << Qt::endl;
                return 2;
            }
        }
        return mbl::reader::runReaderCheck(options, out);
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MyBooksLibrary"));
    QCoreApplication::setApplicationName(QStringLiteral("MyBooksLibrary"));
    const QStringList args = QCoreApplication::arguments();

    // Composition root: the library session, its SDK extractor and its window.
    mbl::presentation::LibraryController library;
    library.setProcessors(std::make_shared<mbl::sdk::SdkMetadataExtractor>(),
                          std::make_shared<mbl::sdk::SdkContentsAnalyzer>(), mbl::sdk::querySdkIdentity().modelsFound);
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.setInitialProperties({{QStringLiteral("library"), QVariant::fromValue<QObject*>(&library)}});
    engine.loadFromModule("MyBooksLibrary", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    const mbl::app::LibraryRoot root = mbl::app::resolveLibraryRoot(args);
    library.open(root.path);

    // Development smoke mode: --import <pdf> (repeatable) queues files once the
    // library is ready; --screenshot <png> saves the window when idle and quits;
    // --activity shows the activity panel.
    QStringList imports;
    QString screenshot;
    const bool closeWhenIdle = args.contains(QLatin1String("--close"));
    if (args.contains(QLatin1String("--activity")))
        engine.rootObjects().constFirst()->setProperty("showActivity", true);
    if (args.contains(QLatin1String("--inspect-first")))
        engine.rootObjects().constFirst()->setProperty("inspectFirst", true);
    for (qsizetype i = 1; i + 1 < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--import"))
            imports << args.at(i + 1);
        else if (args.at(i) == QLatin1String("--screenshot"))
            screenshot = args.at(i + 1);
    }
    if (!imports.isEmpty())
        library.importFiles(imports);  // Queued until the library is ready.
    // Development: --search <text> runs a search once the library is ready
    // (at once, not debounced, so --screenshot shows its results).
    if (const qsizetype at = args.indexOf(QLatin1String("--search")); at >= 0 && at + 1 < args.size()) {
        const QString query = args.at(at + 1);
        QObject::connect(&library, &mbl::presentation::LibraryController::stateChanged, &library, [&library, query] {
            if (library.ready() && library.search()->text() != query) {
                library.search()->setText(query);
                library.search()->refresh();
            }
        });
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    // Development: --read-page <n> opens the first book at page n (1 = first)
    // once processing is idle, before the screenshot or close.
    const qsizetype readAt = args.indexOf(QLatin1String("--read-page"));
    const int readPage = readAt >= 0 && readAt + 1 < args.size() ? args.at(readAt + 1).toInt() : 0;
    if (!screenshot.isEmpty() || closeWhenIdle || readPage > 0) {
        auto readRequested = std::make_shared<bool>(false);
        auto trySave = std::make_shared<std::function<void()>>();
        *trySave = [&library, window, screenshot, closeWhenIdle, trySave, readPage, readRequested] {
            if (library.busy() || library.opening() || library.search()->searching()) {
                QTimer::singleShot(200, *trySave);
                return;
            }
            if (readPage > 0 && !*readRequested && library.books()->rowCount() > 0) {
                *readRequested = true;
                library.reader()->openPageNumber(library.books()->bookIdAt(0), readPage);
            }
            if (readPage > 0 && (!*readRequested || !library.reader()->viewActive())) {
                QTimer::singleShot(200, *trySave);
                return;
            }
            if (screenshot.isEmpty() && !closeWhenIdle)
                return;  // --read-page alone: stay open on the book.
            QTimer::singleShot(readPage > 0 ? 1500 : 500, [window, screenshot, closeWhenIdle] {  // Let the view settle and render.
                if (closeWhenIdle) {
                    window->close();  // Main.qml's onClosing, as for the close button.
                    return;
                }
                const bool saved = window && window->grabWindow().save(screenshot);
                QTextStream(stdout) << "screenshot=" << (saved ? screenshot : QStringLiteral("FAILED")) << Qt::endl;
                QCoreApplication::exit(saved ? 0 : 1);
            });
        };
        QTimer::singleShot(300, *trySave);
    }

    const int code = QGuiApplication::exec();
    if (closeWhenIdle) {
        // The window must close an open book (view, then document) before it
        // closes itself, never leave it to the engine's teardown.
        const bool readerOpen = library.reader()->isOpen() || !library.reader()->documentUrl().isEmpty();
        QTextStream(stdout) << "close: reader.open=" << (readerOpen ? 1 : 0) << Qt::endl;
        if (readerOpen)
            return 1;
    }
    return code;
}
