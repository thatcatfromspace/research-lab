#include "analyzer/RocksDBAdapter.hpp"
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <stdexcept>
#include <iostream>
#include <random>

namespace analyzer {

// ─── Bench parameters ─────────────────────────────────────────────────────────
static constexpr int ROCKS_SEED_ROWS = 10'000;
static constexpr int ROCKS_READ_PCT  = 70;

// ─── Thread-local RNG ─────────────────────────────────────────────────────────
static thread_local std::mt19937                       tl_rng{std::random_device{}()};
static thread_local std::uniform_int_distribution<int> key_dist{1, ROCKS_SEED_ROWS};
static thread_local std::uniform_int_distribution<int> pct_dist{1, 100};
static thread_local std::uniform_int_distribution<int> val_dist{0, 999'999};

RocksDBAdapter::RocksDBAdapter(const std::string& db_path)
    : db_path_(db_path), db_(nullptr) {}

RocksDBAdapter::~RocksDBAdapter() {
    disconnect();
}

void RocksDBAdapter::connect() {
    rocksdb::Options options;
    options.create_if_missing = true;
    
    // Optimize for fast SSD/Local storage
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();

    rocksdb::Status status = rocksdb::DB::Open(options, db_path_, &db_);
    if (!status.ok()) {
        throw std::runtime_error("RocksDB Open failed: " + status.ToString());
    }

    setup_schema();
}

void RocksDBAdapter::setup_schema() {
    // For RocksDB, "seeding" just means ensuring keys 1-10000 exist.
    // We'll check if the DB is empty or just overwrite to ensure consistency.
    
    std::string value;
    rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), "1", &value);
    
    if (s.ok()) {
        std::cout << "[RocksDB] Data already exists at " << db_path_ << " -- skipping seed.\n";
        return;
    }

    std::cout << "[RocksDB] Seeding " << ROCKS_SEED_ROWS << " keys into " << db_path_ << "...\n";
    
    rocksdb::WriteBatch batch;
    for (int i = 1; i <= ROCKS_SEED_ROWS; ++i) {
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

void RocksDBAdapter::perform_op() {
    if (!db_) return;

    int  key     = key_dist(tl_rng);
    bool is_read = (pct_dist(tl_rng) <= ROCKS_READ_PCT);
    std::string k_str = std::to_string(key);

    if (is_read) {
        std::string val;
        db_->Get(rocksdb::ReadOptions(), k_str, &val);
    } else {
        std::string v_str = "upd_" + std::to_string(val_dist(tl_rng));
        db_->Put(rocksdb::WriteOptions(), k_str, v_str);
    }
}

MetricMap RocksDBAdapter::collect_metrics() {
    MetricMap metrics;
    if (!db_) return metrics;

    auto get_prop = [&](const std::string& prop, const std::string& metric_name) {
        std::string val;
        if (db_->GetProperty(prop, &val)) {
            try {
                metrics[metric_name] = std::stoll(val);
            } catch (...) {
                metrics[metric_name] = nullptr;
            }
        } else {
            metrics[metric_name] = nullptr;
        }
    };

    // RocksDB specific properties
    get_prop("rocksdb.estimate-num-keys",         "rocksdb.estimated_keys");
    get_prop("rocksdb.cur-size-all-mem-tables",   "rocksdb.memtable_bytes");
    get_prop("rocksdb.num-immutable-mem-table",  "rocksdb.num_imm_memtables");
    get_prop("rocksdb.num-live-versions",         "rocksdb.num_live_versions");
    get_prop("rocksdb.estimate-table-readers-mem", "rocksdb.index_filter_bytes");
    get_prop("rocksdb.block-cache-usage",         "rocksdb.block_cache_bytes");
    
    return metrics;
}

void RocksDBAdapter::disconnect() {
    if (db_) {
        delete db_;
        db_ = nullptr;
    }
}

} // namespace analyzer
