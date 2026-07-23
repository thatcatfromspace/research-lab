#ifndef ANALYZER_POSTGRESQL_ADAPTER_HPP
#define ANALYZER_POSTGRESQL_ADAPTER_HPP

#include "analyzer/DBAdapter.hpp"
#include <string>

// Forward declare libpq PGconn
typedef struct pg_conn PGconn;

namespace analyzer {

class PostgreSQLAdapter : public DBAdapter {
public:
    // Takes a standard libpq connection string:
    // e.g. "host=127.0.0.1 port=5432 user=bench password=benchpass dbname=bench"
    explicit PostgreSQLAdapter(const std::string& conninfo);
    ~PostgreSQLAdapter() override;

    std::unique_ptr<DBAdapter> clone_connection() override;

    void connect() override;
    void configure(int read_pct, int row_count) override;
    void perform_read(int key) override;
    void perform_write(int key, const std::string& value) override;
    void perform_scan(int start_key, int count) override;
    MetricMap collect_metrics() override;
    void disconnect() override;

private:
    std::string conninfo_;
    PGconn*     conn_;

    // Creates bench_kv table, seeds rows, and prepares named statements.
    void setup_schema();

    int read_pct_ = 70;
    int seed_rows_ = 10000;
};

} // namespace analyzer

#endif // ANALYZER_POSTGRESQL_ADAPTER_HPP
