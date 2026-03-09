#include "analyzer/Analyzer.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace analyzer {

class StubAdapter : public DBAdapter {
public:
    void connect() override {
        std::cout << "[Stub] Connecting..." << std::endl;
    }

    void perform_op() override {
        // Simulate work (1ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    auto result = analyzer.run(1000);

    // Save results
    std::string filename = "output.json";
    analyzer::Analyzer::save_json(result, filename);
    std::cout << "Analysis complete. Results saved to " << filename << std::endl;

    return 0;
}
