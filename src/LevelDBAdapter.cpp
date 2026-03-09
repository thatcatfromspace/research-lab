#include "analyzer/LevelDBAdapter.hpp"
#include <leveldb/db.h>
#include <stdexcept>

namespace analyzer {

LevelDBAdapter::LevelDBAdapter(const std::string& db_path)
    : db_path_(db_path), db_(nullptr) {}

LevelDBAdapter::~LevelDBAdapter() {
    disconnect();
}

void LevelDBAdapter::connect() {
    leveldb::Options options;
    options.create_if_missing = true;

    leveldb::Status status = leveldb::DB::Open(options, db_path_, &db_);
    if (!status.ok()) {
        throw std::runtime_error("LevelDB Open failed: " + status.ToString());
    }
}

void LevelDBAdapter::perform_op() {
    if (!db_) return;

    std::string key = "analyzer_test_key";
    std::string value = "analyzer_test_value";

    leveldb::Status s;
    s = db_->Put(leveldb::WriteOptions(), key, value);
    if (!s.ok()) return;

    std::string read_value;
    s = db_->Get(leveldb::ReadOptions(), key, &read_value);
}

MetricMap LevelDBAdapter::collect_metrics() {
    MetricMap metrics;
    if (!db_) return metrics;

    std::string stats;
    // LevelDB doesn't have fine-grained metric retrieval like RocksDB. 
    // It returns a formatted string for "leveldb.stats".
    // We will parse out something simple if possible, or just store a placeholder 
    // metric to demonstrate the Adapter pattern.
    if (db_->GetProperty("leveldb.stats", &stats)) {
        // Just acknowledging we got the stats string.
        metrics["leveldb.has_stats_string"] = 1LL;
        // In a real implementation, you would parse the stats string 
        // to extract compactions, file sizes, etc.
    } else {
        metrics["leveldb.has_stats_string"] = 0LL;
    }

    std::string approximate_size_str;
    if (db_->GetProperty("leveldb.approximate-memory-usage", &approximate_size_str)) {
         try {
            metrics["leveldb.approximate_memory_usage"] = std::stoll(approximate_size_str);
        } catch (...) {
            metrics["leveldb.approximate_memory_usage"] = nullptr;
        }
    } else {
         metrics["leveldb.approximate_memory_usage"] = nullptr;
    }


    // Explicit null per constraints
    metrics["leveldb.unsupported_feature_metric"] = nullptr;

    return metrics;
}

void LevelDBAdapter::disconnect() {
    if (db_) {
        delete db_;
        db_ = nullptr;
    }
}

} // namespace analyzer
