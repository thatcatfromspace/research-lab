#include "analyzer/PostgreSQLAdapter.hpp"
#include <libpq-fe.h>
#include <stdexcept>

namespace analyzer {

PostgreSQLAdapter::PostgreSQLAdapter(const std::string& conninfo)
    : conninfo_(conninfo), conn_(nullptr) {}

PostgreSQLAdapter::~PostgreSQLAdapter() {
    disconnect();
}

void PostgreSQLAdapter::connect() {
    conn_ = PQconnectdb(conninfo_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn_);
        disconnect();
        throw std::runtime_error("Connection to PostgreSQL failed: " + err);
    }
}

void PostgreSQLAdapter::perform_op() {
    if (!conn_) return;

    // Simulate a simple query 
    PGresult* res = PQexec(conn_, "SELECT 1");
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        // Real implementation might track error rates
    }
    PQclear(res);
}

MetricMap PostgreSQLAdapter::collect_metrics() {
    MetricMap metrics;
    if (!conn_) return metrics;

    // Query commit and rollback counts for the current database
    const char* query = "SELECT xact_commit, xact_rollback "
                        "FROM pg_stat_database "
                        "WHERE datname = current_database()";
                        
    PGresult* res = PQexec(conn_, query);
    
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        try {
            metrics["postgres.xact_commit"] = std::stoll(PQgetvalue(res, 0, 0));
            metrics["postgres.xact_rollback"] = std::stoll(PQgetvalue(res, 0, 1));
        } catch (...) {
            metrics["postgres.xact_commit"] = nullptr;
            metrics["postgres.xact_rollback"] = nullptr;
        }
    } else {
        // If we can't query it for some reason, insert explicit nulls
        metrics["postgres.xact_commit"] = nullptr;
        metrics["postgres.xact_rollback"] = nullptr;
    }
    
    PQclear(res);

    // Explicitly add an unsupported metric as required by constraints
    metrics["postgres.unsupported_feature_metric"] = nullptr;

    return metrics;
}

void PostgreSQLAdapter::disconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

} // namespace analyzer
