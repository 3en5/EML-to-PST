// WLM2PST - minimal expected-style result type and context-rich error object.
//
// Portable core code must not depend on <windows.h>; HRESULT / Win32 codes are
// therefore carried as plain integers here. The MAPI layer converts real
// HRESULTs into Error via make_hresult_error().
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace wlm2pst {

// Context-rich error. `message` is an operator-facing, privacy-safe UTF-8
// description: it must never contain message subjects, bodies, or addresses.
struct Error {
    // HRESULT as a 32-bit value (0 when not applicable).
    int32_t hresult = 0;
    // Win32 error code from GetLastError (0 when not applicable).
    uint32_t win32 = 0;
    // Short machine-readable operation name, e.g. "MIMEToMAPI", "CreateFileW".
    std::string operation;
    // Privacy-safe human-readable detail (UTF-8).
    std::string message;

    [[nodiscard]] bool has_code() const noexcept { return hresult != 0 || win32 != 0; }
};

inline Error make_error(std::string operation, std::string message) {
    return Error{0, 0, std::move(operation), std::move(message)};
}

inline Error make_hresult_error(int32_t hr, std::string operation, std::string message = {}) {
    return Error{hr, 0, std::move(operation), std::move(message)};
}

inline Error make_win32_error(uint32_t code, std::string operation, std::string message = {}) {
    return Error{0, code, std::move(operation), std::move(message)};
}

// Minimal std::expected substitute (project targets C++20; std::expected is C++23).
template <typename T>
class Result {
public:
    Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & { return std::get<0>(storage_); }
    [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(storage_)); }

    [[nodiscard]] Error& error() & { return std::get<1>(storage_); }
    [[nodiscard]] const Error& error() const& { return std::get<1>(storage_); }

private:
    std::variant<T, Error> storage_;
};

// Result<void> analogue.
class Status {
public:
    Status() = default;
    Status(Error error) : error_(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] const Error& error() const { return *error_; }

    static Status success() { return Status{}; }

private:
    std::optional<Error> error_;
};

}  // namespace wlm2pst
