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
    double write_amp = 0.0;
    double read_amp = 0.0;
    double space_amp = 0.0;
};

struct TimeSeriesPoint {
    double elapsed_time_s;
    Snapshot latency_stats;
    double throughput_ops;
};

// RunResult contains the final output of an analysis run
struct RunResult {
    // Client-side observed latency statistics (in microseconds)
    Snapshot latency_stats;
    
    // Throughput (operations per second)
    double throughput_ops;

    // Time-series data collected during the run
    std::vector<TimeSeriesPoint> time_series;

    // Standardized amplification metrics (computed from adapter-specific ones if available)
    AmplificationMetrics amplification;

    // Metrics collected from the DB adapter at the end of the run
    MetricMap db_metrics;
};

} // namespace analyzer

#endif // ANALYZER_METRIC_RESULT_HPP
