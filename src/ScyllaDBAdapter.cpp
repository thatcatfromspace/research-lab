#include "analyzer/ScyllaDBAdapter.hpp"

namespace analyzer {

ScyllaDBAdapter::ScyllaDBAdapter(const std::string& contact_points)
    : CassandraAdapter(contact_points) {}

MetricMap ScyllaDBAdapter::collect_metrics() {
    // Leverage the base Cassandra adapter
    MetricMap cassandra_metrics = CassandraAdapter::collect_metrics();
    MetricMap scylla_metrics;

    // Relabel the metrics to prefix "scylla."
    for (const auto& kv : cassandra_metrics) {
        std::string key = kv.first;
        // Replace "cassandra." with "scylla."
        if (key.rfind("cassandra.", 0) == 0) { 
            scylla_metrics["scylla." + key.substr(10)] = kv.second;
        } else {
            scylla_metrics[key] = kv.second;
        }
    }

    return scylla_metrics;
}

} // namespace analyzer
