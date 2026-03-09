#include "analyzer/RocksDBAdapter.hpp"
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <stdexcept>
#include <iostream>

namespace analyzer {

RocksDBAdapter::RocksDBAdapter(const std::string& db_path)
    : db_path_(db_path), db_(nullptr) {}

RocksDBAdapter::~RocksDBAdapter() {
    disconnect();
}

void RocksDBAdapter::connect() {
    rocksdb::Options options;
    options.create_if_missing = true;
    
    // Enable statistics to collect metrics
    // options.statistics = rocksdb::CreateDBStatistics(); 
    // (Omitted depending on rocksdb version, using GetProperty instead for basic stats)

    rocksdb::Status status = rocksdb::DB::Open(options, db_path_, &db_);
    if (!status.ok()) {
        throw std::runtime_error("RocksDB Open failed: " + status.ToString());
    }
}

void RocksDBAdapter::perform_op() {
    if (!db_) return;

    // Simulate a simple point write and read
    std::string key = "analyzer_test_key";
    std::string value = "analyzer_test_value";
    
    rocksdb::Status s;
    s = db_->Put(rocksdb::WriteOptions(), key, value);
    if (!s.ok()) return;

    std::string read_value;
    s = db_->Get(rocksdb::ReadOptions(), key, &read_value);
}

MetricMap RocksDBAdapter::collect_metrics() {
    MetricMap metrics;
    if (!db_) return metrics;

    std::string prop_value;
    
    // Try to get block cache hit count
    if (db_->GetProperty("rocksdb.block-cache-hit-count", &prop_value)) {
        try {
            metrics["rocksdb.block_cache_hits"] = std::stoll(prop_value);
        } catch (...) {
            metrics["rocksdb.block_cache_hits"] = nullptr;
        }
    } else {
        metrics["rocksdb.block_cache_hits"] = nullptr;
    }

    // Try to get estimated keys
    if (db_->GetProperty("rocksdb.estimate-num-keys", &prop_value)) {
        try {
            metrics["rocksdb.estimate_num_keys"] = std::stoll(prop_value);
        } catch (...) {
            metrics["rocksdb.estimate_num_keys"] = nullptr;
        }
    } else {
        metrics["rocksdb.estimate_num_keys"] = nullptr;
    }

    // Explicitly add an unsupported metric as required by constraints
    metrics["rocksdb.unsupported_feature_metric"] = nullptr;

    return metrics;
}

void RocksDBAdapter::disconnect() {
    if (db_) {
        delete db_;
        db_ = nullptr;
    }
}

} // namespace analyzer
