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
    LevelDBAdapter(const std::string& db_path);
    ~LevelDBAdapter() override;

    void connect() override;
    void perform_op() override;
    MetricMap collect_metrics() override;
    void disconnect() override;

private:
    std::string db_path_;
    leveldb::DB* db_;
};

} // namespace analyzer

#endif // ANALYZER_LEVELDB_ADAPTER_HPP
