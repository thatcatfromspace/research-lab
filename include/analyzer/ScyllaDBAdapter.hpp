#ifndef ANALYZER_SCYLLADB_ADAPTER_HPP
#define ANALYZER_SCYLLADB_ADAPTER_HPP

#include "analyzer/CassandraAdapter.hpp"

namespace analyzer {

// ScyllaDB is compatible with Cassandra and the DataStax C++ driver.
// This adapter inherits from CassandraAdapter but implements a distinct 
// collect_metrics to show differentiation if we can target Scylla-specific 
// system tables, or rename the metrics keys.
class ScyllaDBAdapter : public CassandraAdapter {
public:
    ScyllaDBAdapter(const std::string& contact_points);
    ~ScyllaDBAdapter() override = default;

    MetricMap collect_metrics() override;
};

} // namespace analyzer

#endif // ANALYZER_SCYLLADB_ADAPTER_HPP
