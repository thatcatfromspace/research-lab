#include "analyzer/LevelDBAdapter.hpp"
#include <leveldb/db.h>
#include <leveldb/options.h>
#include <leveldb/write_batch.h>
#include <stdexcept>
#include <iostream>
#include <random>

namespace analyzer {

// ─── Thread-local RNG ─────────────────────────────────────────────────────────
static thread_local std::mt19937 tl_rng{std::random_device{}()};

// ─── Constructor / Destructor ─────────────────────────────────────────────────

LevelDBAdapter::LevelDBAdapter(const std::string& db_path)
    : db_path_(db_path), db_(nullptr) {}

LevelDBAdapter::~LevelDBAdapter() {
    disconnect();
}

// ─── connect() ────────────────────────────────────────────────────────────────

void LevelDBAdapter::connect() {
    leveldb::Options options;
    options.create_if_missing = true;
    
    leveldb::Status status = leveldb::DB::Open(options, db_path_, &db_);
    if (!status.ok()) {
        throw std::runtime_error("LevelDB Open failed: " + status.ToString());
    }

    setup_schema();
}

// ─── configure() ───────────────────────────────────────────────────────────────

void LevelDBAdapter::configure(int read_pct, int row_count) {
    read_pct_ = read_pct;
    seed_rows_ = row_count;
}

// ─── setup_schema() ───────────────────────────────────────────────────────────

void LevelDBAdapter::setup_schema() {
    std::string value;
    leveldb::Status s = db_->Get(leveldb::ReadOptions(), "1", &value);
    
    if (s.ok()) {
        std::cout << "[LevelDB] Data already exists at " << db_path_ << " -- skipping seed.\n";
        return;
    }

    std::cout << "[LevelDB] Seeding " << seed_rows_ << " keys into " << db_path_ << "...\n";
    
    leveldb::WriteBatch batch;
    for (int i = 1; i <= seed_rows_; ++i) {
        std::string k = std::to_string(i);
        std::string v = "seed_" + k;
        batch.Put(k, v);
        
        if (i % 1000 == 0) {
            db_->Write(leveldb::WriteOptions(), &batch);
            batch.Clear();
        }
    }
    db_->Write(leveldb::WriteOptions(), &batch);
    
    std::cout << "[LevelDB] Seed complete.\n";
}

// ─── perform_op() ─────────────────────────────────────────────────────────────

void LevelDBAdapter::perform_op() {
    if (!db_) return;

    static thread_local std::uniform_int_distribution<int> key_dist;
    static thread_local std::uniform_int_distribution<int> pct_dist(1, 100);
    static thread_local std::uniform_int_distribution<int> val_dist(0, 999'999);

    int  key     = key_dist(tl_rng, std::uniform_int_distribution<int>::param_type{1, seed_rows_});
    bool is_read = (pct_dist(tl_rng) <= read_pct_);
    std::string k_str = std::to_string(key);

    if (is_read) {
        std::string val;
        db_->Get(leveldb::ReadOptions(), k_str, &val);
    } else {
        std::string v_str = "upd_" + std::to_string(val_dist(tl_rng));
        db_->Put(leveldb::WriteOptions(), k_str, v_str);
    }
}

MetricMap LevelDBAdapter::collect_metrics() {
    MetricMap metrics;
    if (!db_) return metrics;

    auto get_prop = [&](const std::string& prop, const std::string& metric_name) {
        std::string val;
        if (db_->GetProperty(prop, &val)) {
            // LevelDB prop strings are often formatted info, 
            // only 'approximate-memory-usage' is a pure numeric string.
            if (prop == "leveldb.approximate-memory-usage") {
                try {
                    metrics[metric_name] = std::stoll(val);
                } catch (...) {
                    metrics[metric_name] = nullptr;
                }
            } else {
                // If we get "leveldb.stats", just note that we have it
                metrics[metric_name] = 1LL;
            }
        } else {
            metrics[metric_name] = nullptr;
        }
    };

    get_prop("leveldb.approximate-memory-usage", "leveldb.approx_mem_bytes");
    get_prop("leveldb.stats",                   "leveldb.has_internal_stats");

    return metrics;
}

void LevelDBAdapter::disconnect() {
    if (db_) {
        delete db_;
        db_ = nullptr;
    }
}

} // namespace analyzer
