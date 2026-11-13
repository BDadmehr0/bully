#include "bully/project/database.h"

#include <cstdint>

#include <sqlite3.h>

namespace bully::project {

namespace {

core::DiagnosticList db_error(sqlite3* db, std::string code, std::string_view sql)
{
    core::DiagnosticList diagnostics;
    const char* message = db != nullptr ? sqlite3_errmsg(db) : "database is not open";
    diagnostics.error(std::move(code),
                      std::string(message) + " while executing: " +
                          std::string(sql.substr(0, 160)));
    return diagnostics;
}

/// SQLITE_TRANSIENT with a C++-style cast (the macro uses an old-style cast).
sqlite3_destructor_type transient_destructor()
{
    return reinterpret_cast<sqlite3_destructor_type>(static_cast<std::intptr_t>(-1));
}

void bind_params(sqlite3_stmt* statement, const SqlParams& params)
{
    for (std::size_t i = 0; i < params.size(); ++i) {
        const int index = static_cast<int>(i) + 1;
        const SqlValue& value = params[i];
        switch (value.kind) {
            case SqlValue::Kind::Null:
                sqlite3_bind_null(statement, index);
                break;
            case SqlValue::Kind::Integer:
                sqlite3_bind_int64(statement, index, value.integer);
                break;
            case SqlValue::Kind::Real:
                sqlite3_bind_double(statement, index, value.real);
                break;
            case SqlValue::Kind::Text:
                sqlite3_bind_text(statement, index, value.text.c_str(),
                                  static_cast<int>(value.text.size()), transient_destructor());
                break;
        }
    }
}

std::string column_text(sqlite3_stmt* statement, int index)
{
    const unsigned char* text = sqlite3_column_text(statement, index);
    if (text == nullptr) return {};
    const int bytes = sqlite3_column_bytes(statement, index);
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(bytes));
}

}  // namespace

Database::Database() = default;

Database::~Database() { close(); }

Database::Database(Database&& other) noexcept
    : db_(other.db_), path_(std::move(other.path_)), transaction_depth_(other.transaction_depth_)
{
    other.db_ = nullptr;
    other.transaction_depth_ = 0;
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other) {
        close();
        db_ = other.db_;
        path_ = std::move(other.path_);
        transaction_depth_ = other.transaction_depth_;
        other.db_ = nullptr;
        other.transaction_depth_ = 0;
    }
    return *this;
}

core::Result<Database> Database::open(const std::filesystem::path& path)
{
    core::DiagnosticList diagnostics;
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    sqlite3* handle = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const int result = sqlite3_open_v2(path.string().c_str(), &handle, flags, nullptr);
    if (result != SQLITE_OK) {
        diagnostics.error("BULLY-DB-0001",
                          "cannot open project database '" + path.string() + "': " +
                              (handle != nullptr ? sqlite3_errmsg(handle) : "unknown error"),
                          path.string());
        if (handle != nullptr) sqlite3_close(handle);
        return core::Result<Database>(std::move(diagnostics));
    }
    Database database;
    database.db_ = handle;
    database.path_ = path;
    // WAL keeps the Studio UI responsive while background builds write.
    (void)database.exec("PRAGMA journal_mode=WAL;");
    (void)database.exec("PRAGMA foreign_keys=ON;");
    (void)database.exec("PRAGMA synchronous=NORMAL;");
    (void)database.exec("PRAGMA busy_timeout=5000;");
    return database;
}

core::Result<Database> Database::open_memory()
{
    core::DiagnosticList diagnostics;
    sqlite3* handle = nullptr;
    const int result = sqlite3_open(":memory:", &handle);
    if (result != SQLITE_OK) {
        diagnostics.error("BULLY-DB-0002", "cannot open in-memory database");
        if (handle != nullptr) sqlite3_close(handle);
        return core::Result<Database>(std::move(diagnostics));
    }
    Database database;
    database.db_ = handle;
    (void)database.exec("PRAGMA foreign_keys=ON;");
    return database;
}

void Database::close()
{
    if (db_ != nullptr) {
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
        transaction_depth_ = 0;
    }
}

core::Status Database::exec(std::string_view sql, const SqlParams& params)
{
    auto* db = static_cast<sqlite3*>(db_);
    if (db == nullptr) {
        core::DiagnosticList diagnostics;
        diagnostics.error("BULLY-DB-0003", "database is not open");
        return core::Status(std::move(diagnostics));
    }
    // Multi-statement support: schema migrations are written as scripted SQL,
    // and sqlite3_prepare_v2 only compiles the first statement, so walk the
    // tail pointer until the script is consumed. Parameters bind to the first
    // statement only, which is how every call site uses this overload.
    std::string script(sql);
    const char* cursor = script.c_str();
    bool first = true;
    while (cursor != nullptr && *cursor != '\0') {
        sqlite3_stmt* statement = nullptr;
        const char* tail = nullptr;
        if (sqlite3_prepare_v2(db, cursor, -1, &statement, &tail) != SQLITE_OK) {
            return core::Status(db_error(db, "BULLY-DB-0004", sql));
        }
        if (statement == nullptr) {
            // Trailing whitespace or a pure comment.
            if (tail == nullptr || tail == cursor) break;
            cursor = tail;
            continue;
        }
        if (first) {
            bind_params(statement, params);
            first = false;
        }
        const int result = sqlite3_step(statement);
        sqlite3_finalize(statement);
        if (result != SQLITE_DONE && result != SQLITE_ROW) {
            return core::Status(db_error(db, "BULLY-DB-0005", sql));
        }
        if (tail == nullptr || tail == cursor) break;
        cursor = tail;
    }
    return core::Status{};
}

core::Result<Rows> Database::query(std::string_view sql, const SqlParams& params)
{
    core::DiagnosticList diagnostics;
    auto* db = static_cast<sqlite3*>(db_);
    if (db == nullptr) {
        diagnostics.error("BULLY-DB-0003", "database is not open");
        return core::Result<Rows>(std::move(diagnostics));
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, std::string(sql).c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        return core::Result<Rows>(db_error(db, "BULLY-DB-0006", sql));
    }
    bind_params(statement, params);
    Rows rows;
    int more = 0;
    while ((more = sqlite3_step(statement)) == SQLITE_ROW) {
        Row row;
        const int columns = sqlite3_column_count(statement);
        for (int i = 0; i < columns; ++i) {
            const char* name = sqlite3_column_name(statement, i);
            row[name != nullptr ? name : ""] = column_text(statement, i);
        }
        rows.push_back(std::move(row));
    }
    if (more != SQLITE_DONE) {
        sqlite3_finalize(statement);
        return core::Result<Rows>(db_error(db, "BULLY-DB-0007", sql));
    }
    sqlite3_finalize(statement);
    return rows;
}

std::int64_t Database::query_int(std::string_view sql, const SqlParams& params,
                                 std::int64_t fallback)
{
    auto* db = static_cast<sqlite3*>(db_);
    if (db == nullptr) return fallback;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, std::string(sql).c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        return fallback;
    }
    bind_params(statement, params);
    std::int64_t value = fallback;
    if (sqlite3_step(statement) == SQLITE_ROW) value = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

core::Status Database::begin()
{
    auto status = exec("BEGIN;");
    if (status) ++transaction_depth_;
    return status;
}

core::Status Database::commit()
{
    auto status = exec("COMMIT;");
    if (status && transaction_depth_ > 0) --transaction_depth_;
    return status;
}

core::Status Database::rollback()
{
    auto status = exec("ROLLBACK;");
    if (transaction_depth_ > 0) --transaction_depth_;
    return status;
}

Database::Transaction::Transaction(Database& db) : db_(db)
{
    active_ = static_cast<bool>(db_.begin());
}

Database::Transaction::~Transaction()
{
    if (active_) (void)db_.rollback();
}

void Database::Transaction::commit()
{
    if (!active_) return;
    if (db_.commit()) active_ = false;
}

void Database::Transaction::rollback()
{
    if (!active_) return;
    (void)db_.rollback();
    active_ = false;
}

std::int64_t Database::last_insert_rowid() const
{
    auto* db = static_cast<sqlite3*>(db_);
    return db != nullptr ? sqlite3_last_insert_rowid(db) : 0;
}

std::int64_t Database::changes() const
{
    auto* db = static_cast<sqlite3*>(db_);
    return db != nullptr ? sqlite3_changes(db) : 0;
}

int Database::user_version()
{
    return static_cast<int>(query_int("PRAGMA user_version;", {}, 0));
}

core::Status Database::set_user_version(int version)
{
    return exec("PRAGMA user_version=" + std::to_string(version) + ";");
}

core::Status Database::migrate(const std::vector<std::pair<int, std::string>>& migrations)
{
    core::DiagnosticList diagnostics;
    const int current = user_version();
    Transaction transaction(*this);
    for (const auto& [version, sql] : migrations) {
        if (version <= current) continue;
        (void)0;
        auto status = exec(sql);
        if (!status) {
            transaction.rollback();
            diagnostics.append(status.diagnostics());
            diagnostics.error("BULLY-DB-0008",
                              "migration to version " + std::to_string(version) + " failed");
            return core::Status(std::move(diagnostics));
        }
        auto version_status = set_user_version(version);
        if (!version_status) {
            transaction.rollback();
            return version_status;
        }
    }
    transaction.commit();
    return core::Status{};
}

}  // namespace bully::project
