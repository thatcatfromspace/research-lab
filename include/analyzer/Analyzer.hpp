#ifndef ANALYZER_ANALYZER_HPP
#define ANALYZER_ANALYZER_HPP

#include "analyzer/DBAdapter.hpp"
#include "analyzer/Stats.hpp"
#include "analyzer/MetricResult.hpp"
#include <memory>
#include <string>

namespace analyzer {

struct RunOptions {
    size_t operation_count = 0;   // 0 = use duration
    size_t duration_seconds = 0; // 0 = use operation count
};

class Analyzer {
public:
    Analyzer(std::unique_ptr<DBAdapter> adapter);

    // Run the analysis based on operations or time
    RunResult run(const RunOptions& options);

    // Save results to a file (JSON format)
    static void save_json(const RunResult& result, const std::string& filename);

private:
    std::unique_ptr<DBAdapter> adapter_;
    Stats stats_;
};

} // namespace analyzer

#endif // ANALYZER_ANALYZER_HPP
