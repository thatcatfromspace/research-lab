#include "analyzer/Analyzer.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace analyzer {

Analyzer::Analyzer(std::unique_ptr<DBAdapter> adapter)
    : adapter_(std::move(adapter)) {}

RunResult Analyzer::run(size_t operation_count) {
  adapter_->connect();

  auto start_time = std::chrono::steady_clock::now();

  for (size_t i = 0; i < operation_count; ++i) {
    auto op_start = std::chrono::steady_clock::now();
    adapter_->perform_op();
    auto op_end = std::chrono::steady_clock::now();

    auto latency_us =
        std::chrono::duration_cast<std::chrono::microseconds>(op_end - op_start)
            .count();
    stats_.add_latency(latency_us);
  }

  auto end_time = std::chrono::steady_clock::now();

  // Calculate total duration in seconds for throughput
  double total_duration_s =
      std::chrono::duration<double>(end_time - start_time).count();

  // Collect DB-specific metrics
  MetricMap db_metrics = adapter_->collect_metrics();
  adapter_->disconnect();

  RunResult result;
  result.latency_stats = stats_.get_snapshot();
  result.throughput_ops = operation_count / total_duration_s;
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

  ofs << j.dump(2); // Pretty print with 2-space indent
}

} // namespace analyzer
