// Collections: named groups of books. A book can be in several collections;
// membership never copies files. Trashed books keep their memberships but
// are not listed or counted until restored.
#pragma once

#include "domain/ids.h"

#include <QString>

namespace mbl::domain {

struct CollectionSummary {
    CollectionId id;
    QString name;
    int bookCount = 0;  // Active books only.
};

} // namespace mbl::domain
