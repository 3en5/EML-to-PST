// WLM2PST - small RAII wrappers around the vendored SQLite C API.
//
// Portable: no Windows headers. Every failing SQLite call is surfaced as a
// wlm2pst::Error. For this module, Error::hresult carries the SQLite
// *extended* result code (see sqlite3_extended_errcode) rather than an
// HRESULT, and Error::message carries sqlite3_errmsg(). Error::win32 is
// unused here and stays 0.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/errors/result.h"

struct sqlite3;
struct sqlite3_stmt;

namespace wlm2pst::sqlite_util {

// A single SQLite connection. Opens with CREATE|READWRITE, a 5s busy
// timeout, WAL journaling, synchronous=NORMAL and foreign_keys=ON.
class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    // db_path is a native wide path (UTF-16 on Windows, UTF-32 on Linux test
    // builds); it is converted to UTF-8 internally before being handed to
    // sqlite3_open_v2, which expects UTF-8 filenames.
    static Result<Database> open(const std::wstring& db_path);

    // Runs one or more ';'-separated statements with no bound parameters or
    // result rows (DDL, PRAGMAs, BEGIN/COMMIT/ROLLBACK).
    Status exec(const std::string& sql);

    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }
    [[nodiscard]] bool valid() const noexcept { return db_ != nullptr; }
    // Rows affected by the most recently completed INSERT/UPDATE/DELETE.
    [[nodiscard]] int changes() const noexcept;

private:
    explicit Database(sqlite3* db) noexcept : db_(db) {}

    sqlite3* db_ = nullptr;
};

// A prepared statement. Bind indices are 1-based and column indices are
// 0-based, matching the underlying sqlite3_bind_*/sqlite3_column_* indexing
// so callers can cross-reference the SQL text directly.
class Statement {
public:
    Statement() = default;
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    static Result<Statement> prepare(Database& db, const std::string& sql);

    [[nodiscard]] bool valid() const noexcept { return stmt_ != nullptr; }

    Status bind_null(int index);
    Status bind_int64(int index, int64_t value);
    // Stored as SQLite INTEGER (signed 64-bit); fails cleanly if value would
    // not round-trip (>= 2^63).
    Status bind_uint64(int index, uint64_t value);
    // Converted to UTF-8 before binding.
    Status bind_text(int index, const std::wstring& value);
    Status bind_text_utf8(int index, const std::string& value);
    Status bind_blob(int index, const std::vector<uint8_t>& value);

    // SQLITE_ROW -> true, SQLITE_DONE -> false, anything else -> Error.
    Result<bool> step();
    // Resets the statement and clears bindings so it can be re-executed.
    Status reset();

    [[nodiscard]] bool column_is_null(int index) const;
    [[nodiscard]] int64_t column_int64(int index) const;
    [[nodiscard]] uint64_t column_uint64(int index) const;
    [[nodiscard]] std::string column_text_utf8(int index) const;
    // Converted from the column's UTF-8 text to a native wide string.
    [[nodiscard]] std::wstring column_text_wide(int index) const;
    [[nodiscard]] std::vector<uint8_t> column_blob(int index) const;

private:
    Statement(sqlite3_stmt* stmt, sqlite3* db) noexcept : stmt_(stmt), db_(db) {}
    void reset_to_empty() noexcept;

    sqlite3_stmt* stmt_ = nullptr;
    sqlite3* db_ = nullptr;  // Non-owning; used only to format error messages.
};

// BEGIN IMMEDIATE / COMMIT wrapper. If the transaction is not committed
// before destruction, it is rolled back automatically.
class Transaction {
public:
    Transaction() = default;
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&& other) noexcept;

    static Result<Transaction> begin_immediate(Database& db);

    Status commit();

private:
    explicit Transaction(sqlite3* db) noexcept : db_(db) {}

    sqlite3* db_ = nullptr;
    bool active_ = false;
    bool committed_ = false;
};

}  // namespace wlm2pst::sqlite_util
