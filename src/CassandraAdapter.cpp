#include "analyzer/CassandraAdapter.hpp"
#include <cassandra.h>
#include <stdexcept>
#include <iostream>

namespace analyzer {

CassandraAdapter::CassandraAdapter(const std::string& contact_points)
    : contact_points_(contact_points), cluster_(nullptr), session_(nullptr) {}

CassandraAdapter::~CassandraAdapter() {
    disconnect();
}

void CassandraAdapter::connect() {
    cluster_ = cass_cluster_new();
    session_ = cass_session_new();
    
    cass_cluster_set_contact_points(cluster_, contact_points_.c_str());

    CassFuture* connect_future = cass_session_connect(session_, cluster_);
    
    if (cass_future_error_code(connect_future) != CASS_OK) {
        const char* message;
        size_t message_length;
        cass_future_error_message(connect_future, &message, &message_length);
        std::string err(message, message_length);
        cass_future_free(connect_future);
        throw std::runtime_error("Cassandra connect failed: " + err);
    }
    
    cass_future_free(connect_future);
}

void CassandraAdapter::perform_op() {
    if (!session_) return;

    // Simulate a simple system query just to ping the node
    const char* query = "SELECT release_version FROM system.local";
    CassStatement* statement = cass_statement_new(query, 0);

    CassFuture* result_future = cass_session_execute(session_, statement);

    if (cass_future_error_code(result_future) == CASS_OK) {
        // Success
    }

    cass_statement_free(statement);
    cass_future_free(result_future);
}

MetricMap CassandraAdapter::collect_metrics() {
    MetricMap metrics;
    if (!session_) return metrics;

    CassMetrics cass_metrics;
    cass_session_get_metrics(session_, &cass_metrics);

    // Collect DataStax driver level metrics
    metrics["cassandra.requests_timeouts"] = static_cast<long long>(cass_metrics.requests.timeouts);
    metrics["cassandra.requests_connection_errors"] = static_cast<long long>(cass_metrics.requests.connection_errors);
    metrics["cassandra.stats_total_connections"] = static_cast<long long>(cass_metrics.stats.total_connections);
    metrics["cassandra.stats_available_connections"] = static_cast<long long>(cass_metrics.stats.available_connections);

    // Add required explicit null
    metrics["cassandra.unsupported_feature_metric"] = nullptr;

    return metrics;
}

void CassandraAdapter::disconnect() {
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
