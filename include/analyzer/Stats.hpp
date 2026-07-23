#ifndef ANALYZER_STATS_HPP
#define ANALYZER_STATS_HPP

#include "analyzer/MetricResult.hpp"
#include <vector>
#include <algorithm>
#include <numeric>

namespace analyzer {

class Stats {
public:
    void add_latency(long long latency_us) {
        latencies_.push_back(latency_us);
    }

    Snapshot get_snapshot() {
        Snapshot s;
        if (latencies_.empty()) {
            s.avg = 0;
            s.p50 = 0;
            s.p95 = 0;
            s.p99 = 0;
            return s;
        }

        // Calculate average
        double sum = std::accumulate(latencies_.begin(), latencies_.end(), 0.0);
        s.avg = sum / latencies_.size();

        // Calculate percentiles
        // We sort the vector to get exact percentiles. 
        // Note: This modifies the internal storage order.
        std::sort(latencies_.begin(), latencies_.end());

        s.p50 = get_percentile(50);
        s.p95 = get_percentile(95);
        s.p99 = get_percentile(99);

        return s;
    }

    size_t count() const {
        return latencies_.size();
    }

    void clear() {
        latencies_.clear();
    }

private:
    std::vector<long long> latencies_;

    long long get_percentile(double p) {
        if (latencies_.empty()) return 0;
        size_t idx = static_cast<size_t>(p / 100.0 * latencies_.size());
        if (idx >= latencies_.size()) idx = latencies_.size() - 1;
        return latencies_[idx];
    }
};

} // namespace analyzer

#endif // ANALYZER_STATS_HPP
