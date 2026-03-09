#ifndef ANALYZER_CASSANDRA_ADAPTER_HPP
#define ANALYZER_CASSANDRA_ADAPTER_HPP

#include "analyzer/DBAdapter.hpp"
#include <string>

// Forward declare DataStax structs
typedef struct CassCluster_ CassCluster;
typedef struct CassSession_ CassSession;

namespace analyzer {

class CassandraAdapter : public DBAdapter {
public:
    // Takes a comma-separated list of contact points
    CassandraAdapter(const std::string& contact_points);
    ~CassandraAdapter() override;

    void connect() override;
    void perform_op() override;
    MetricMap collect_metrics() override;
    void disconnect() override;

private:
    std::string contact_points_;
    CassCluster* cluster_;
    CassSession* session_;
};

} // namespace analyzer

#endif // ANALYZER_CASSANDRA_ADAPTER_HPP
