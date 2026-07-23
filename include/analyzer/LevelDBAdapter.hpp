#ifndef ANALYZER_LEVELDB_ADAPTER_HPP
#define ANALYZER_LEVELDB_ADAPTER_HPP

#include "analyzer/DBAdapter.hpp"
#include <string>

namespace leveldb {
    class DB;
}

namespace analyzer {

class LevelDBAdapter : public DBAdapter {
public:
    explicit LevelDBAdapter(const std::string& db_path);
    ~LevelDBAdapter() override;

    std::unique_ptr<DBAdapter> clone_connection() override;

    void connect() override;
    void configure(int read_pct, int row_count) override;
    void perform_read(int key) override;
    void perform_write(int key, const std::string& value) override;
    void perform_scan(int start_key, int count) override;
    MetricMap collect_metrics() override;
    void disconnect() override;

    // Public constructor for cloning
    LevelDBAdapter(const std::string& db_path, std::shared_ptr<leveldb::DB> db, int read_pct, int seed_rows);

private:
    std::string db_path_;
    std::shared_ptr<leveldb::DB> db_;

    // Seeds initial data into the KV store
    void setup_schema();

    int read_pct_ = 70;
    int seed_rows_ = 10000;
};

} // namespace analyzer

#endif // ANALYZER_LEVELDB_ADAPTER_HPP
