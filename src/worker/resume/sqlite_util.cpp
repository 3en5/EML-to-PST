#include "worker/resume/sqlite_util.h"

#include <sqlite3.h>

#include <limits>
#include <utility>

namespace wlm2pst::sqlite_util {

namespace {

// File-local UTF-8 <-> wide string conversion. Deliberately independent of
// common/unicode (implemented in parallel by another module) so this module
// has no ordering dependency on it. Surrogate-correct for both wchar_t
// widths: UTF-16 on Windows, UTF-32 on Linux test builds.
namespace detail {

void append_utf8(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string wide_to_utf8(const std::wstring& wide) {
    std::string out;
    out.reserve(wide.size());
    size_t i = 0;
    while (i < wide.size()) {
        char32_t cp;
        if constexpr (sizeof(wchar_t) == 2) {
            uint32_t unit = static_cast<uint16_t>(wide[i]);
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                if (i + 1 < wide.size()) {
                    uint32_t next = static_cast<uint16_t>(wide[i + 1]);
                    if (next >= 0xDC00 && next <= 0xDFFF) {
                        cp = 0x10000u + ((unit - 0xD800u) << 10) + (next - 0xDC00u);
                        i += 2;
                    } else {
                        cp = 0xFFFD;  // unpaired high surrogate
                        i += 1;
                    }
                } else {
                    cp = 0xFFFD;  // dangling high surrogate at end of string
                    i += 1;
                }
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                cp = 0xFFFD;  // unpaired low surrogate
                i += 1;
            } else {
                cp = unit;
                i += 1;
            }
        } else {
            cp = static_cast<char32_t>(static_cast<uint32_t>(wide[i]));
            i += 1;
        }
        append_utf8(out, cp);
    }
    return out;
}

std::wstring utf8_to_wide(const std::string& utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        unsigned char c0 = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0xFFFD;
        size_t len = 1;
        if (c0 < 0x80) {
            cp = c0;
            len = 1;
        } else if ((c0 & 0xE0) == 0xC0) {
            cp = c0 & 0x1F;
            len = 2;
        } else if ((c0 & 0xF0) == 0xE0) {
            cp = c0 & 0x0F;
            len = 3;
        } else if ((c0 & 0xF8) == 0xF0) {
            cp = c0 & 0x07;
            len = 4;
        } else {
            cp = 0xFFFD;
            len = 1;
        }

        if (len > 1) {
            if (i + len > n) {
                cp = 0xFFFD;
                len = 1;
            } else {
                bool valid = true;
                char32_t accum = cp;
                for (size_t k = 1; k < len; ++k) {
                    unsigned char c = static_cast<unsigned char>(utf8[i + k]);
                    if ((c & 0xC0) != 0x80) {
                        valid = false;
                        break;
                    }
                    accum = (accum << 6) | (c & 0x3F);
                }
                if (valid) {
                    cp = accum;
                } else {
                    cp = 0xFFFD;
                    len = 1;
                }
            }
        }

        i += len;

        if constexpr (sizeof(wchar_t) == 2) {
            if (cp > 0xFFFF) {
                char32_t v = cp - 0x10000u;
                out.push_back(static_cast<wchar_t>(0xD800u + (v >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00u + (v & 0x3FFu)));
            } else {
                out.push_back(static_cast<wchar_t>(cp));
            }
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

}  // namespace detail

Error make_sqlite_error(sqlite3* db, const std::string& operation, int fallback_code = SQLITE_ERROR) {
    if (db != nullptr) {
        int code = sqlite3_extended_errcode(db);
        std::string msg = sqlite3_errmsg(db);
        return make_hresult_error(code, operation, msg);
    }
    return make_hresult_error(fallback_code, operation, "sqlite error (no connection handle)");
}

}  // namespace

// ---------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------

Database::~Database() {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

Database::Database(Database&& other) noexcept : db_(other.db_) {
    other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (db_ != nullptr) {
            sqlite3_close_v2(db_);
        }
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

Result<Database> Database::open(const std::wstring& db_path) {
    const std::string utf8_path = detail::wide_to_utf8(db_path);

    sqlite3* raw = nullptr;
    int rc = sqlite3_open_v2(utf8_path.c_str(), &raw,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        Error err = make_sqlite_error(raw, "sqlite3_open_v2", rc);
        if (raw != nullptr) {
            sqlite3_close_v2(raw);
        }
        return err;
    }

    Database db(raw);
    sqlite3_busy_timeout(raw, 5000);

    Status s = db.exec("PRAGMA journal_mode=WAL;");
    if (!s) return s.error();
    s = db.exec("PRAGMA synchronous=NORMAL;");
    if (!s) return s.error();
    s = db.exec("PRAGMA foreign_keys=ON;");
    if (!s) return s.error();

    return db;
}

int Database::changes() const noexcept {
    return db_ != nullptr ? sqlite3_changes(db_) : 0;
}

Status Database::exec(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string msg = err_msg != nullptr ? err_msg : sqlite3_errmsg(db_);
        if (err_msg != nullptr) {
            sqlite3_free(err_msg);
        }
        int code = sqlite3_extended_errcode(db_);
        return make_hresult_error(code, "sqlite3_exec", msg);
    }
    return Status::success();
}

// ---------------------------------------------------------------------
// Statement
// ---------------------------------------------------------------------

Statement::~Statement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

Statement::Statement(Statement&& other) noexcept : stmt_(other.stmt_), db_(other.db_) {
    other.stmt_ = nullptr;
    other.db_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        stmt_ = other.stmt_;
        db_ = other.db_;
        other.stmt_ = nullptr;
        other.db_ = nullptr;
    }
    return *this;
}

Result<Statement> Statement::prepare(Database& db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db.handle(), sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return make_sqlite_error(db.handle(), "sqlite3_prepare_v2", rc);
    }
    return Statement(stmt, db.handle());
}

Status Statement::bind_null(int index) {
    int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) return make_sqlite_error(db_, "sqlite3_bind_null");
    return Status::success();
}

Status Statement::bind_int64(int index, int64_t value) {
    int rc = sqlite3_bind_int64(stmt_, index, static_cast<sqlite3_int64>(value));
    if (rc != SQLITE_OK) return make_sqlite_error(db_, "sqlite3_bind_int64");
    return Status::success();
}

Status Statement::bind_uint64(int index, uint64_t value) {
    if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return make_error("Statement::bind_uint64",
                           "value exceeds INT64_MAX and cannot be stored as a SQLite INTEGER");
    }
    return bind_int64(index, static_cast<int64_t>(value));
}

Status Statement::bind_text(int index, const std::wstring& value) {
    return bind_text_utf8(index, detail::wide_to_utf8(value));
}

Status Statement::bind_text_utf8(int index, const std::string& value) {
    int rc = sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()),
                                SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) return make_sqlite_error(db_, "sqlite3_bind_text");
    return Status::success();
}

Status Statement::bind_blob(int index, const std::vector<uint8_t>& value) {
    int rc;
    if (value.empty()) {
        // sqlite3_bind_blob with a null pointer and n=0 binds a zero-length
        // blob (distinct from NULL), which is what an empty vector means.
        rc = sqlite3_bind_blob(stmt_, index, "", 0, SQLITE_TRANSIENT);
    } else {
        rc = sqlite3_bind_blob(stmt_, index, value.data(), static_cast<int>(value.size()),
                                SQLITE_TRANSIENT);
    }
    if (rc != SQLITE_OK) return make_sqlite_error(db_, "sqlite3_bind_blob");
    return Status::success();
}

Result<bool> Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    return make_sqlite_error(db_, "sqlite3_step", rc);
}

Status Statement::reset() {
    int rc = sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    if (rc != SQLITE_OK) return make_sqlite_error(db_, "sqlite3_reset", rc);
    return Status::success();
}

bool Statement::column_is_null(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

int64_t Statement::column_int64(int index) const {
    return static_cast<int64_t>(sqlite3_column_int64(stmt_, index));
}

uint64_t Statement::column_uint64(int index) const {
    return static_cast<uint64_t>(sqlite3_column_int64(stmt_, index));
}

std::string Statement::column_text_utf8(int index) const {
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    int bytes = sqlite3_column_bytes(stmt_, index);
    if (text == nullptr || bytes <= 0) return {};
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(bytes));
}

std::wstring Statement::column_text_wide(int index) const {
    return detail::utf8_to_wide(column_text_utf8(index));
}

std::vector<uint8_t> Statement::column_blob(int index) const {
    const void* blob = sqlite3_column_blob(stmt_, index);
    int bytes = sqlite3_column_bytes(stmt_, index);
    if (blob == nullptr || bytes <= 0) return {};
    const auto* begin = static_cast<const uint8_t*>(blob);
    return std::vector<uint8_t>(begin, begin + bytes);
}

// ---------------------------------------------------------------------
// Transaction
// ---------------------------------------------------------------------

Transaction::~Transaction() {
    if (active_ && !committed_ && db_ != nullptr) {
        // Best-effort rollback; there is nothing actionable to do with a
        // failure here (we are typically already unwinding due to an error).
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
}

Transaction::Transaction(Transaction&& other) noexcept
    : db_(other.db_), active_(other.active_), committed_(other.committed_) {
    other.db_ = nullptr;
    other.active_ = false;
    other.committed_ = false;
}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if (this != &other) {
        if (active_ && !committed_ && db_ != nullptr) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
        db_ = other.db_;
        active_ = other.active_;
        committed_ = other.committed_;
        other.db_ = nullptr;
        other.active_ = false;
        other.committed_ = false;
    }
    return *this;
}

Result<Transaction> Transaction::begin_immediate(Database& db) {
    Status s = db.exec("BEGIN IMMEDIATE;");
    if (!s) return s.error();
    Transaction txn(db.handle());
    txn.active_ = true;
    return txn;
}

Status Transaction::commit() {
    if (!active_ || db_ == nullptr) {
        return make_error("Transaction::commit", "transaction is not active");
    }
    int rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        return make_sqlite_error(db_, "COMMIT", rc);
    }
    committed_ = true;
    active_ = false;
    return Status::success();
}

}  // namespace wlm2pst::sqlite_util
