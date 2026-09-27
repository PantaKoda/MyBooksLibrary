// Book, asset and run contracts shared by the catalog, search and processing.
#pragma once

#include "domain/ids.h"
#include "domain/metadata.h"
#include "domain/toc.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <optional>

namespace mbl::domain {

enum class Lifecycle { Active, Trashed };

// A managed PDF file. Identity is its exact SHA-256.
struct AssetRecord {
    AssetId id;
    QString sha256;                  // Lower-case hex.
    qint64 byteSize = 0;
    std::optional<int> pageCount;    // Unknown at import is valid.
    QString managedPath;             // Relative to the library root.
};

// Everything needed to register a newly imported book.
struct NewBook {
    AssetRecord asset;
    QString originalFileName;        // Provenance and display fallback only.
    QString originalPath;            // Provenance only; may later move or vanish.
};

// Identity of one SDK run, stored with its normalised results.
struct RunIdentity {
    QString sourceSha256;            // Must match the book's asset at publication.
    QString sdkVersion;
    QString modelIdentity;           // Empty when OCR models were unavailable.
    QString optionsJson;             // Options the run used.
    QString outcome;                 // SDK outcome/status name.
    std::optional<QString> reportPath; // Raw report, relative to the library root.
};

// Generation a job captured when it was requested. Publication succeeds only
// if it still matches, so late results from superseded requests are rejected.
struct PublishTicket {
    BookId book;
    qint64 generation = 0;
    QString sourceSha256;
};

struct BookSummary {
    BookId id;
    AssetId assetId;
    Lifecycle lifecycle = Lifecycle::Active;
    qint64 revision = 0;             // Bumped on every catalog change to the book.
    EffectiveMetadata metadata;
    QString displayTitle;            // Effective title, else the original file name.
    bool displayTitleFromFileName = false;
    bool hasMetadataRun = false;
    std::optional<FieldStatus> extractedTitleStatus;  // Of the active metadata run, if any.
    bool hasTocRun = false;
    int tocEntryCount = 0;
};

struct BookDetails {
    BookSummary summary;
    AssetRecord asset;
    QString originalFileName;
    QString originalPath;
    std::optional<ExtractedMetadata> extracted;  // Active metadata run.
    MetadataOverrides overrides;
    std::optional<TocAnalysis> toc;              // Active TOC run.
    qint64 metadataGeneration = 0;
    qint64 tocGeneration = 0;
};

QString toCode(Lifecycle lifecycle);
std::optional<Lifecycle> lifecycleFromCode(const QString& code);

} // namespace mbl::domain
