#ifndef ANALYZER_DB_ADAPTER_HPP
#define ANALYZER_DB_ADAPTER_HPP

#include "analyzer/MetricResult.hpp"
#include <string>

namespace analyzer {

// Abstract base class for database adapters
class DBAdapter {
public:
    virtual ~DBAdapter() = default;

    // Establish connection to the database
    virtual void connect() = 0;

    // Configure workload parameters before setup (optional)
    virtual void configure(int read_pct, int row_count) = 0;

    // Execute a workload operation (e.g., a simple read or write)
    virtual void perform_op() = 0;

    // Collect database-specific metrics
    virtual MetricMap collect_metrics() = 0;

    // Close connection and cleanup
    virtual void disconnect() = 0;
};

} // namespace analyzer

#endif // ANALYZER_DB_ADAPTER_HPP
