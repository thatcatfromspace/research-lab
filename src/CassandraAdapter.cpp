#include "analyzer/CassandraAdapter.hpp"
#include <cassandra.h>
#include <stdexcept>
#include <iostream>
#include <random>
#include <vector>

namespace analyzer {

// ─── Thread-local RNG ─────────────────────────────────────────────────────────
static thread_local std::mt19937 tl_rng{std::random_device{}()};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

CassandraAdapter::CassandraAdapter(const std::string& contact_points)
    : contact_points_(contact_points), 
      cluster_(nullptr), 
      session_(nullptr),
      prepared_read_(nullptr),
      prepared_write_(nullptr) {}

CassandraAdapter::~CassandraAdapter() {
    disconnect();
}

static void check_future(CassFuture* future, const std::string& msg) {
    if (cass_future_error_code(future) != CASS_OK) {
        const char* message;
        size_t message_length;
        cass_future_error_message(future, &message, &message_length);
        std::string err(message, message_length);
        cass_future_free(future);
        throw std::runtime_error(msg + ": " + err);
    }
}

void CassandraAdapter::connect() {
    cluster_ = cass_cluster_new();
    session_ = cass_session_new();
    
    cass_cluster_set_contact_points(cluster_, contact_points_.c_str());
    cass_cluster_set_num_threads_io(cluster_, 2); // Local benchmark doesn't need many
    
    CassFuture* connect_future = cass_session_connect(session_, cluster_);
    check_future(connect_future, "Cassandra connect failed");
    cass_future_free(connect_future);
    
    setup_schema();
}

void CassandraAdapter::configure(int read_pct, int row_count) {
    read_pct_ = read_pct;
    seed_rows_ = row_count;
}

void CassandraAdapter::setup_schema() {
    auto run_query = [&](const char* query) {
        CassStatement* statement = cass_statement_new(query, 0);
        CassFuture* future = cass_session_execute(session_, statement);
        cass_future_wait(future);
        check_future(future, std::string("Query failed: ") + query);
        cass_future_free(future);
        cass_statement_free(statement);
    };

    // 1. Keyspace
    run_query("CREATE KEYSPACE IF NOT EXISTS bench WITH replication = "
              "{'class': 'SimpleStrategy', 'replication_factor': 1}");

    // 2. Table
    run_query("CREATE TABLE IF NOT EXISTS bench.bench_kv ("
              "  id INT PRIMARY KEY,"
              "  val TEXT"
              ")");

    // 3. Seed checking
    const char* count_query = "SELECT count(*) FROM bench.bench_kv";
    CassStatement* count_stmt = cass_statement_new(count_query, 0);
    CassFuture* count_future = cass_session_execute(session_, count_stmt);
    cass_future_wait(count_future);
    check_future(count_future, "Count query failed");
    
    const CassResult* result = cass_future_get_result(count_future);
    const CassRow* row = cass_result_first_row(result);
    cass_int64_t count;
    cass_value_get_int64(cass_row_get_column(row, 0), &count);
    
    cass_result_free(result);
    cass_future_free(count_future);
    cass_statement_free(count_stmt);

    if (count < seed_rows_) {
        std::cout << "[Cassandra] Seeding " << seed_rows_ << " rows...\n";
        const char* insert_query = "INSERT INTO bench.bench_kv (id, val) VALUES (?, ?)";
        CassStatement* insert_stmt = cass_statement_new(insert_query, 2);
        
        for (int i = 1; i <= seed_rows_; ++i) {
            std::string v = "seed_" + std::to_string(i);
            cass_statement_bind_int32(insert_stmt, 0, i);
            cass_statement_bind_string(insert_stmt, 1, v.c_str());
            
            CassFuture* fut = cass_session_execute(session_, insert_stmt);
            cass_future_wait(fut);
            check_future(fut, "Insert failed during seeding");
            cass_future_free(fut);
            
            cass_statement_reset_parameters(insert_stmt, 2);
        }
        cass_statement_free(insert_stmt);
        std::cout << "[Cassandra] Seed complete.\n";
    } else {
        std::cout << "[Cassandra] Already has " << count << " rows -- skipping seed.\n";
    }

    // 4. Prepare statements
    auto prepare = [&](const char* query) -> const CassPrepared* {
        CassFuture* prepare_future = cass_session_prepare(session_, query);
        cass_future_wait(prepare_future);
        check_future(prepare_future, "Prepare failed");
        const CassPrepared* prepared = cass_future_get_prepared(prepare_future);
        cass_future_free(prepare_future);
        return prepared;
    };

    prepared_read_  = prepare("SELECT val FROM bench.bench_kv WHERE id = ?");
    prepared_write_ = prepare("UPDATE bench.bench_kv SET val = ? WHERE id = ?");
}

void CassandraAdapter::perform_op() {
    if (!session_) return;

    static thread_local std::uniform_int_distribution<int> key_dist;
    static thread_local std::uniform_int_distribution<int> pct_dist(1, 100);
    static thread_local std::uniform_int_distribution<int> val_dist(0, 999'999);

    int  key     = key_dist(tl_rng, std::uniform_int_distribution<int>::param_type{1, seed_rows_});
    bool is_read = (pct_dist(tl_rng) <= read_pct_);

    CassStatement* statement = nullptr;

    if (is_read) {
        statement = cass_prepared_bind(prepared_read_);
        cass_statement_bind_int32(statement, 0, key);
    } else {
        std::string v = "upd_" + std::to_string(val_dist(tl_rng));
        statement = cass_prepared_bind(prepared_write_);
        cass_statement_bind_string(statement, 0, v.c_str());
        cass_statement_bind_int32(statement, 1, key);
    }

    CassFuture* future = cass_session_execute(session_, statement);
    cass_future_wait(future); // Sync for latency measurement
    
    // We don't throw on per-op errors to keep the bench running, 
    // but in a real app we would.
    cass_future_free(future);
    cass_statement_free(statement);
}

MetricMap CassandraAdapter::collect_metrics() {
    MetricMap metrics;
    if (!session_) return metrics;

    // 1. Driver-level metrics
    CassMetrics cass_metrics;
    cass_session_get_metrics(session_, &cass_metrics);

    metrics["cassandra.driver_request_mean_rate"]   = static_cast<double>(cass_metrics.requests.mean_rate);
    metrics["cassandra.driver_request_timeouts"]    = static_cast<long long>(cass_metrics.errors.request_timeouts);
    metrics["cassandra.driver_total_connections"]   = static_cast<long long>(cass_metrics.stats.total_connections);

    // 2. Server-side Disk Usage (Cassandra 4.0+)
    const char* disk_query = 
        "SELECT mebibytes "
        "FROM system_views.disk_usage "
        "WHERE keyspace_name = 'bench' AND table_name = 'bench_kv'";
        
    CassStatement* disk_stmt = cass_statement_new(disk_query, 0);
    CassFuture* disk_future = cass_session_execute(session_, disk_stmt);
    
    if (cass_future_error_code(disk_future) == CASS_OK) {
        const CassResult* res = cass_future_get_result(disk_future);
        if (cass_result_row_count(res) > 0) {
            const CassRow* row = cass_result_first_row(res);
            cass_int64_t mib;
            if (cass_value_get_int64(cass_row_get_column(row, 0), &mib) == CASS_OK) {
                metrics["cassandra.disk_mebibytes"] = static_cast<long long>(mib);
            }
        }
        cass_result_free(res);
    }
    cass_future_free(disk_future);
    cass_statement_free(disk_stmt);

    // 3. Pending Tasks (Compactions)
    const char* task_query = 
        "SELECT count(*) "
        "FROM system_views.sstable_tasks "
        "WHERE keyspace_name = 'bench' AND table_name = 'bench_kv'";
        
    CassStatement* task_stmt = cass_statement_new(task_query, 0);
    CassFuture* task_future = cass_session_execute(session_, task_stmt);
    if (cass_future_error_code(task_future) == CASS_OK) {
        const CassResult* res = cass_future_get_result(task_future);
        const CassRow* row = cass_result_first_row(res);
        cass_int64_t tasks;
        if (cass_value_get_int64(cass_row_get_column(row, 0), &tasks) == CASS_OK) {
            metrics["cassandra.pending_tasks"] = static_cast<long long>(tasks);
        }
        cass_result_free(res);
    }
    cass_future_free(task_future);
    cass_statement_free(task_stmt);

    return metrics;
}

void CassandraAdapter::disconnect() {
    if (prepared_read_) {
        cass_prepared_free(prepared_read_);
        prepared_read_ = nullptr;
    }
    if (prepared_write_) {
        cass_prepared_free(prepared_write_);
        prepared_write_ = nullptr;
    }
    if (session_) {
        CassFuture* close_future = cass_session_close(session_);
        cass_future_wait(close_future);
        cass_future_free(close_future);
        cass_session_free(session_);
        session_ = nullptr;
    }
    if (cluster_) {
        cass_cluster_free(cluster_);
        cluster_ = nullptr;
    }
}

} // namespace analyzer
