#ifndef ANALYZER_METRIC_RESULT_HPP
#define ANALYZER_METRIC_RESULT_HPP

#include <variant>
#include <map>
#include <string>
#include <vector>
#include <optional>

namespace analyzer {

// MetricValue can be an integer, a double, or null (if unsupported/missing)
using MetricValue = std::variant<std::nullptr_t, long long, double>;

// MetricMap allows arbitrary string keys with MetricValues
using MetricMap = std::map<std::string, MetricValue>;

// Snapshot aggregates statistical properties of a metric distribution
struct Snapshot {
    double avg;
    long long p50;
    long long p95;
    long long p99;
};

struct AmplificationMetrics {
    std::optional<double> write_amp;
    std::optional<double> read_amp;
    std::optional<double> space_amp;
};

struct TimeSeriesPoint {
    double elapsed_time_s;
    Snapshot latency_stats;
    double throughput_ops;
    long long attempted = 0;
    long long successful = 0;
    long long failed = 0;
};

struct OperationCounts {
    long long attempted = 0;
    long long successful = 0;
    long long failed = 0;
};

struct PhaseResult {
    double start_time_s = 0;
    double end_time_s = 0;
    long long attempted = 0;
    int read_pct = 0;
    int write_pct = 0;
    int scan_pct = 0;
    std::string distribution;
};

// RunResult contains the final output of an analysis run
struct RunResult {
    // Client-side observed latency statistics (in microseconds)
    Snapshot latency_stats;

    // Throughput (operations per second)
    double throughput_ops;

    // Time-series data collected during the run
    std::vector<TimeSeriesPoint> time_series;
    std::vector<PhaseResult> phases;

    OperationCounts reads, writes, scans;
    long long logical_write_bytes = 0;
    MetricMap db_metrics_before;
    MetricMap process_io_before, process_io_after;

    // Standardized amplification metrics (computed from adapter-specific ones if available)
    AmplificationMetrics amplification;

    // Metrics collected from the DB adapter at the end of the run
    MetricMap db_metrics;
};

} // namespace analyzer

#endif // ANALYZER_METRIC_RESULT_HPP
