#include "analyzer/RocksDBAdapter.hpp"
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <stdexcept>
#include <iostream>
#include <random>

namespace analyzer {

// ─── Thread-local RNG ─────────────────────────────────────────────────────────
static thread_local std::mt19937 tl_rng{std::random_device{}()};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

RocksDBAdapter::RocksDBAdapter(const std::string& db_path)
    : db_path_(db_path), db_(nullptr) {}

RocksDBAdapter::RocksDBAdapter(const std::string& db_path, std::shared_ptr<rocksdb::DB> db, int read_pct, int seed_rows)
    : db_path_(db_path), db_(db), read_pct_(read_pct), seed_rows_(seed_rows) {}

std::unique_ptr<DBAdapter> RocksDBAdapter::clone_connection() {
    return std::make_unique<RocksDBAdapter>(db_path_, db_, read_pct_, seed_rows_);
}

RocksDBAdapter::~RocksDBAdapter() {
    disconnect();
}

// ─── connect() ────────────────────────────────────────────────────────────────

void RocksDBAdapter::connect() {
    rocksdb::Options options;
    options.create_if_missing = true;
    
    // Optimize for fast SSD/Local storage
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();

    rocksdb::DB* raw_db = nullptr;
    rocksdb::Status status = rocksdb::DB::Open(options, db_path_, &raw_db);
    if (!status.ok()) {
        throw std::runtime_error("RocksDB Open failed: " + status.ToString());
    }
    db_ = std::shared_ptr<rocksdb::DB>(raw_db);

    setup_schema();
}

// ─── configure() ───────────────────────────────────────────────────────────────

void RocksDBAdapter::configure(int read_pct, int row_count) {
    read_pct_ = read_pct;
    seed_rows_ = row_count;
}

// ─── setup_schema() ───────────────────────────────────────────────────────────

void RocksDBAdapter::setup_schema() {
    std::string value;
    rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), "1", &value);
    
    if (s.ok()) {
        std::cout << "[RocksDB] Data already exists at " << db_path_ << " -- skipping seed.\n";
        return;
    }

    std::cout << "[RocksDB] Seeding " << seed_rows_ << " keys into " << db_path_ << "...\n";
    
    rocksdb::WriteBatch batch;
    for (int i = 1; i <= seed_rows_; ++i) {
        std::string k = std::to_string(i);
        std::string v = "seed_" + k;
        batch.Put(k, v);
        
        // Write in batches of 1000 to avoid massive memory usage during init
        if (i % 1000 == 0) {
            db_->Write(rocksdb::WriteOptions(), &batch);
            batch.Clear();
        }
    }
    db_->Write(rocksdb::WriteOptions(), &batch);
    
    std::cout << "[RocksDB] Seed complete.\n";
}

void RocksDBAdapter::perform_read(int key) {
    if (!db_) return;
    std::string val;
    db_->Get(rocksdb::ReadOptions(), std::to_string(key), &val);
}

void RocksDBAdapter::perform_write(int key, const std::string& value) {
    if (!db_) return;
    db_->Put(rocksdb::WriteOptions(), std::to_string(key), value);
}

void RocksDBAdapter::perform_scan(int start_key, int count) {
    if (!db_) return;
    rocksdb::Iterator* it = db_->NewIterator(rocksdb::ReadOptions());
    it->Seek(std::to_string(start_key));
    for (int i = 0; i < count && it->Valid(); ++i) {
        it->Next();
    }
    delete it;
}

MetricMap RocksDBAdapter::collect_metrics() {
    MetricMap metrics;
    if (!db_) return metrics;

    auto get_prop = [&](const std::string& prop, const std::string& metric_name) {
        std::string val;
        uint64_t val_uint;
        if (db_->GetIntProperty(prop, &val_uint)) {
            metrics[metric_name] = static_cast<long long>(val_uint);
        } else if (db_->GetProperty(prop, &val)) {
            try {
                metrics[metric_name] = std::stoll(val);
            } catch (...) {
                metrics[metric_name] = nullptr;
            }
        } else {
            metrics[metric_name] = nullptr;
        }
    };

    // 1. Basic Capacity & Memory
    get_prop("rocksdb.estimate-num-keys",          "rocksdb.estimated_keys");
    get_prop("rocksdb.cur-size-all-mem-tables",    "rocksdb.memtable_bytes");
    get_prop("rocksdb.block-cache-usage",          "rocksdb.block_cache_bytes");
    get_prop("rocksdb.estimate-table-readers-mem", "rocksdb.index_filter_bytes");

    // 2. Space Amplification
    get_prop("rocksdb.total-sst-files-size",       "rocksdb.total_sst_bytes");
    get_prop("rocksdb.estimate-live-data-size",    "rocksdb.live_data_bytes");
    
    // 3. Write Amplification (Compaction stats)
    get_prop("rocksdb.compaction-pending",          "rocksdb.compaction_pending");
    get_prop("rocksdb.background-errors",           "rocksdb.bg_errors");
    
    // Note: Compaction bytes are cumulative since DB open
    // They give a clear picture of Write Amplification in LSM
    get_prop("rocksdb.actual-delayed-write-rate",   "rocksdb.write_stall_rate");

    return metrics;
}

void RocksDBAdapter::disconnect() {
    if (db_) {
        db_.reset();
    }
}

} // namespace analyzer
