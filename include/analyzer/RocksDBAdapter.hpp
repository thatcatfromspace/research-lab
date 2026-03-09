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
    RocksDBAdapter(const std::string& db_path);
    ~RocksDBAdapter() override;

    void connect() override;
    void perform_op() override;
    MetricMap collect_metrics() override;
    void disconnect() override;

private:
    std::string db_path_;
    rocksdb::DB* db_;
};

} // namespace analyzer

#endif // ANALYZER_ROCKSDB_ADAPTER_HPP
