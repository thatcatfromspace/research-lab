#ifndef ANALYZER_ANALYZER_HPP
#define ANALYZER_ANALYZER_HPP

#include "analyzer/DBAdapter.hpp"
#include "analyzer/Stats.hpp"
#include "analyzer/MetricResult.hpp"
#include <memory>
#include <string>

namespace analyzer {

class Analyzer {
public:
    Analyzer(std::unique_ptr<DBAdapter> adapter);

    // Run the analysis for a specified number of operations
    RunResult run(size_t operation_count);

    // Save results to a file (JSON format)
    static void save_json(const RunResult& result, const std::string& filename);

private:
    std::unique_ptr<DBAdapter> adapter_;
    Stats stats_;
};

} // namespace analyzer

#endif // ANALYZER_ANALYZER_HPP
