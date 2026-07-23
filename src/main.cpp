#include "analyzer/Analyzer.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace analyzer {

class StubAdapter : public DBAdapter {
public:
    std::unique_ptr<DBAdapter> clone_connection() override {
        return std::make_unique<StubAdapter>();
    }

    void connect() override {
        std::cout << "[Stub] Connecting..." << std::endl;
    }

    void configure(int, int) override {
        // No-op for stub
    }

    void perform_read(int) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    void perform_write(int, const std::string&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    void perform_scan(int, int) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    MetricMap collect_metrics() override {
        MetricMap m;
        m["stub.simulated_reads"] = 1000LL;
        m["stub.cache_hit_rate"] = 0.95;
        m["stub.unsupported_metric"] = nullptr;
        return m;
    }

    void disconnect() override {
        std::cout << "[Stub] Disconnecting..." << std::endl;
    }
};

} // namespace analyzer

int main() {
    std::cout << "Starting DB Analyzer..." << std::endl;

    auto adapter = std::make_unique<analyzer::StubAdapter>();
    analyzer::Analyzer analyzer(std::move(adapter));

    // Run 1000 operations
    std::cout << "Running workload..." << std::endl;
    analyzer::RunOptions options;
    analyzer::Phase p;
    p.operation_count = 1000;
    p.read_pct = 80;
    p.write_pct = 20;
    options.phases.push_back(p);
    auto result = analyzer.run(options);

    // Save results
    std::string filename = "output.json";
    analyzer::Analyzer::save_json(result, filename);
    std::cout << "Analysis complete. Results saved to " << filename << std::endl;

    return 0;
}
