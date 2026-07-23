#include "analyzer/MySQLAdapter.hpp" 

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <random>
#include <set>

namespace analyzer {

// ─── Bench parameters (defaults) ──────────────────────────────────────────────
static constexpr int  DEFAULT_BATCH_SIZE = 500;

// ─── Thread-local RNG (safe for multi-threaded use without locking) ───────────
static thread_local std::mt19937 tl_rng{std::random_device{}()};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

MySQLAdapter::MySQLAdapter(const std::string& host, const std::string& user,
                           const std::string& password, const std::string& dbname,
                           int port)
    : host_(host), user_(user), password_(password), dbname_(dbname), port_(port), conn_(nullptr) {
}

MySQLAdapter::~MySQLAdapter() {
    disconnect();
}

std::unique_ptr<DBAdapter> MySQLAdapter::clone_connection() {
    auto clone = std::make_unique<MySQLAdapter>(host_, user_, password_, dbname_, port_);
    clone->connect();
    clone->configure(read_pct_, seed_rows_);
    return clone;
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

// ─── configure() ───────────────────────────────────────────────────────────────

void MySQLAdapter::configure(int read_pct, int row_count) {
    read_pct_ = read_pct;
    seed_rows_ = row_count;
}

// ─── setup_schema() ───────────────────────────────────────────────────────────
// Creates the bench_kv table and seeds it with seed_rows_ rows if it is empty.

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

    if (cnt >= seed_rows_) {
        std::cout << "[MySQL] bench_kv already has " << cnt
                  << " rows — skipping seed.\n";
        return;
    }

    // 3. Seed rows in batches
    std::cout << "[MySQL] Seeding " << seed_rows_ << " rows into bench_kv...\n";

    for (int base = 1; base <= seed_rows_; base += DEFAULT_BATCH_SIZE) {
        std::ostringstream oss;
        oss << "INSERT IGNORE INTO bench_kv (id, val) VALUES ";
        int end = std::min(base + DEFAULT_BATCH_SIZE - 1, seed_rows_);
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

void MySQLAdapter::perform_read(int key) {
    if (!conn_) return;
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "SELECT val FROM bench_kv WHERE id = %d", key);
    if (mysql_real_query(conn_, buf, static_cast<unsigned long>(len)) == 0) {
        MYSQL_RES* res = mysql_store_result(conn_);
        if (res) mysql_free_result(res);
    }
}

void MySQLAdapter::perform_write(int key, const std::string& value) {
    if (!conn_) return;
    // We do REPLACE INTO to handle both insert and update
    std::string esc_val(value.length() * 2 + 1, '\0');
    mysql_real_escape_string(conn_, esc_val.data(), value.c_str(), value.length());
    
    std::ostringstream oss;
    oss << "REPLACE INTO bench_kv (id, val) VALUES (" << key << ", '" << esc_val.c_str() << "')";
    std::string query = oss.str();
    mysql_real_query(conn_, query.c_str(), query.length());
}

void MySQLAdapter::perform_scan(int start_key, int count) {
    if (!conn_) return;
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "SELECT val FROM bench_kv WHERE id >= %d ORDER BY id ASC LIMIT %d", start_key, count);
    if (mysql_real_query(conn_, buf, static_cast<unsigned long>(len)) == 0) {
        MYSQL_RES* res = mysql_store_result(conn_);
        if (res) mysql_free_result(res);
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
