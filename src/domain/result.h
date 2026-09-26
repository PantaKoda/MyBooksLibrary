// Result values for application operations. Expected domain refusals (a
// stale generation, a trashed book) are errors with a code, not exceptions.
#pragma once

#include <QString>

#include <optional>
#include <utility>

namespace mbl::domain {

enum class ErrorCode {
    NotFound,         // No such book or record.
    StaleGeneration,  // A newer request superseded this result.
    SourceMismatch,   // Result was produced from different source bytes.
    Trashed,          // The book is in Trash; results are not published.
    InvalidArgument,  // Caller supplied inconsistent values.
    Database,         // SQL failure; the transaction was rolled back.
    SchemaTooNew,     // Catalog was written by a newer application.
    LibraryLocked,    // Another process holds the library writer lock.
    Io,               // Filesystem problem.
    Duplicate,        // A book with the same SHA-256 already exists.
};

struct Error {
    ErrorCode code = ErrorCode::Database;
    QString message;
};

template <typename T>
class Result {
public:
    Result(T value) : m_value(std::move(value)) {}
    Result(Error error) : m_error(std::move(error)) {}

    bool ok() const { return m_value.has_value(); }
    explicit operator bool() const { return ok(); }
    const T& value() const { return *m_value; }
    T& value() { return *m_value; }
    const Error& error() const { return *m_error; }

private:
    std::optional<T> m_value;
    std::optional<Error> m_error;
};

// For operations with no value.
struct Done {};
using Status = Result<Done>;

inline Error makeError(ErrorCode code, QString message)
{
    return Error{code, std::move(message)};
}

} // namespace mbl::domain
