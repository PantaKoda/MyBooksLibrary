#include "reader/readercheck.h"

#include "processing/sdk/sdkinfo.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPdfDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QThread>
#include <QScopeGuard>
#include <QUrl>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <thread>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#endif

namespace mbl::reader {

namespace {

const QSize kRenderSize(150, 200);

struct SdkBaseline {
    sdk::PdfProbe metadata;
    sdk::AnalysisProbe analysis;
};

// Runs `work` on a plain worker thread while the GUI thread keeps running
// `whileRunning` and processing events. Never blocks the GUI thread on the
// worker. On timeout the harness process exits with code 3, because the
// still-running worker may reference this stack frame.
bool runOnWorker(const std::function<void()>& work, const std::function<void()>& whileRunning, int timeoutMs)
{
    auto done = std::make_shared<std::atomic_bool>(false);
    std::thread worker([work, done] {
        work();
        done->store(true);
    });
    QElapsedTimer timer;
    timer.start();
    while (!done->load()) {
        if (whileRunning)
            whileRunning();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (!whileRunning)
            QThread::msleep(2);
        if (timer.elapsed() > timeoutMs) {
            std::fprintf(stderr, "reader check: SDK worker did not finish within %d ms\n", timeoutMs);
            std::fflush(nullptr);
            std::_Exit(3);
        }
    }
    worker.join();
    return true;
}

// Loads the PDF with Qt PDF and returns a digest per rendered page, or an
// empty list on failure.
QList<QByteArray> renderAll(const QString& path, QString* error, QString* firstLabel = nullptr)
{
    QPdfDocument document;
    if (const auto e = document.load(path); e != QPdfDocument::Error::None) {
        if (error)
            *error = QStringLiteral("load error %1").arg(int(e));
        return {};
    }
    if (document.status() != QPdfDocument::Status::Ready || document.pageCount() <= 0) {
        if (error)
            *error = QStringLiteral("status %1, %2 pages").arg(int(document.status())).arg(document.pageCount());
        return {};
    }
    if (firstLabel)
        *firstLabel = document.pageLabel(0);
    QList<QByteArray> digests;
    for (int page = 0; page < document.pageCount(); ++page) {
        const QImage image = document.render(page, kRenderSize);
        if (image.isNull()) {
            if (error)
                *error = QStringLiteral("page %1 rendered null").arg(page);
            return {};
        }
        digests << QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()),
            QCryptographicHash::Sha256);
    }
    document.close();
    return digests;
}

bool sameSdkResults(const SdkBaseline& a, const SdkBaseline& b)
{
    return a.metadata.ok == b.metadata.ok && a.metadata.sha256 == b.metadata.sha256
           && a.metadata.titleStatus == b.metadata.titleStatus && a.metadata.title == b.metadata.title
           && a.analysis.ok == b.analysis.ok && a.analysis.outcome == b.analysis.outcome
           && a.analysis.parsedEntries == b.analysis.parsedEntries
           && a.analysis.resolvedEntries == b.analysis.resolvedEntries
           && a.analysis.planReady == b.analysis.planReady;
}

SdkBaseline runSdk(const QString& path, const std::atomic_bool* cancel)
{
    return {sdk::probePdf(path, cancel), sdk::probeAnalysis(path, cancel)};
}

#ifdef Q_OS_WIN
DWORD g_guiThreadId = 0;

// Diagnostics only: reports fatal native exceptions (including PDFium's
// out-of-memory termination 0xE0000008) with the modules on the stack, then
// lets normal crash handling continue.
LONG CALLBACK reportFatalException(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != 0xE0000008 && code != EXCEPTION_ACCESS_VIOLATION && code != 0xC0000409 && code != 0xC0000374)
        return EXCEPTION_CONTINUE_SEARCH;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    std::fprintf(stderr, "fatal.exception=0x%08lX thread=%s private_mb=%llu\n", code,
                 GetCurrentThreadId() == g_guiThreadId ? "gui" : "worker",
                 static_cast<unsigned long long>(memory.PrivateUsage / (1024 * 1024)));
    void* frames[40];
    const USHORT count = CaptureStackBackTrace(0, 40, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        wchar_t name[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCWSTR>(frames[i]), &module))
            GetModuleFileNameW(module, name, MAX_PATH);
        const wchar_t* base = wcsrchr(name, L'\\');
        std::fprintf(stderr, "fatal.frame%02u=%ls+0x%llx\n", i, base ? base + 1 : name,
                     static_cast<unsigned long long>(reinterpret_cast<const char*>(frames[i])
                                                     - reinterpret_cast<const char*>(module)));
    }
    std::fflush(nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void phase(QTextStream& out, const char* name)
{
    out << "phase." << name << "=start" << Qt::endl;
}

class Reporter {
public:
    explicit Reporter(QTextStream& out) : m_out(out) {}
    void report(const char* name, bool pass, const QString& detail)
    {
        m_out << "check." << name << '=' << (pass ? "PASS" : "FAIL") << ' ' << detail << Qt::endl;
        if (!pass)
            ++m_failures;
    }
    int failures() const { return m_failures; }

private:
    QTextStream& m_out;
    int m_failures = 0;
};

} // namespace

int runReaderCheck(const ReaderCheckOptions& options, QTextStream& out)
{
    Reporter r(out);
#ifdef Q_OS_WIN
    g_guiThreadId = GetCurrentThreadId();
    void* handler = AddVectoredExceptionHandler(1, reportFatalException);
    const auto removeHandler = qScopeGuard([handler] { RemoveVectoredExceptionHandler(handler); });
#endif
    const int timeoutMs = options.timeoutSeconds * 1000;
    const QString path = options.pdfPath;

    phase(out, "qtpdf_render");
    // 1. Qt PDF alone: load and render every page.
    QString error, label;
    const QList<QByteArray> baselineRender = renderAll(path, &error, &label);
    r.report("qtpdf_render", !baselineRender.isEmpty(),
             baselineRender.isEmpty() ? error
                                      : QStringLiteral("%1 pages rendered; page 1 label \"%2\"")
                                            .arg(baselineRender.size())
                                            .arg(label));
    if (baselineRender.isEmpty())
        return r.failures();

    phase(out, "sdk_alone");
    // 2. SDK alone on a worker thread: the reference results.
    SdkBaseline baseline;
    std::atomic_bool noCancel{false};
    const bool baselineDone = runOnWorker([&] { baseline = runSdk(path, &noCancel); }, {}, timeoutMs);
    r.report("sdk_alone", baselineDone && baseline.metadata.ok && baseline.analysis.ok,
             QStringLiteral("title %1 \"%2\"; analysis %3, %4 parsed, %5 resolved, plan_ready=%6%7")
                 .arg(baseline.metadata.titleStatus, baseline.metadata.title, baseline.analysis.outcome)
                 .arg(baseline.analysis.parsedEntries)
                 .arg(baseline.analysis.resolvedEntries)
                 .arg(baseline.analysis.planReady)
                 .arg(baseline.metadata.ok && baseline.analysis.ok
                          ? QString()
                          : QStringLiteral("; error: ") + baseline.metadata.error + baseline.analysis.error));
    if (!baselineDone || !baseline.metadata.ok || !baseline.analysis.ok)
        return r.failures();

    phase(out, "concurrent");
    // 3. Concurrent: SDK rounds on the worker while the GUI thread repeatedly
    //    loads, renders and closes documents.
    {
        QList<SdkBaseline> rounds;
        int viewCycles = 0, renderMismatches = 0, viewFailures = 0;
        QElapsedTimer timer;
        timer.start();
        const bool finished = runOnWorker(
            [&] {
                for (int i = 0; i < options.rounds; ++i)
                    rounds << runSdk(path, &noCancel);
            },
            options.viewDuringRounds ? std::function<void()>([&] {
                QString e;
                const auto digests = renderAll(path, &e);
                ++viewCycles;
                if (digests.isEmpty())
                    ++viewFailures;
                else if (digests != baselineRender)
                    ++renderMismatches;
            })
                                     : std::function<void()>(),
            timeoutMs);
        int sdkMismatches = 0;
        for (const SdkBaseline& round : rounds) {
            if (!sameSdkResults(round, baseline))
                ++sdkMismatches;
        }
        r.report(options.viewDuringRounds ? "concurrent_view_and_analysis" : "sdk_rounds_without_view",
                 finished && rounds.size() == options.rounds && sdkMismatches == 0 && renderMismatches == 0
                     && viewFailures == 0 && (viewCycles > 0 || !options.viewDuringRounds),
                 QStringLiteral("%1 SDK rounds (%2 differing), %3 load/render/close cycles (%4 differing, %5 "
                                "failed) in %6 ms")
                     .arg(rounds.size())
                     .arg(sdkMismatches)
                     .arg(viewCycles)
                     .arg(renderMismatches)
                     .arg(viewFailures)
                     .arg(timer.elapsed()));
    }

    phase(out, "cancel_while_viewing");
    // 4. Cancellation while viewing: request cancel as soon as analysis starts.
    {
        auto cancel = std::make_shared<std::atomic_bool>(false);
        sdk::AnalysisProbe result;
        bool requested = false;
        QElapsedTimer sinceCancel;
        QPdfDocument viewer;
        viewer.load(path);
        int page = 0;
        const bool returned = runOnWorker(
            [&, cancel] { result = sdk::probeAnalysis(path, cancel.get()); },
            [&] {
                viewer.render(page++ % viewer.pageCount(), kRenderSize);
                if (!requested) {
                    cancel->store(true);
                    requested = true;
                    sinceCancel.start();
                }
            },
            timeoutMs);
        const bool cancelled = result.cancelledError || result.outcome == QLatin1String("cancelled");
        r.report("cancel_while_viewing", returned && (cancelled || result.ok),
                 QStringLiteral("returned %1 ms after cancel: %2")
                     .arg(sinceCancel.isValid() ? sinceCancel.elapsed() : -1)
                     .arg(cancelled ? QStringLiteral("cancelled")
                                    : result.ok ? QStringLiteral("completed before observing cancel (%1)").arg(result.outcome)
                                                : QStringLiteral("error: ") + result.error));
    }

    phase(out, "shutdown_during_analysis");
    // 5. Shutdown order: the viewer is destroyed and cancel requested while
    //    analysis runs; the GUI thread keeps processing events until it ends.
    {
        auto cancel = std::make_shared<std::atomic_bool>(false);
        auto viewer = std::make_unique<QPdfDocument>();
        viewer->load(path);
        viewer->render(0, kRenderSize);
        bool closed = false;
        int eventTurns = 0;
        QElapsedTimer timer;
        timer.start();
        const bool returned = runOnWorker(
            [path, cancel] { sdk::probeAnalysis(path, cancel.get()); },
            [&] {
                ++eventTurns;
                if (!closed) {
                    viewer.reset();
                    cancel->store(true);
                    closed = true;
                }
            },
            timeoutMs);
        r.report("shutdown_during_analysis", returned && closed,
                 QStringLiteral("viewer destroyed first; worker finished after %1 ms; GUI thread ran %2 event "
                                "turns meanwhile")
                     .arg(timer.elapsed())
                     .arg(eventTurns));
    }

    phase(out, "qml_pdf_module");
    // 6. Qt Quick PDF module: PdfDocument and PdfMultiPageView instantiate.
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(QByteArrayLiteral(R"(
            import QtQuick
            import QtQuick.Pdf
            Item {
                width: 400; height: 600
                property alias status: doc.status
                property alias pageCount: doc.pageCount
                PdfDocument { id: doc }
                PdfMultiPageView { anchors.fill: parent; document: doc }
                Component.onCompleted: doc.source = sourceUrl
                property url sourceUrl
            })"),
                          QUrl(QStringLiteral("qrc:/readercheck.qml")));
        std::unique_ptr<QObject> root(component.createWithInitialProperties(
            {{QStringLiteral("sourceUrl"), QUrl::fromLocalFile(path)}}));
        QString detail;
        bool pass = false;
        if (!root) {
            detail = component.errorString().simplified();
        } else {
            QElapsedTimer timer;
            timer.start();
            while (root->property("status").toInt() != int(QPdfDocument::Status::Ready) && timer.elapsed() < 5000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            const int pages = root->property("pageCount").toInt();
            pass = root->property("status").toInt() == int(QPdfDocument::Status::Ready) && pages > 0;
            detail = QStringLiteral("QtQuick.Pdf loaded; PdfDocument status %1, %2 pages")
                         .arg(root->property("status").toInt())
                         .arg(pages);
        }
        r.report("qml_pdf_module", pass, detail);
    }

    phase(out, "non_ascii_path");
    // 7. Non-ASCII path through both libraries.
    {
        QTemporaryDir dir;
        const QString copy = QDir(dir.path()).filePath(QStringLiteral("Βιβλία ü/Τίτλος ü.pdf"));
        QDir().mkpath(QFileInfo(copy).absolutePath());
        const bool copied = QFile::copy(path, copy);
        QString e;
        const auto digests = copied ? renderAll(copy, &e) : QList<QByteArray>{};
        const sdk::PdfProbe probe = copied ? sdk::probePdf(copy, &noCancel) : sdk::PdfProbe{};
        r.report("non_ascii_path", copied && digests == baselineRender && probe.ok && probe.sha256 == baseline.metadata.sha256,
                 QStringLiteral("copied=%1, render %2, sdk %3")
                     .arg(copied)
                     .arg(digests == baselineRender ? QStringLiteral("identical") : QStringLiteral("differs ") + e)
                     .arg(probe.ok ? QStringLiteral("same digest") : probe.error));
    }

    return r.failures();
}

} // namespace mbl::reader
