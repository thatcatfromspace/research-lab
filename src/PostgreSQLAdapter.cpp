#include "analyzer/PostgreSQLAdapter.hpp"
#include <libpq-fe.h>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <random>

namespace analyzer {

// ─── Bench parameters ─────────────────────────────────────────────────────────
static constexpr int  PG_SEED_ROWS  = 10'000;
static constexpr int  PG_BATCH_SIZE = 500;
static constexpr int  PG_READ_PCT   = 70;    // % of ops that are reads

// Named prepared statements (created once in setup_schema)
static constexpr const char* STMT_READ  = "bench_read";
static constexpr const char* STMT_WRITE = "bench_write";

// ─── Thread-local RNG ─────────────────────────────────────────────────────────
static thread_local std::mt19937                       tl_rng{std::random_device{}()};
static thread_local std::uniform_int_distribution<int>  key_dist{1, PG_SEED_ROWS};
static thread_local std::uniform_int_distribution<int>  pct_dist{1, 100};
static thread_local std::uniform_int_distribution<int>  val_dist{0, 999'999};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

PostgreSQLAdapter::PostgreSQLAdapter(const std::string& conninfo)
    : conninfo_(conninfo), conn_(nullptr) {}

PostgreSQLAdapter::~PostgreSQLAdapter() {
    disconnect();
}

// ─── Helper: check a PGresult, throw on failure ───────────────────────────────
static void check(PGresult* res, ExecStatusType expected, const char* ctx, PGconn* conn) {
    if (PQresultStatus(res) != expected) {
        std::string err = PQerrorMessage(conn);
        PQclear(res);
        throw std::runtime_error(std::string(ctx) + ": " + err);
    }
}

// ─── connect() ────────────────────────────────────────────────────────────────

void PostgreSQLAdapter::connect() {
    conn_ = PQconnectdb(conninfo_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("PQconnectdb failed: " + err);
    }

    setup_schema();
}

// ─── setup_schema() ───────────────────────────────────────────────────────────
// 1. Creates bench_kv table (if not exists)
// 2. Seeds PG_SEED_ROWS rows (if table is empty)
// 3. Prepares named statements for reads and writes
//    Using server-side prepared statements avoids per-call parse/plan overhead,
//    giving a cleaner picture of pure execution latency.

void PostgreSQLAdapter::setup_schema() {
    // 1. Create table
    PGresult* res = PQexec(conn_,
        "CREATE TABLE IF NOT EXISTS bench_kv ("
        "  id  INT         NOT NULL,"
        "  val VARCHAR(255) NOT NULL,"
        "  PRIMARY KEY (id)"
        ")");
    check(res, PGRES_COMMAND_OK, "CREATE TABLE bench_kv", conn_);
    PQclear(res);

    // 2. Count existing rows
    res = PQexec(conn_, "SELECT COUNT(*) FROM bench_kv");
    check(res, PGRES_TUPLES_OK, "COUNT bench_kv", conn_);
    long long cnt = std::stoll(PQgetvalue(res, 0, 0));
    PQclear(res);

    if (cnt >= PG_SEED_ROWS) {
        std::cout << "[PostgreSQL] bench_kv already has " << cnt
                  << " rows — skipping seed.\n";
    } else {
        std::cout << "[PostgreSQL] Seeding " << PG_SEED_ROWS
                  << " rows into bench_kv...\n";

        // Insert in batches using a VALUES list
        for (int base = 1; base <= PG_SEED_ROWS; base += PG_BATCH_SIZE) {
            std::ostringstream oss;
            oss << "INSERT INTO bench_kv (id, val) VALUES ";
            int end = std::min(base + PG_BATCH_SIZE - 1, PG_SEED_ROWS);
            for (int i = base; i <= end; ++i) {
                if (i > base) oss << ',';
                oss << '(' << i << ",'seed_" << i << "')";
            }
            oss << " ON CONFLICT (id) DO NOTHING";
            std::string sql = oss.str();
            res = PQexec(conn_, sql.c_str());
            check(res, PGRES_COMMAND_OK, "INSERT seed batch", conn_);
            PQclear(res);
        }
        std::cout << "[PostgreSQL] Seed complete.\n";
    }

    // 3. Prepare named statements
    //    STMT_READ  : SELECT val FROM bench_kv WHERE id = $1
    //    STMT_WRITE : UPDATE bench_kv SET val = $1 WHERE id = $2
    res = PQprepare(conn_, STMT_READ,
                    "SELECT val FROM bench_kv WHERE id = $1",
                    1, nullptr);
    check(res, PGRES_COMMAND_OK, "PQprepare bench_read", conn_);
    PQclear(res);

    res = PQprepare(conn_, STMT_WRITE,
                    "UPDATE bench_kv SET val = $1 WHERE id = $2",
                    2, nullptr);
    check(res, PGRES_COMMAND_OK, "PQprepare bench_write", conn_);
    PQclear(res);
}

// ─── perform_op() ─────────────────────────────────────────────────────────────
// Uses server-side prepared statements for both read and write paths.
// 70 % point-reads  : SELECT val FROM bench_kv WHERE id = $1
// 30 % point-writes : UPDATE bench_kv SET val = $1 WHERE id = $2

void PostgreSQLAdapter::perform_op() {
    if (!conn_) return;

    int  key     = key_dist(tl_rng);
    bool is_read = (pct_dist(tl_rng) <= PG_READ_PCT);

    char key_buf[24];
    snprintf(key_buf, sizeof(key_buf), "%d", key);

    PGresult* res = nullptr;

    if (is_read) {
        const char* params[1] = { key_buf };
        res = PQexecPrepared(conn_, STMT_READ,
                             1, params, nullptr, nullptr, 0);
        // PGRES_TUPLES_OK on hit, PGRES_TUPLES_OK with 0 rows on miss — both fine
    } else {
        char val_buf[32];
        snprintf(val_buf, sizeof(val_buf), "upd_%d", val_dist(tl_rng));
        const char* params[2] = { val_buf, key_buf };
        res = PQexecPrepared(conn_, STMT_WRITE,
                             2, params, nullptr, nullptr, 0);
    }

    if (res) PQclear(res);
}

// ─── collect_metrics() ────────────────────────────────────────────────────────
// Collects a curated set of stats from pg_stat_database for the current DB
// and pg_stat_bgwriter for buffer/IO insight.

MetricMap PostgreSQLAdapter::collect_metrics() {
    MetricMap metrics;
    if (!conn_) return metrics;

    // Per-database statistics
    const char* db_query =
        "SELECT xact_commit, xact_rollback, blks_read, blks_hit, "
        "       tup_returned, tup_fetched, tup_inserted, tup_updated "
        "FROM pg_stat_database "
        "WHERE datname = current_database()";

    PGresult* res = PQexec(conn_, db_query);
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        auto get = [&](int col) -> MetricValue {
            const char* v = PQgetvalue(res, 0, col);
            try { return std::stoll(v); } catch (...) { return nullptr; }
        };
        metrics["postgres.xact_commit"]   = get(0);
        metrics["postgres.xact_rollback"] = get(1);
        metrics["postgres.blks_read"]     = get(2);
        metrics["postgres.blks_hit"]      = get(3);
        metrics["postgres.tup_returned"]  = get(4);
        metrics["postgres.tup_fetched"]   = get(5);
        metrics["postgres.tup_inserted"]  = get(6);
        metrics["postgres.tup_updated"]   = get(7);
    }
    PQclear(res);

    // Buffer manager statistics
    const char* bgw_query =
        "SELECT buffers_clean, buffers_checkpoint, buffers_backend "
        "FROM pg_stat_bgwriter";
    res = PQexec(conn_, bgw_query);
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        auto get = [&](int col) -> MetricValue {
            const char* v = PQgetvalue(res, 0, col);
            try { return std::stoll(v); } catch (...) { return nullptr; }
        };
        metrics["postgres.buffers_clean"]      = get(0);
        metrics["postgres.buffers_checkpoint"] = get(1);
        metrics["postgres.buffers_backend"]    = get(2);
    }
    PQclear(res);

    return metrics;
}

// ─── disconnect() ─────────────────────────────────────────────────────────────

void PostgreSQLAdapter::disconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

} // namespace analyzer
