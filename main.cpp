// appMyBooksLibrary
//   appMyBooksLibrary                        the window (Main.qml)
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
#include "infrastructure/sqlitecapabilities.h"
#include "processing/sdk/sdkinfo.h"
#include "reader/readercheck.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTextStream>

#include <atomic>
#include <cstring>

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

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("MyBooksLibrary", "Main");

    return QGuiApplication::exec();
}
