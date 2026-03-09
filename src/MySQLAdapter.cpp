#include "analyzer/MySQLAdapter.hpp"
#include <mysql/mysql.h>
#include <stdexcept>
#include <iostream>

namespace analyzer {

MySQLAdapter::MySQLAdapter(const std::string& host, const std::string& user, 
                           const std::string& password, const std::string& dbname, 
                           int port)
    : host_(host), user_(user), password_(password), dbname_(dbname), port_(port), conn_(nullptr) {}

MySQLAdapter::~MySQLAdapter() {
    disconnect();
}

void MySQLAdapter::connect() {
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        throw std::runtime_error("mysql_init failed");
    }

    if (!mysql_real_connect(conn_, host_.c_str(), user_.c_str(), password_.c_str(), 
                            dbname_.c_str(), port_, nullptr, 0)) {
        std::string err = mysql_error(conn_);
        mysql_close(conn_);
        conn_ = nullptr;
        throw std::runtime_error("mysql_real_connect failed: " + err);
    }
}

void MySQLAdapter::perform_op() {
    if (!conn_) return;
    
    // Simulate a simple point query
    if (mysql_query(conn_, "SELECT 1")) {
        // On error, we just return. Real implementation might track error rates.
        return;
    }
    
    MYSQL_RES* res = mysql_store_result(conn_);
    if (res) {
        mysql_free_result(res);
    }
}

MetricMap MySQLAdapter::collect_metrics() {
    MetricMap metrics;
    if (!conn_) return metrics;

    if (mysql_query(conn_, "SHOW GLOBAL STATUS")) {
        return metrics;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) return metrics;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        if (row[0] && row[1]) {
            std::string key = row[0];
            std::string val_str = row[1];
            
            // Extract a subset of known interesting metrics
            if (key == "Threads_connected" || key == "Questions" || key == "Slow_queries") {
                try {
                    metrics["mysql." + key] = std::stoll(val_str);
                } catch (...) {
                    metrics["mysql." + key] = nullptr;
                }
            }
        }
    }
    mysql_free_result(res);
    
    // Explicitly add an unsupported metric as required by constraints
    metrics["mysql.unsupported_feature_metric"] = nullptr;

    return metrics;
}

void MySQLAdapter::disconnect() {
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

} // namespace analyzer
