#ifndef ANALYZER_CASSANDRA_ADAPTER_HPP
#define ANALYZER_CASSANDRA_ADAPTER_HPP

#include "analyzer/DBAdapter.hpp"
#include <string>

// Forward declare DataStax structs
typedef struct CassCluster_ CassCluster;
typedef struct CassSession_ CassSession;
typedef struct CassPrepared_ CassPrepared;

namespace analyzer {

class CassandraAdapter : public DBAdapter {
public:
    // Takes a comma-separated list of contact points
    explicit CassandraAdapter(const std::string& contact_points);
    ~CassandraAdapter() override;

    std::unique_ptr<DBAdapter> clone_connection() override;

    void connect() override;
    void configure(int read_pct, int row_count) override;
    void perform_read(int key) override;
    void perform_write(int key, const std::string& value) override;
    void perform_scan(int start_key, int count) override;
    MetricMap collect_metrics() override;
    void disconnect() override;

private:
    std::string contact_points_;
    CassCluster* cluster_;
    CassSession* session_;

    const CassPrepared* prepared_read_;
    const CassPrepared* prepared_write_;

    // Creates keyspace, table, and seeds initial data
    void setup_schema();

    int read_pct_ = 70;
    int seed_rows_ = 10000;
};

} // namespace analyzer

#endif // ANALYZER_CASSANDRA_ADAPTER_HPP
