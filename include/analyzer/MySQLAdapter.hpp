#ifndef ANALYZER_MYSQL_ADAPTER_HPP
#define ANALYZER_MYSQL_ADAPTER_HPP

#include "analyzer/DBAdapter.hpp"
#include <string>

// Forward declare MYSQL to avoid including mysql headers everywhere
typedef struct st_mysql MYSQL;

namespace analyzer {

class MySQLAdapter : public DBAdapter {
public:
    MySQLAdapter(const std::string& host, const std::string& user,
                 const std::string& password, const std::string& dbname,
                 int port = 3306);

    ~MySQLAdapter() override;

    void connect() override;
    void perform_op() override;
    MetricMap collect_metrics() override;
    void disconnect() override;

private:
    std::string host_;
    std::string user_;
    std::string password_;
    std::string dbname_;
    int   port_;
    MYSQL* conn_;

    // Creates bench_kv table and seeds rows if empty.
    // Called once at the end of connect().
    void setup_schema();
};

} // namespace analyzer

#endif // ANALYZER_MYSQL_ADAPTER_HPP
