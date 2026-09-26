// A1: the library folder layout. The catalog stores paths relative to the
// root, so a library folder can be moved as a whole.
//
//   library.sqlite                     catalog (A2)
//   files/<asset-id>/source.pdf        immutable managed copy
//   reports/<run-id>.json              immutable SDK report (M04)
//   derivatives/<asset-id>/<id>.pdf    generated copies (M09)
//   staging/<import-id>/               incomplete import
//   cache/                             rebuildable artifacts only
#pragma once

#include "domain/ids.h"

#include <QString>

namespace mbl::storage {

class LibraryLayout {
public:
    explicit LibraryLayout(QString root);

    QString root() const { return m_root; }
    QString absolute(const QString& relativePath) const;

    static QString managedSourcePath(const domain::AssetId& asset);    // Relative.
    static QString stagingDirectory(const domain::ImportId& import);   // Relative.
    static QString stagedSourcePath(const domain::ImportId& import);   // Relative.

    // Creates files/, staging/, reports/, derivatives/ and cache/ if needed.
    bool ensureDirectories(QString* error) const;

private:
    QString m_root;
};

} // namespace mbl::storage
