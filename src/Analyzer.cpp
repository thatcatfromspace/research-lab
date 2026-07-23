#include "analyzer/Analyzer.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "analyzer/ZipfianGenerator.hpp"
#include <random>
#include <thread>
#include <atomic>
#include <mutex>

namespace analyzer {

Analyzer::Analyzer(std::unique_ptr<DBAdapter> adapter)
    : adapter_(std::move(adapter)) {}

RunResult Analyzer::run(const RunOptions& options) {
    // Connect base adapter to setup schema
    adapter_->connect();

    int n_threads = std::max(1, options.thread_count);
    std::vector<std::unique_ptr<DBAdapter>> worker_adapters;
    for (int i = 0; i < n_threads; ++i) {
        if (i == 0) {
            worker_adapters.push_back(adapter_->clone_connection());
        } else {
            worker_adapters.push_back(adapter_->clone_connection());
        }
    }

    std::string dummy_payload(options.payload_size, 'a');

    RunResult result;
    auto run_start_time = std::chrono::steady_clock::now();
    
    std::atomic<size_t> global_total_count{0};
    std::atomic<bool> is_running{true};
    
    std::mutex interval_mutex;
    Stats global_interval_stats;
    Stats global_total_stats;

    // Sampler Thread
    std::thread sampler([&]() {
        auto last_sample_time = std::chrono::steady_clock::now();
        size_t last_sample_count = 0;
        
        while (is_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed_since_sample = std::chrono::duration<double>(current_time - last_sample_time).count();
            
            if (elapsed_since_sample >= 1.0) {
                size_t current_total = global_total_count.load(std::memory_order_relaxed);
                
                Stats current_interval;
                {
                    std::lock_guard<std::mutex> lock(interval_mutex);
                    current_interval = std::move(global_interval_stats);
                    // move constructor leaves global_interval_stats in valid empty state
                }
                
                TimeSeriesPoint point;
                point.elapsed_time_s = std::chrono::duration<double>(current_time - run_start_time).count();
                point.latency_stats = current_interval.get_snapshot();
                point.throughput_ops = (current_total - last_sample_count) / elapsed_since_sample;
                
                result.time_series.push_back(point);
                
                last_sample_time = current_time;
                last_sample_count = current_total;
            }
        }
    });

    for (const auto& phase : options.phases) {
        std::atomic<size_t> global_phase_count{0};
        auto phase_start_time = std::chrono::steady_clock::now();
        
        std::vector<std::thread> threads;
        for (int t = 0; t < n_threads; ++t) {
            threads.emplace_back([&, t]() {
                auto& thread_adapter = worker_adapters[t];
                
                std::mt19937 rng(std::random_device{}() + t);
                std::uniform_int_distribution<int> unif_dist(0, options.row_count - 1);
                std::uniform_int_distribution<int> op_dist(1, 100);
                std::unique_ptr<ZipfianGenerator> zipf = std::make_unique<ZipfianGenerator>(options.row_count);
                
                std::vector<long long> local_interval_lats;
                local_interval_lats.reserve(1000);
                
                auto should_continue = [&]() {
                    if (phase.operation_count > 0) {
                        return global_phase_count.load(std::memory_order_relaxed) < phase.operation_count;
                    } else if (phase.duration_seconds > 0) {
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - phase_start_time).count();
                        return static_cast<size_t>(elapsed) < phase.duration_seconds;
                    }
                    return false;
                };

                while (should_continue()) {
                    int op_type = op_dist(rng);
                    int key = (phase.distribution == Distribution::ZIPFIAN) ? zipf->next() : unif_dist(rng);

                    auto op_start = std::chrono::steady_clock::now();
                    
                    if (op_type <= phase.read_pct) {
                        thread_adapter->perform_read(key);
                    } else if (op_type <= phase.read_pct + phase.write_pct) {
                        thread_adapter->perform_write(key, dummy_payload);
                    } else {
                        thread_adapter->perform_scan(key, 10);
                    }

                    auto op_end = std::chrono::steady_clock::now();
                    auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(op_end - op_start).count();
                    
                    local_interval_lats.push_back(latency_us);
                    
                    if (local_interval_lats.size() >= 1000) {
                        std::lock_guard<std::mutex> lock(interval_mutex);
                        for (auto l : local_interval_lats) {
                            global_interval_stats.add_latency(l);
                            global_total_stats.add_latency(l);
                        }
                        global_total_count.fetch_add(local_interval_lats.size(), std::memory_order_relaxed);
                        global_phase_count.fetch_add(local_interval_lats.size(), std::memory_order_relaxed);
                        local_interval_lats.clear();
                    }
                }
                
                // flush remainder
                if (!local_interval_lats.empty()) {
                    std::lock_guard<std::mutex> lock(interval_mutex);
                    for (auto l : local_interval_lats) {
                        global_interval_stats.add_latency(l);
                        global_total_stats.add_latency(l);
                    }
                    global_total_count.fetch_add(local_interval_lats.size(), std::memory_order_relaxed);
                    global_phase_count.fetch_add(local_interval_lats.size(), std::memory_order_relaxed);
                }
            });
        }
        
        for (auto& th : threads) {
            th.join();
        }
    }

    is_running.store(false);
    sampler.join();

    auto end_time = std::chrono::steady_clock::now();
    double total_duration_s = std::chrono::duration<double>(end_time - run_start_time).count();
    
    // Disconnect workers
    for (auto& wa : worker_adapters) {
        wa->disconnect();
    }

    // Collect metrics from the base adapter
    MetricMap db_metrics = adapter_->collect_metrics();
    adapter_->disconnect();

    // Compute standardized amplification metrics if possible
    AmplificationMetrics amp;
    auto get_metric = [&](const std::string& k) -> double {
        auto it = db_metrics.find(k);
        if (it != db_metrics.end()) {
            if (std::holds_alternative<long long>(it->second)) return static_cast<double>(std::get<long long>(it->second));
            if (std::holds_alternative<double>(it->second)) return std::get<double>(it->second);
        }
        return 0.0;
    };

    double rocks_wa = get_metric("rocksdb.stats.cumulative.write_amplification");
    if (rocks_wa > 0) amp.write_amp = rocks_wa;
    
    double mysql_data = get_metric("mysql.table_data_bytes");
    if (mysql_data > 0 && options.row_count > 0) {
        amp.space_amp = mysql_data / (options.row_count * static_cast<double>(options.payload_size));
    }
    
    double pg_data = get_metric("postgres.total_rel_bytes");
    if (pg_data > 0 && options.row_count > 0) {
        amp.space_amp = pg_data / (options.row_count * static_cast<double>(options.payload_size));
    }
    
    result.latency_stats = global_total_stats.get_snapshot();
    result.throughput_ops = (total_duration_s > 0) ? (global_total_count.load() / total_duration_s) : 0;
    result.amplification = amp;
    result.db_metrics = db_metrics;

    return result;
}

void Analyzer::save_json(const RunResult &result, const std::string &filename) {
  nlohmann::json j;

  j["latency_stats"] = {{"avg_us", result.latency_stats.avg},
                        {"p50_us", result.latency_stats.p50},
                        {"p95_us", result.latency_stats.p95},
                        {"p99_us", result.latency_stats.p99}};

  j["throughput_ops"] = result.throughput_ops;

  nlohmann::json ts_array = nlohmann::json::array();
  for (const auto& pt : result.time_series) {
      ts_array.push_back({
          {"elapsed_time_s", pt.elapsed_time_s},
          {"throughput_ops", pt.throughput_ops},
          {"latency_stats", {
              {"avg_us", pt.latency_stats.avg},
              {"p50_us", pt.latency_stats.p50},
              {"p95_us", pt.latency_stats.p95},
              {"p99_us", pt.latency_stats.p99}
          }}
      });
  }
  j["time_series"] = ts_array;

  j["amplification"] = {
      {"write_amp", result.amplification.write_amp},
      {"read_amp", result.amplification.read_amp},
      {"space_amp", result.amplification.space_amp}
  };

  j["db_metrics"] = nlohmann::json::object();
  for (const auto &[key, value] : result.db_metrics) {
    if (std::holds_alternative<std::nullptr_t>(value)) {
      j["db_metrics"][key] = nullptr;
    } else if (std::holds_alternative<long long>(value)) {
      j["db_metrics"][key] = std::get<long long>(value);
    } else if (std::holds_alternative<double>(value)) {
      j["db_metrics"][key] = std::get<double>(value);
    }
  }

  std::ofstream ofs(filename);
  if (!ofs.is_open()) {
    std::cerr << "Failed to open output file: " << filename << std::endl;
    return;
  }

  ofs << j.dump(2);
}

} // namespace analyzer
