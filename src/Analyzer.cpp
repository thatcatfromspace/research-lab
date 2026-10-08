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
#include <sstream>

namespace analyzer {

static MetricMap process_io() {
    MetricMap out;
    std::ifstream file("/proc/self/io");
    std::string line, name;
    long long value;
    while (std::getline(file, line)) {
        std::istringstream in(line);
        if (in >> name >> value) {
            if (!name.empty() && name.back() == ':') name.pop_back();
            out[name] = value;
        }
    }
    return out;
}

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
    MetricMap db_metrics_before = adapter_->collect_metrics();
    MetricMap process_io_before = process_io();

    std::string dummy_payload(options.payload_size, 'a');

    RunResult result;
    auto run_start_time = std::chrono::steady_clock::now();

    std::atomic<size_t> global_total_count{0};
    std::atomic<long long> attempted[3]{};
    std::atomic<long long> successful[3]{};
    std::atomic<long long> interval_attempted{0}, interval_successful{0};
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
            if (!is_running.load()) break;

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
                point.attempted = interval_attempted.exchange(0);
                point.successful = interval_successful.exchange(0);
                point.failed = point.attempted - point.successful;

                result.time_series.push_back(point);

                last_sample_time = current_time;
                last_sample_count = current_total;
            }
        }
    });

    for (const auto& phase : options.phases) {
        std::atomic<size_t> global_phase_count{0};
        auto phase_start_time = std::chrono::steady_clock::now();
        auto phase_start_count = global_total_count.load();

        std::vector<std::thread> threads;
        for (int t = 0; t < n_threads; ++t) {
            threads.emplace_back([&, t]() {
                auto& thread_adapter = worker_adapters[t];

                std::mt19937 rng(std::random_device{}() + t);
                std::uniform_int_distribution<int> unif_dist(1, options.row_count);
                std::uniform_int_distribution<int> op_dist(1, 100);
                std::unique_ptr<ZipfianGenerator> zipf = std::make_unique<ZipfianGenerator>(options.row_count);

                std::vector<long long> local_interval_lats;
                local_interval_lats.reserve(1000);
                auto last_flush = std::chrono::steady_clock::now();

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
                    int key = (phase.distribution == Distribution::ZIPFIAN) ? zipf->next() + 1 : unif_dist(rng);

                    auto op_start = std::chrono::steady_clock::now();

                    int kind;
                    bool ok;
                    if (op_type <= phase.read_pct) {
                        kind = 0;
                        ok = thread_adapter->perform_read(key);
                    } else if (op_type <= phase.read_pct + phase.write_pct) {
                        kind = 1;
                        ok = thread_adapter->perform_write(key, dummy_payload);
                    } else {
                        kind = 2;
                        ok = thread_adapter->perform_scan(key, 10);
                    }
                    auto op_end = std::chrono::steady_clock::now();
                    attempted[kind].fetch_add(1, std::memory_order_relaxed);
                    interval_attempted.fetch_add(1, std::memory_order_relaxed);
                    if (ok) {
                        successful[kind].fetch_add(1, std::memory_order_relaxed);
                        interval_successful.fetch_add(1, std::memory_order_relaxed);
                    }

                    auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(op_end - op_start).count();

                    local_interval_lats.push_back(latency_us);
                    global_total_count.fetch_add(1, std::memory_order_relaxed);
                    global_phase_count.fetch_add(1, std::memory_order_relaxed);

                    if (local_interval_lats.size() >= 1000 ||
                        op_end - last_flush >= std::chrono::milliseconds(100)) {
                        std::lock_guard<std::mutex> lock(interval_mutex);
                        for (auto l : local_interval_lats) {
                            global_interval_stats.add_latency(l);
                            global_total_stats.add_latency(l);
                        }
                        local_interval_lats.clear();
                        last_flush = op_end;
                    }
                }

                // flush remainder
                if (!local_interval_lats.empty()) {
                    std::lock_guard<std::mutex> lock(interval_mutex);
                    for (auto l : local_interval_lats) {
                        global_interval_stats.add_latency(l);
                        global_total_stats.add_latency(l);
                    }
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }
        auto phase_end_time = std::chrono::steady_clock::now();
        result.phases.push_back({
            std::chrono::duration<double>(phase_start_time - run_start_time).count(),
            std::chrono::duration<double>(phase_end_time - run_start_time).count(),
            static_cast<long long>(global_total_count.load() - phase_start_count),
            phase.read_pct, phase.write_pct, phase.scan_pct,
            phase.distribution == Distribution::ZIPFIAN ? "zipfian" : "uniform"
        });
    }

    auto end_time = std::chrono::steady_clock::now();
    is_running.store(false);
    sampler.join();
    double total_duration_s = std::chrono::duration<double>(end_time - run_start_time).count();

    // Disconnect workers
    for (auto& wa : worker_adapters) {
        wa->disconnect();
    }

    // Collect metrics from the base adapter
    MetricMap db_metrics = adapter_->collect_metrics();
    MetricMap process_io_after = process_io();
    adapter_->disconnect();

    // Comparable amplification requires physical I/O and live-data boundaries.
    // Leave these null; raw before/after counters are reported separately.
    AmplificationMetrics amp;

    result.latency_stats = global_total_stats.get_snapshot();
    result.throughput_ops = (total_duration_s > 0) ? (global_total_count.load() / total_duration_s) : 0;
    result.amplification = amp;
    result.db_metrics = db_metrics;
    result.db_metrics_before = db_metrics_before;
    result.process_io_before = process_io_before;
    result.process_io_after = process_io_after;
    auto counts = [&](int i) {
        OperationCounts c;
        c.attempted = attempted[i].load();
        c.successful = successful[i].load();
        c.failed = c.attempted - c.successful;
        return c;
    };
    result.reads = counts(0);
    result.writes = counts(1);
    result.scans = counts(2);
    result.logical_write_bytes = result.writes.successful * static_cast<long long>(options.payload_size);

    return result;
}

void Analyzer::save_json(const RunResult &result, const std::string &filename) {
  nlohmann::json j;

  j["latency_stats"] = {{"avg_us", result.latency_stats.avg},
                        {"p50_us", result.latency_stats.p50},
                        {"p95_us", result.latency_stats.p95},
                        {"p99_us", result.latency_stats.p99}};

  j["throughput_ops"] = result.throughput_ops;
  auto count_json = [](const OperationCounts& c) {
      return nlohmann::json{{"attempted", c.attempted}, {"successful", c.successful}, {"failed", c.failed}};
  };
  j["operation_counts"] = {{"read", count_json(result.reads)},
                           {"write", count_json(result.writes)},
                           {"scan", count_json(result.scans)}};
  j["logical_write_bytes"] = result.logical_write_bytes;

  nlohmann::json ts_array = nlohmann::json::array();
  for (const auto& pt : result.time_series) {
      ts_array.push_back({
          {"elapsed_time_s", pt.elapsed_time_s},
          {"throughput_ops", pt.throughput_ops},
          {"attempted", pt.attempted},
          {"successful", pt.successful},
          {"failed", pt.failed},
          {"latency_stats", {
              {"avg_us", pt.latency_stats.avg},
              {"p50_us", pt.latency_stats.p50},
              {"p95_us", pt.latency_stats.p95},
              {"p99_us", pt.latency_stats.p99}
          }}
      });
  }
  j["time_series"] = ts_array;
  j["phases"] = nlohmann::json::array();
  for (const auto& p : result.phases) {
      j["phases"].push_back({
          {"start_time_s", p.start_time_s}, {"end_time_s", p.end_time_s},
          {"attempted", p.attempted}, {"read_pct", p.read_pct},
          {"write_pct", p.write_pct}, {"scan_pct", p.scan_pct},
          {"distribution", p.distribution}
      });
  }

  j["amplification"] = {
      {"write_amp", result.amplification.write_amp ? nlohmann::json(*result.amplification.write_amp) : nlohmann::json(nullptr)},
      {"read_amp", result.amplification.read_amp ? nlohmann::json(*result.amplification.read_amp) : nlohmann::json(nullptr)},
      {"space_amp", result.amplification.space_amp ? nlohmann::json(*result.amplification.space_amp) : nlohmann::json(nullptr)}
  };

  j["db_metrics"] = nlohmann::json::object();
  j["db_metrics_before"] = nlohmann::json::object();
  for (const auto &[key, value] : result.db_metrics_before) {
    if (std::holds_alternative<std::nullptr_t>(value)) j["db_metrics_before"][key] = nullptr;
    else if (std::holds_alternative<long long>(value)) j["db_metrics_before"][key] = std::get<long long>(value);
    else if (std::holds_alternative<double>(value)) j["db_metrics_before"][key] = std::get<double>(value);
  }
  auto metric_json = [](const MetricMap& metrics) {
      nlohmann::json out = nlohmann::json::object();
      for (const auto &[key, value] : metrics) {
          if (std::holds_alternative<std::nullptr_t>(value)) out[key] = nullptr;
          else if (std::holds_alternative<long long>(value)) out[key] = std::get<long long>(value);
          else if (std::holds_alternative<double>(value)) out[key] = std::get<double>(value);
      }
      return out;
  };
  j["process_io_before"] = metric_json(result.process_io_before);
  j["process_io_after"] = metric_json(result.process_io_after);
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
