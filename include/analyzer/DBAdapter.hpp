#ifndef ANALYZER_DB_ADAPTER_HPP
#define ANALYZER_DB_ADAPTER_HPP

#include "analyzer/MetricResult.hpp"
#include <string>
#include <memory>

namespace analyzer {

// Abstract base class for database adapters
class DBAdapter {
public:
    virtual ~DBAdapter() = default;
    
    // Create a new instance of this adapter for a worker thread
    virtual std::unique_ptr<DBAdapter> clone_connection() = 0;

    // Establish connection to the database
    virtual void connect() = 0;

    // Configure workload parameters before setup (optional)
    virtual void configure(int read_pct, int row_count) = 0;

    // Execute a read operation for a specific key
    virtual void perform_read(int key) = 0;

    // Execute a write operation (insert/update) for a specific key
    virtual void perform_write(int key, const std::string& value) = 0;

    // Execute a scan operation starting from a key
    virtual void perform_scan(int start_key, int count) = 0;

    // Collect database-specific metrics
    virtual MetricMap collect_metrics() = 0;

    // Close connection and cleanup
    virtual void disconnect() = 0;
};

} // namespace analyzer

#endif // ANALYZER_DB_ADAPTER_HPP
