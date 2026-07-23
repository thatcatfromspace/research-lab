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

    std::unique_ptr<DBAdapter> clone_connection() override;

    void connect() override;
    void configure(int read_pct, int row_count) override;
    void perform_read(int key) override;
    void perform_write(int key, const std::string& value) override;
    void perform_scan(int start_key, int count) override;
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
    void setup_schema();

    int read_pct_ = 70;
    int seed_rows_ = 10000;
};

} // namespace analyzer

#endif // ANALYZER_MYSQL_ADAPTER_HPP
