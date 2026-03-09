#ifndef ANALYZER_POSTGRESQL_ADAPTER_HPP
#define ANALYZER_POSTGRESQL_ADAPTER_HPP

#include "analyzer/DBAdapter.hpp"
#include <string>

// Forward declare libpq PGconn
typedef struct pg_conn PGconn;

namespace analyzer {

class PostgreSQLAdapter : public DBAdapter {
public:
    // Takes a standard connection string, e.g., "host=localhost user=user password=pass dbname=db"
    PostgreSQLAdapter(const std::string& conninfo);
    ~PostgreSQLAdapter() override;

    void connect() override;
    void perform_op() override;
    MetricMap collect_metrics() override;
    void disconnect() override;

private:
    std::string conninfo_;
    PGconn* conn_;
};

} // namespace analyzer

#endif // ANALYZER_POSTGRESQL_ADAPTER_HPP
