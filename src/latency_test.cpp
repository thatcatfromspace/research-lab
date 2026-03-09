// latency_test.cpp
// ─────────────────────────────────────────────────────────────────────────────
// Standalone latency benchmark CLI.
//
// Usage:
//   ./latency_test [options]
//
//   --db       <type>   Database backend (default: mysql)
//   --host     <host>   Hostname / IP  (default: 127.0.0.1)
//   --user     <user>   Username       (default: bench)
//   --password <pass>   Password       (default: benchpass)
//   --dbname   <name>   Database name  (default: bench)
//   --port     <port>   Port           (default: 3306 for MySQL)
//   --ops      <n>      Operations     (default: 10000)
//   --out      <file>   JSON output    (default: results_<db>.json)
//   --help              Show this help
//
// Example:
//   ./latency_test --db mysql --ops 20000
// ─────────────────────────────────────────────────────────────────────────────

#include "analyzer/Analyzer.hpp"
#include "analyzer/MySQLAdapter.hpp"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

// ─── Config ──────────────────────────────────────────────────────────────────

struct Config {
    std::string db_type  = "mysql";
    std::string host     = "127.0.0.1";
    std::string user     = "bench";
    std::string password = "benchpass";
    std::string dbname   = "bench";
    int         port     = 3306;
    std::size_t ops      = 10'000;
    std::string json_out = "";   // filled from db_type if left empty
};

static void print_usage(const char* prog) {
    std::cerr
        << "\nUsage: " << prog << " [options]\n\n"
        << "  --db       <type>   Database backend (default: mysql)\n"
        << "  --host     <host>   Host             (default: 127.0.0.1)\n"
        << "  --user     <user>   Username         (default: bench)\n"
        << "  --password <pass>   Password         (default: benchpass)\n"
        << "  --dbname   <name>   Database name    (default: bench)\n"
        << "  --port     <port>   Port             (default: 3306)\n"
        << "  --ops      <n>      Operations       (default: 10000)\n"
        << "  --out      <file>   JSON output file (default: results_<db>.json)\n"
        << "  --help              Print this help\n\n"
        << "Supported DB types: mysql\n\n";
}

static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc)
                throw std::invalid_argument(
                    std::string(flag) + " requires a value");
            return argv[++i];
        };

        std::string arg = argv[i];
        if      (arg == "--db")       cfg.db_type  = next("--db");
        else if (arg == "--host")     cfg.host     = next("--host");
        else if (arg == "--user")     cfg.user     = next("--user");
        else if (arg == "--password") cfg.password = next("--password");
        else if (arg == "--dbname")   cfg.dbname   = next("--dbname");
        else if (arg == "--port")     cfg.port     = std::stoi(next("--port"));
        else if (arg == "--ops")      cfg.ops      = std::stoull(next("--ops"));
        else if (arg == "--out")      cfg.json_out = next("--out");
        else if (arg == "--help")   { print_usage(argv[0]); std::exit(0); }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    if (cfg.json_out.empty())
        cfg.json_out = "results_" + cfg.db_type + ".json";

    return cfg;
}

// ─── Reporting ───────────────────────────────────────────────────────────────

static std::string db_label(const std::string& type) {
    if (type == "mysql")      return "MySQL 8.x";
    if (type == "postgresql") return "PostgreSQL";
    if (type == "rocksdb")    return "RocksDB";
    if (type == "leveldb")    return "LevelDB";
    if (type == "cassandra")  return "Cassandra";
    return type;
}

static void sep(int width = 54) {
    std::cout << std::string(width, '-') << "\n";
}

static void bar(int width = 54) {
    std::cout << std::string(width, '=') << "\n";
}

static void print_report(const Config& cfg,
                         const analyzer::RunResult& result) {
    const auto& s = result.latency_stats;

    std::cout << "\n";
    bar();
    std::cout << "  DB Latency Test  |  " << db_label(cfg.db_type) << "\n";
    bar();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Endpoint   : " << cfg.host << ":" << cfg.port
              << "  [" << cfg.dbname << "]\n";
    std::cout << "  Operations : " << cfg.ops << "\n";
    std::cout << "  Throughput : " << result.throughput_ops << " ops/s\n";

    sep();
    std::cout << "  Latency (microseconds)\n";
    sep();
    std::cout << "    avg  : " << std::setw(10) << std::setprecision(1)
              << s.avg  << " us\n";
    std::cout << "    p50  : " << std::setw(10) << s.p50  << " us\n";
    std::cout << "    p95  : " << std::setw(10) << s.p95  << " us\n";
    std::cout << "    p99  : " << std::setw(10) << s.p99  << " us\n";

    if (!result.db_metrics.empty()) {
        sep();
        std::cout << "  DB Metrics\n";
        sep();
        for (const auto& [k, v] : result.db_metrics) {
            std::cout << "    " << std::left << std::setw(42) << k << " : ";
            if (std::holds_alternative<long long>(v))
                std::cout << std::right << std::get<long long>(v);
            else if (std::holds_alternative<double>(v))
                std::cout << std::right << std::get<double>(v);
            else
                std::cout << "(null)";
            std::cout << "\n";
        }
    }

    bar();
    std::cout << "\n";
}

// ─── Adapter Factory ─────────────────────────────────────────────────────────

static std::unique_ptr<analyzer::DBAdapter>
make_adapter(const Config& cfg) {
    if (cfg.db_type == "mysql") {
        return std::make_unique<analyzer::MySQLAdapter>(
            cfg.host, cfg.user, cfg.password, cfg.dbname, cfg.port);
    }
    // Future: postgresql, rocksdb, leveldb, cassandra
    throw std::invalid_argument("Unsupported db type: '" + cfg.db_type +
                                "'  (supported: mysql)");
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    Config cfg;
    try {
        cfg = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Argument error: " << e.what() << "\n";
        return 1;
    }

    std::unique_ptr<analyzer::DBAdapter> adapter;
    try {
        adapter = make_adapter(cfg);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create adapter: " << e.what() << "\n";
        return 1;
    }

    analyzer::Analyzer bench(std::move(adapter));

    std::cout << "[latency_test] Connecting to " << cfg.db_type
              << " @ " << cfg.host << ":" << cfg.port << " ...\n";

    analyzer::RunResult result;
    try {
        result = bench.run(cfg.ops);
    } catch (const std::exception& e) {
        std::cerr << "Benchmark error: " << e.what() << "\n";
        return 1;
    }

    print_report(cfg, result);

    analyzer::Analyzer::save_json(result, cfg.json_out);
    std::cout << "JSON results saved to: " << cfg.json_out << "\n";

    return 0;
}
