#include "analyzer/MySQLAdapter.hpp"
#include <mysql/mysql.h>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <random>
#include <set>

namespace analyzer {

// ─── Bench parameters ─────────────────────────────────────────────────────────
static constexpr int  SEED_ROWS  = 10'000;   // rows pre-populated in bench_kv
static constexpr int  BATCH_SIZE = 500;      // rows per INSERT batch
static constexpr int  READ_PCT   = 70;       // % of ops that are reads

// ─── Thread-local RNG (safe for multi-threaded use without locking) ───────────
static thread_local std::mt19937                      tl_rng{std::random_device{}()};
static thread_local std::uniform_int_distribution<int> key_dist{1, SEED_ROWS};
static thread_local std::uniform_int_distribution<int> pct_dist{1, 100};
static thread_local std::uniform_int_distribution<int> val_dist{0, 999'999};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

MySQLAdapter::MySQLAdapter(const std::string& host, const std::string& user,
                           const std::string& password, const std::string& dbname,
                           int port)
    : host_(host), user_(user), password_(password),
      dbname_(dbname), port_(port), conn_(nullptr) {}

MySQLAdapter::~MySQLAdapter() {
    disconnect();
}

// ─── connect() ────────────────────────────────────────────────────────────────

void MySQLAdapter::connect() {
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        throw std::runtime_error("mysql_init failed: out of memory");
    }

    // Set explicit timeouts so we fail fast if the container isn't ready
    unsigned int timeout = 10;
    mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(conn_, MYSQL_OPT_READ_TIMEOUT,    &timeout);
    mysql_options(conn_, MYSQL_OPT_WRITE_TIMEOUT,   &timeout);

    if (!mysql_real_connect(conn_,
                            host_.c_str(), user_.c_str(), password_.c_str(),
                            dbname_.c_str(), port_, nullptr, 0)) {
        std::string err = mysql_error(conn_);
        mysql_close(conn_);
        conn_ = nullptr;
        throw std::runtime_error("mysql_real_connect failed: " + err);
    }

    setup_schema();
}

// ─── setup_schema() ───────────────────────────────────────────────────────────
// Creates the bench_kv table and seeds it with SEED_ROWS rows if it is empty.
// This is called once per connect(), before the timed workload begins.

void MySQLAdapter::setup_schema() {
    // 1. Create table
    const char* create_sql =
        "CREATE TABLE IF NOT EXISTS bench_kv ("
        "  id  INT          NOT NULL,"
        "  val VARCHAR(255) NOT NULL,"
        "  PRIMARY KEY (id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(conn_, create_sql)) {
        throw std::runtime_error(
            std::string("CREATE TABLE bench_kv failed: ") + mysql_error(conn_));
    }

    // 2. Check existing row count
    if (mysql_query(conn_, "SELECT COUNT(*) FROM bench_kv")) {
        throw std::runtime_error(
            std::string("COUNT(*) failed: ") + mysql_error(conn_));
    }
    MYSQL_RES* res = mysql_store_result(conn_);
    MYSQL_ROW  row = mysql_fetch_row(res);
    long long  cnt = row ? std::stoll(row[0]) : 0;
    mysql_free_result(res);

    if (cnt >= SEED_ROWS) {
        std::cout << "[MySQL] bench_kv already has " << cnt
                  << " rows — skipping seed.\n";
        return;
    }

    // 3. Seed rows in batches
    std::cout << "[MySQL] Seeding " << SEED_ROWS << " rows into bench_kv...\n";

    for (int base = 1; base <= SEED_ROWS; base += BATCH_SIZE) {
        std::ostringstream oss;
        oss << "INSERT IGNORE INTO bench_kv (id, val) VALUES ";
        int end = std::min(base + BATCH_SIZE - 1, SEED_ROWS);
        for (int i = base; i <= end; ++i) {
            if (i > base) oss << ',';
            oss << '(' << i << ",'seed_" << i << "')";
        }
        std::string sql = oss.str();
        if (mysql_real_query(conn_, sql.c_str(),
                             static_cast<unsigned long>(sql.size()))) {
            throw std::runtime_error(
                std::string("INSERT seed failed: ") + mysql_error(conn_));
        }
    }

    std::cout << "[MySQL] Seed complete.\n";
}

// ─── perform_op() ─────────────────────────────────────────────────────────────
// 70 % point-reads  : SELECT val FROM bench_kv WHERE id = <rand>
// 30 % point-writes : UPDATE bench_kv SET val = <rand_str> WHERE id = <rand>

void MySQLAdapter::perform_op() {
    if (!conn_) return;

    int  key = key_dist(tl_rng);
    bool is_read = (pct_dist(tl_rng) <= READ_PCT);

    char buf[192];
    int  len;

    if (is_read) {
        len = snprintf(buf, sizeof(buf),
                       "SELECT val FROM bench_kv WHERE id = %d", key);
        if (mysql_real_query(conn_, buf, static_cast<unsigned long>(len)) == 0) {
            MYSQL_RES* res = mysql_store_result(conn_);
            if (res) mysql_free_result(res);
        }
    } else {
        len = snprintf(buf, sizeof(buf),
                       "UPDATE bench_kv SET val = 'upd_%d' WHERE id = %d",
                       val_dist(tl_rng), key);
        mysql_real_query(conn_, buf, static_cast<unsigned long>(len));
    }
}

// ─── collect_metrics() ────────────────────────────────────────────────────────
// Pulls a curated subset from SHOW GLOBAL STATUS after the workload finishes.

MetricMap MySQLAdapter::collect_metrics() {
    MetricMap metrics;
    if (!conn_) return metrics;

    // 1. Collect Global Status metrics
    if (mysql_query(conn_, "SHOW GLOBAL STATUS") == 0) {
        MYSQL_RES* res = mysql_store_result(conn_);
        if (res) {
            static const std::set<std::string> wanted = {
                "Threads_connected",
                "Questions",
                "Slow_queries",
                "Com_select",
                "Com_update",
                "Innodb_buffer_pool_read_requests",
                "Innodb_buffer_pool_reads",
                "Innodb_rows_read",
                "Innodb_rows_updated",
                // Amplification metrics:
                "Innodb_os_log_written",      // Bytes written to redo log
                "Innodb_data_written",        // Bytes written to data files
                "Innodb_dblwr_pages_written"  // Doublewrite buffer pages written
            };

            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                if (!row[0] || !row[1]) continue;
                std::string key = row[0];
                if (wanted.count(key)) {
                    try {
                        metrics["mysql." + key] = std::stoll(row[1]);
                    } catch (...) {
                        metrics["mysql." + key] = nullptr;
                    }
                }
            }
            mysql_free_result(res);
        }
    }

    // 2. Collect Table Size metrics (Space Amplification footprint)
    const char* size_query = 
        "SELECT data_length, index_length "
        "FROM information_schema.tables "
        "WHERE table_schema = 'bench' AND table_name = 'bench_kv'";
        
    if (mysql_query(conn_, size_query) == 0) {
        MYSQL_RES* res = mysql_store_result(conn_);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0] && row[1]) {
                try {
                    metrics["mysql.table_data_bytes"]  = std::stoll(row[0]);
                    metrics["mysql.table_index_bytes"] = std::stoll(row[1]);
                } catch (...) {
                    // Ignore on parse failure
                }
            }
            mysql_free_result(res);
        }
    }

    return metrics;
}

// ─── disconnect() ─────────────────────────────────────────────────────────────

void MySQLAdapter::disconnect() {
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

} // namespace analyzer
