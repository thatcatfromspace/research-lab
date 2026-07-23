#ifndef ANALYZER_ROCKSDB_ADAPTER_HPP
#define ANALYZER_ROCKSDB_ADAPTER_HPP

#include "analyzer/DBAdapter.hpp"
#include <string>

namespace rocksdb {
    class DB;
}

namespace analyzer {

class RocksDBAdapter : public DBAdapter {
public:
    explicit RocksDBAdapter(const std::string& db_path);
    ~RocksDBAdapter() override;

    std::unique_ptr<DBAdapter> clone_connection() override;

    void connect() override;
    void configure(int read_pct, int row_count) override;
    void perform_read(int key) override;
    void perform_write(int key, const std::string& value) override;
    void perform_scan(int start_key, int count) override;
    MetricMap collect_metrics() override;
    void disconnect() override;

    // Public constructor for cloning
    RocksDBAdapter(const std::string& db_path, std::shared_ptr<rocksdb::DB> db, int read_pct, int seed_rows);

private:
    std::string db_path_;
    std::shared_ptr<rocksdb::DB> db_;

    // Seeds initial data into the KV store
    void setup_schema();

    int read_pct_ = 70;
    int seed_rows_ = 10000;
};

} // namespace analyzer

#endif // ANALYZER_ROCKSDB_ADAPTER_HPP
