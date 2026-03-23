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

    void connect() override;
    void perform_op() override;
    MetricMap collect_metrics() override;
    void disconnect() override;

private:
    std::string conninfo_;
    PGconn*     conn_;

    // Creates bench_kv table, seeds rows, and prepares named statements.
    // Called once at the end of connect().
    void setup_schema();
};

} // namespace analyzer

#endif // ANALYZER_POSTGRESQL_ADAPTER_HPP
