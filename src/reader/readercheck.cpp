#include "reader/readercheck.h"

#include "infrastructure/memorystats.h"
#include "processing/sdk/sdkinfo.h"
#include "reader/checkverdict.h"

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
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <thread>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#endif

namespace mbl::reader {

namespace {

using infrastructure::MemoryPeaks;
using infrastructure::sampleMemory;

const QSize kRenderSize(150, 200);

QString mb(quint64 bytes)
{
    return QString::number(bytes / (1024 * 1024));
}

QString peaksText(const MemoryPeaks& p)
{
    if (p.samples == 0)
        return QStringLiteral("memory n/a");
    return QStringLiteral("peak process private %1 MB, peak system commit %2/%3 MB, min available physical %4 MB")
        .arg(mb(p.processPrivateBytes), mb(p.systemCommitBytes), mb(p.systemCommitLimitBytes),
             mb(p.minAvailablePhysicalBytes));
}

// Samples memory at most every 250 ms, and prints a flushed line every 2 s so
// the data survives a crash of the process.
class MemoryTrace {
public:
    MemoryTrace(QTextStream& out, const char* phase) : m_out(out), m_phase(phase) { m_timer.start(); }
    void tick()
    {
        if (m_timer.elapsed() - m_lastSample < 250 && m_peaks.samples > 0)
            return;
        m_lastSample = m_timer.elapsed();
        const auto s = sampleMemory();
        m_peaks.add(s);
        if (s.available && m_timer.elapsed() - m_lastPrint >= 2000) {
            m_lastPrint = m_timer.elapsed();
            m_out << "sample." << m_phase << " t_ms=" << m_timer.elapsed() << " private_mb=" << mb(s.processPrivateBytes)
                  << " commit_mb=" << mb(s.systemCommitBytes) << " commit_limit_mb=" << mb(s.systemCommitLimitBytes)
                  << " avail_phys_mb=" << mb(s.systemAvailablePhysicalBytes) << Qt::endl;
        }
    }
    const MemoryPeaks& peaks() const { return m_peaks; }

private:
    QTextStream& m_out;
    const char* m_phase;
    QElapsedTimer m_timer;
    qint64 m_lastSample = 0;
    qint64 m_lastPrint = 0;
    MemoryPeaks m_peaks;
};

// Runs `work` on a plain worker thread while the GUI thread keeps running
// `whileRunning` and processing events. Never blocks the GUI thread on the
// worker. On timeout the harness process exits with code 3, because the
// still-running worker may reference this stack frame.
bool runOnWorker(const std::function<void()>& work, const std::function<void()>& whileRunning, int timeoutMs,
                 MemoryTrace* trace = nullptr)
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
        if (trace)
            trace->tick();
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

QByteArray imageDigest(const QImage& image)
{
    return QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()),
        QCryptographicHash::Sha256);
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
        digests << imageDigest(image);
    }
    document.close();
    return digests;
}

// GUI-thread viewing workload for one mode; counts renders and mismatches.
class Viewer {
public:
    Viewer(ViewMode mode, const QString& path, const QList<QByteArray>& baseline)
        : m_mode(mode), m_path(path), m_baseline(baseline)
    {
        if (m_mode == ViewMode::Persistent)
            m_document.load(path);
    }
    void turn()
    {
        if (m_mode == ViewMode::Churn) {
            QString e;
            const auto digests = renderAll(m_path, &e);
            ++m_cycles;
            m_pages += int(digests.size());
            if (digests.isEmpty())
                ++m_failures;
            else if (digests != m_baseline)
                ++m_mismatches;
        } else if (m_mode == ViewMode::Persistent) {
            const int page = m_pages % int(m_baseline.size());
            const QImage image = m_document.render(page, kRenderSize);
            ++m_pages;
            if (image.isNull())
                ++m_failures;
            else if (imageDigest(image) != m_baseline.at(page))
                ++m_mismatches;
        }
    }
    bool clean() const { return m_failures == 0 && m_mismatches == 0 && (m_mode == ViewMode::None || m_pages > 0); }
    QString summary() const
    {
        switch (m_mode) {
        case ViewMode::Churn:
            return QStringLiteral("%1 load/render/close cycles, %2 pages (%3 differing, %4 failed)")
                .arg(m_cycles).arg(m_pages).arg(m_mismatches).arg(m_failures);
        case ViewMode::Persistent:
            return QStringLiteral("persistent viewer rendered %1 pages (%2 differing, %3 failed)")
                .arg(m_pages).arg(m_mismatches).arg(m_failures);
        case ViewMode::None:
            return QStringLiteral("no viewing");
        }
        return {};
    }

private:
    ViewMode m_mode;
    QString m_path;
    QList<QByteArray> m_baseline;
    QPdfDocument m_document;
    int m_cycles = 0, m_pages = 0, m_mismatches = 0, m_failures = 0;
};

struct SdkRound {
    sdk::PdfProbe metadata;
    sdk::AnalysisProbe analysis;
};

SdkRound runSdk(const QString& path, const std::atomic_bool* cancel, const sdk::ProbeOptions& options)
{
    return {sdk::probePdf(path, cancel, options), sdk::probeAnalysis(path, cancel, options)};
}

bool sameSemantics(const SdkRound& a, const SdkRound& b)
{
    return a.metadata.ok && b.metadata.ok && a.analysis.ok && b.analysis.ok
           && a.metadata.sha256 == b.metadata.sha256 && a.metadata.semanticDigest == b.metadata.semanticDigest
           && a.analysis.semanticDigest == b.analysis.semanticDigest;
}

// First line where two snapshots differ, for diagnostics.
QString firstDifference(const QString& a, const QString& b)
{
    const QStringList la = a.split(u'\n'), lb = b.split(u'\n');
    for (qsizetype i = 0; i < qMax(la.size(), lb.size()); ++i) {
        const QString x = la.value(i), y = lb.value(i);
        if (x != y)
            return QStringLiteral("line %1: \"%2\" vs \"%3\"").arg(i + 1).arg(x, y);
    }
    return {};
}

QString ocrText(const SdkRound& r)
{
    return QStringLiteral("models \"%1\", metadata OCR attempts %2, analysis OCR attempts %3 (pages completed %4, "
                          "failed %5, skipped %6)")
        .arg(r.analysis.modelIdentity.isEmpty() ? QStringLiteral("none") : r.analysis.modelIdentity)
        .arg(r.metadata.ocrAttemptsUsed)
        .arg(r.analysis.ocrAttemptsUsed)
        .arg(r.analysis.ocrPagesCompleted)
        .arg(r.analysis.ocrPagesFailed)
        .arg(r.analysis.ocrPagesSkipped);
}

#ifdef Q_OS_WIN
DWORD g_guiThreadId = 0;

// Diagnostics only: reports fatal native exceptions (including PDFium's
// out-of-memory termination 0xE0000008) with memory figures and the modules
// on the stack, then lets normal crash handling continue.
LONG CALLBACK reportFatalException(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != 0xE0000008 && code != EXCEPTION_ACCESS_VIOLATION && code != 0xC0000409 && code != 0xC0000374)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto memory = sampleMemory();
    std::fprintf(stderr,
                 "fatal.exception=0x%08lX thread=%s private_mb=%llu commit_mb=%llu commit_limit_mb=%llu "
                 "avail_phys_mb=%llu\n",
                 code, GetCurrentThreadId() == g_guiThreadId ? "gui" : "worker",
                 static_cast<unsigned long long>(memory.processPrivateBytes >> 20),
                 static_cast<unsigned long long>(memory.systemCommitBytes >> 20),
                 static_cast<unsigned long long>(memory.systemCommitLimitBytes >> 20),
                 static_cast<unsigned long long>(memory.systemAvailablePhysicalBytes >> 20));
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

// Full path of a loaded module whose file name is one of `names`, if any.
QString loadedModulePath(std::initializer_list<const wchar_t*> names)
{
    for (const wchar_t* n : names) {
        if (HMODULE module = GetModuleHandleW(n)) {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(module, path, MAX_PATH);
            return QDir::toNativeSeparators(QString::fromWCharArray(path));
        }
    }
    return QStringLiteral("(not loaded)");
}
#endif

void phase(QTextStream& out, const char* name)
{
    out << "phase." << name << "=start" << Qt::endl;
}

class Reporter {
public:
    explicit Reporter(QTextStream& out) : m_out(out) {}
    void report(const char* name, Verdict verdict, const QString& detail)
    {
        m_out << "check." << name << '=' << verdictName(verdict) << ' ' << detail << Qt::endl;
        if (verdict != Verdict::Pass)
            ++m_notPassed;
    }
    void report(const char* name, bool pass, const QString& detail)
    {
        report(name, pass ? Verdict::Pass : Verdict::Fail, detail);
    }
    int notPassed() const { return m_notPassed; }

private:
    QTextStream& m_out;
    int m_notPassed = 0;
};

const char* viewName(ViewMode mode)
{
    switch (mode) {
    case ViewMode::Churn: return "churn";
    case ViewMode::Persistent: return "persistent";
    case ViewMode::None: return "none";
    }
    return "?";
}

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
    const sdk::ProbeOptions probeOptions{options.useModels, {}};
    out << "config view=" << viewName(options.view) << " rounds=" << options.rounds
        << " require_ocr=" << options.requireOcr << " models=" << options.useModels << Qt::endl;

    // 1. Qt PDF alone: load and render every page.
    phase(out, "qtpdf_render");
    QString error, label;
    const QList<QByteArray> baselineRender = renderAll(path, &error, &label);
    r.report("qtpdf_render", !baselineRender.isEmpty(),
             baselineRender.isEmpty() ? error
                                      : QStringLiteral("%1 pages rendered; page 1 label \"%2\"")
                                            .arg(baselineRender.size())
                                            .arg(label));
    if (baselineRender.isEmpty())
        return r.notPassed();

    // 2. SDK alone on a worker thread: the reference results.
    phase(out, "sdk_alone");
    SdkRound baseline;
    std::atomic_bool noCancel{false};
    MemoryTrace aloneTrace(out, "sdk_alone");
    QElapsedTimer aloneTimer;
    aloneTimer.start();
    runOnWorker([&] { baseline = runSdk(path, &noCancel, probeOptions); }, {}, timeoutMs, &aloneTrace);
    const qint64 aloneMs = aloneTimer.elapsed();
    const bool sdkOk = baseline.metadata.ok && baseline.analysis.ok;
    const Verdict ocr = ocrWorkloadVerdict(options.requireOcr, baseline.analysis.ocrPagesCompleted);
    r.report("sdk_alone", !sdkOk ? Verdict::Fail : ocr,
             QStringLiteral("%1 ms; title %2 \"%3\"; analysis %4, %5 parsed, %6 resolved, plan_ready=%7; %8; "
                            "metadata digest %9, analysis digest %10; %11%12")
                 .arg(aloneMs)
                 .arg(baseline.metadata.titleStatus, baseline.metadata.title, baseline.analysis.outcome)
                 .arg(baseline.analysis.parsedEntries)
                 .arg(baseline.analysis.resolvedEntries)
                 .arg(baseline.analysis.planReady)
                 .arg(ocrText(baseline), baseline.metadata.semanticDigest.left(12),
                      baseline.analysis.semanticDigest.left(12), peaksText(aloneTrace.peaks()))
                 .arg(sdkOk ? QString() : QStringLiteral("; error: ") + baseline.metadata.error + baseline.analysis.error));
    if (!sdkOk || ocr != Verdict::Pass) {
        out << "stopped: the SDK reference run did not exercise the requested workload" << Qt::endl;
        return r.notPassed();
    }

    // 3. Qt-only control: the same viewing workload for the SDK baseline's
    //    duration, without SDK work, to separate viewer effects from coexistence.
    if (options.qtControl && options.view != ViewMode::None) {
        phase(out, "qt_only_control");
        MemoryTrace trace(out, "qt_only_control");
        Viewer viewer(options.view, path, baselineRender);
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < aloneMs * options.rounds) {
            viewer.turn();
            trace.tick();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        r.report("qt_only_control", viewer.clean(),
                 QStringLiteral("view=%1 for %2 ms: %3; %4")
                     .arg(QLatin1StringView(viewName(options.view)))
                     .arg(timer.elapsed())
                     .arg(viewer.summary(), peaksText(trace.peaks())));
    }

    // 4. Concurrent: SDK rounds on the worker while the GUI thread views.
    {
        phase(out, "concurrent");
        MemoryTrace trace(out, "concurrent");
        QList<SdkRound> rounds;
        Viewer viewer(options.view, path, baselineRender);
        QElapsedTimer timer;
        timer.start();
        runOnWorker(
            [&] {
                for (int i = 0; i < options.rounds; ++i)
                    rounds << runSdk(path, &noCancel, probeOptions);
            },
            options.view == ViewMode::None ? std::function<void()>() : std::function<void()>([&] { viewer.turn(); }),
            timeoutMs, &trace);
        int differing = 0, ocrMissing = 0;
        QString difference;
        for (const SdkRound& round : rounds) {
            if (!sameSemantics(round, baseline)) {
                ++differing;
                if (difference.isEmpty()) {
                    difference = firstDifference(baseline.metadata.semanticSnapshot + u'\n' + baseline.analysis.semanticSnapshot,
                                                 round.metadata.semanticSnapshot + u'\n' + round.analysis.semanticSnapshot);
                }
            }
            if (ocrWorkloadVerdict(options.requireOcr, round.analysis.ocrPagesCompleted) != Verdict::Pass)
                ++ocrMissing;
        }
        const char* name = options.view == ViewMode::None ? "sdk_rounds_without_view"
                           : options.view == ViewMode::Persistent ? "concurrent_persistent_view_and_sdk"
                                                                  : "concurrent_churn_view_and_sdk";
        const Verdict verdict = ocrMissing > 0 ? Verdict::NotExercised
                                : (differing == 0 && rounds.size() == options.rounds && viewer.clean()) ? Verdict::Pass
                                                                                                        : Verdict::Fail;
        r.report(name, verdict,
                 QStringLiteral("%1 SDK rounds (%2 semantically differing%3, %4 without completed OCR); %5; %6 ms; %7")
                     .arg(rounds.size())
                     .arg(differing)
                     .arg(difference.isEmpty() ? QString() : QStringLiteral(", first: ") + difference)
                     .arg(ocrMissing)
                     .arg(viewer.summary())
                     .arg(timer.elapsed())
                     .arg(peaksText(trace.peaks())));
    }

    // 5. Cancellation while viewing. The worker pauses inside its first
    //    progress callback until the GUI thread has requested cancellation, so
    //    the request provably happens while analysis is active.
    {
        phase(out, "cancel_while_viewing");
        auto cancel = std::make_shared<std::atomic_bool>(false);
        auto started = std::make_shared<std::atomic_bool>(false);
        auto requested = std::make_shared<std::atomic_bool>(false);
        auto done = std::make_shared<std::atomic_bool>(false);
        sdk::ProbeOptions handshake = probeOptions;
        handshake.onProgress = [started, requested] {
            if (started->exchange(true))
                return;
            QElapsedTimer wait;
            wait.start();
            while (!requested->load() && wait.elapsed() < 30000)
                QThread::msleep(1);
        };
        sdk::AnalysisProbe result;
        CancellationObservation observation;
        QElapsedTimer sinceRequest;
        Viewer viewer(ViewMode::Persistent, path, baselineRender);
        observation.returned = runOnWorker(
            [&, cancel, done] {
                result = sdk::probeAnalysis(path, cancel.get(), handshake);
                done->store(true);
            },
            [&] {
                viewer.turn();
                if (!observation.requestMade && started->load()) {
                    observation.workStartedBeforeRequest = true;
                    observation.requestedWhileActive = !done->load();
                    cancel->store(true);
                    observation.requestMade = true;
                    sinceRequest.start();
                    requested->store(true);
                }
            },
            timeoutMs);
        observation.cancellationObserved = result.cancelledError || result.outcome == QLatin1String("cancelled");
        r.report("cancel_while_viewing", cancellationVerdict(observation),
                 QStringLiteral("request while active=%1 after %2 progress callbacks; returned %3 ms after request: %4; "
                                "%5")
                     .arg(observation.requestedWhileActive)
                     .arg(result.progressCallbacks)
                     .arg(sinceRequest.isValid() ? sinceRequest.elapsed() : -1)
                     .arg(observation.cancellationObserved ? QStringLiteral("cancelled")
                          : result.ok ? QStringLiteral("completed normally (%1)").arg(result.outcome)
                                      : QStringLiteral("error: ") + result.error)
                     .arg(viewer.summary()));
    }

    // 6. Shutdown order: once analysis has started, destroy the viewer and
    //    request cancellation; the GUI thread keeps serving events until the
    //    worker returns.
    {
        phase(out, "shutdown_during_analysis");
        auto cancel = std::make_shared<std::atomic_bool>(false);
        auto started = std::make_shared<std::atomic_bool>(false);
        auto requested = std::make_shared<std::atomic_bool>(false);
        sdk::ProbeOptions handshake = probeOptions;
        handshake.onProgress = [started, requested] {
            if (started->exchange(true))
                return;
            QElapsedTimer wait;
            wait.start();
            while (!requested->load() && wait.elapsed() < 30000)
                QThread::msleep(1);
        };
        auto viewer = std::make_unique<QPdfDocument>();
        viewer->load(path);
        viewer->render(0, kRenderSize);
        sdk::AnalysisProbe result;
        bool closedWhileActive = false;
        int eventTurns = 0;
        QElapsedTimer timer;
        const bool returned = runOnWorker(
            [&, cancel] { result = sdk::probeAnalysis(path, cancel.get(), handshake); },
            [&] {
                ++eventTurns;
                if (viewer && started->load()) {
                    viewer.reset();
                    closedWhileActive = true;
                    cancel->store(true);
                    timer.start();
                    requested->store(true);
                }
            },
            timeoutMs);
        const Verdict verdict = !returned ? Verdict::Fail : closedWhileActive ? Verdict::Pass : Verdict::NotExercised;
        r.report("shutdown_during_analysis", verdict,
                 QStringLiteral("viewer destroyed while analysis active=%1; worker returned %2 ms later (%3); GUI "
                                "thread ran %4 event turns")
                     .arg(closedWhileActive)
                     .arg(timer.isValid() ? timer.elapsed() : -1)
                     .arg(result.cancelledError || result.outcome == QLatin1String("cancelled")
                              ? QStringLiteral("cancelled")
                              : result.ok ? QStringLiteral("completed ") + result.outcome : result.error)
                     .arg(eventTurns));
    }

    // 7. Qt Quick PDF module, from the registered qml/reader/ReaderCheckView.qml.
    {
        phase(out, "qml_pdf_module");
        QQmlEngine engine;
        QQmlComponent component(&engine, QStringLiteral("MyBooksLibrary"), QStringLiteral("ReaderCheckView"));
        std::unique_ptr<QObject> root(component.isReady()
                                          ? component.createWithInitialProperties(
                                                {{QStringLiteral("sourceUrl"), QUrl::fromLocalFile(path)}})
                                          : nullptr);
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
            detail = QStringLiteral("PdfDocument status %1, %2 pages").arg(root->property("status").toInt()).arg(pages);
        }
#ifdef Q_OS_WIN
        detail += QStringLiteral("; pdfquickplugin: %1; Qt6PdfQuick: %2")
                      .arg(loadedModulePath({L"pdfquickplugin.dll", L"pdfquickplugind.dll"}),
                           loadedModulePath({L"Qt6PdfQuick.dll", L"Qt6PdfQuickd.dll"}));
#endif
        detail += QStringLiteral("; import paths: %1").arg(engine.importPathList().join(u';'));
        r.report("qml_pdf_module", pass, detail);
    }

    // 8. Non-ASCII path through both libraries.
    {
        phase(out, "non_ascii_path");
        QTemporaryDir dir;
        const QString copy = QDir(dir.path()).filePath(QStringLiteral("Βιβλία ü/Τίτλος ü.pdf"));
        QDir().mkpath(QFileInfo(copy).absolutePath());
        const bool copied = QFile::copy(path, copy);
        QString e;
        const auto digests = copied ? renderAll(copy, &e) : QList<QByteArray>{};
        const sdk::PdfProbe probe = copied ? sdk::probePdf(copy, &noCancel, probeOptions) : sdk::PdfProbe{};
        r.report("non_ascii_path",
                 copied && digests == baselineRender && probe.ok && probe.sha256 == baseline.metadata.sha256
                     && probe.semanticDigest == baseline.metadata.semanticDigest,
                 QStringLiteral("copied=%1, render %2, sdk %3")
                     .arg(copied)
                     .arg(digests == baselineRender ? QStringLiteral("identical") : QStringLiteral("differs ") + e)
                     .arg(probe.ok ? (probe.semanticDigest == baseline.metadata.semanticDigest
                                          ? QStringLiteral("same digest and metadata")
                                          : QStringLiteral("metadata differs"))
                                   : probe.error));
    }

    return r.notPassed();
}

} // namespace mbl::reader
