// Bully Studio - SQLite access layer.
//
// Project metadata (asset records, dependencies, build cache, plugin state,
// QA reports) lives in one SQLite database per project. This wrapper keeps
// call sites free of C-API bookkeeping while preserving:
//   * prepared statements (no string concatenation of user data),
//   * explicit transactions with RAII rollback,
//   * schema migrations driven by PRAGMA user_version,
//   * deterministic diagnostics (BULLY-DB-*) instead of exceptions.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/error.h"

namespace bully::project {

/// One result row: column name -> text value (NULL becomes "").
using Row = std::map<std::string, std::string>;
using Rows = std::vector<Row>;

struct SqlValue {
    enum class Kind { Null, Text, Integer, Real } kind = Kind::Null;
    std::string text;
    std::int64_t integer = 0;
    double real = 0.0;

    SqlValue() = default;
    SqlValue(std::string value) : kind(Kind::Text), text(std::move(value)) {}
    SqlValue(const char* value) : kind(Kind::Text), text(value) {}
    SqlValue(std::int64_t value) : kind(Kind::Integer), integer(value) {}
    SqlValue(int value) : kind(Kind::Integer), integer(value) {}
    SqlValue(double value) : kind(Kind::Real), real(value) {}
    SqlValue(bool value) : kind(Kind::Integer), integer(value ? 1 : 0) {}

    static SqlValue null() { return SqlValue{}; }
};

using SqlParams = std::vector<SqlValue>;

class Database {
public:
    Database();
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    /// Opens (and creates if needed) a database file.
    [[nodiscard]] static core::Result<Database> open(const std::filesystem::path& path);
    /// Opens an in-memory database, used by tests.
    [[nodiscard]] static core::Result<Database> open_memory();

    [[nodiscard]] bool is_open() const noexcept { return db_ != nullptr; }
    void close();

    [[nodiscard]] core::Status exec(std::string_view sql, const SqlParams& params = {});

    [[nodiscard]] core::Result<Rows> query(std::string_view sql, const SqlParams& params = {});

    /// Queries a single scalar integer (0 when no row).
    [[nodiscard]] std::int64_t query_int(std::string_view sql, const SqlParams& params = {},
                                         std::int64_t fallback = 0);

    [[nodiscard]] core::Status begin();
    [[nodiscard]] core::Status commit();
    [[nodiscard]] core::Status rollback();
    [[nodiscard]] bool in_transaction() const noexcept { return transaction_depth_ > 0; }
    /// RAII transaction: commits on destruction unless `rollback()` was called.
    class Transaction {
    public:
        explicit Transaction(Database& db);
        ~Transaction();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        void commit();
        void rollback();
        [[nodiscard]] bool active() const noexcept { return active_; }

    private:
        Database& db_;
        bool active_ = false;
    };

    [[nodiscard]] std::int64_t last_insert_rowid() const;
    [[nodiscard]] std::int64_t changes() const;

    [[nodiscard]] int user_version();
    [[nodiscard]] core::Status set_user_version(int version);

    /// Runs `migrations[version]` for every version above the current
    /// user_version, inside one transaction.
    [[nodiscard]] core::Status migrate(
        const std::vector<std::pair<int, std::string>>& migrations);

    [[nodiscard]] std::filesystem::path path() const { return path_; }

private:
    void* db_ = nullptr;
    std::filesystem::path path_;
    int transaction_depth_ = 0;
};

}  // namespace bully::project
