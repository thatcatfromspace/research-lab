cmake_minimum_required(VERSION 3.14)
project(db_analyzer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── System header lookup ───────────────────────────────────────────────────────
find_package(nlohmann_json REQUIRED)

include_directories(include)

# Linux & macOS Header Paths
if(APPLE)
    include_directories(/opt/homebrew/include /opt/homebrew/opt/libpq/include /opt/homebrew/opt/mariadb/include/mysql)
    link_directories(/opt/homebrew/lib /opt/homebrew/opt/libpq/lib /opt/homebrew/opt/mariadb/lib)
else()
    include_directories(/usr/include/postgresql /usr/include/mysql /usr/include/mariadb /usr/include/mariadb/mysql)
endif()
# ── Core analysis engine (no DB-specific deps) ────────────────────────────────
add_library(analyzer_core STATIC
    src/Analyzer.cpp
)
target_link_libraries(analyzer_core
    PUBLIC nlohmann_json::nlohmann_json
)

# ── MySQL adapter ─────────────────────────────────────────────────────────────
# Requires: mariadb-libs (AUR) — provides libmariadb.so + mysql/mysql.h
add_library(adapter_mysql STATIC
    src/MySQLAdapter.cpp
)
target_link_libraries(adapter_mysql
    PUBLIC  analyzer_core
    PRIVATE mariadb
)

# ── PostgreSQL adapter ────────────────────────────────────────────────────────
# Requires: postgresql-libs — provides libpq + libpq-fe.h
add_library(adapter_postgres STATIC
    src/PostgreSQLAdapter.cpp
)
target_link_libraries(adapter_postgres
    PUBLIC  analyzer_core
    PRIVATE pq
)

# ── RocksDB adapter ───────────────────────────────────────────────────────────
# Requires: rocksdb
add_library(adapter_rocksdb STATIC
    src/RocksDBAdapter.cpp
)
target_link_libraries(adapter_rocksdb
    PUBLIC  analyzer_core
    PRIVATE rocksdb
)

# ── Cassandra adapter ────────────────────────────────────────────────────────
# Requires: cassandra-cpp-driver
add_library(adapter_cassandra STATIC
    src/CassandraAdapter.cpp
)
target_link_libraries(adapter_cassandra
    PUBLIC  analyzer_core
    PRIVATE cassandra
)

# ── LevelDB adapter ───────────────────────────────────────────────────────────
# Requires: leveldb
add_library(adapter_leveldb STATIC
    src/LevelDBAdapter.cpp
)
target_link_libraries(adapter_leveldb
    PUBLIC  analyzer_core
    PRIVATE leveldb
)

# ── Latency test binary ───────────────────────────────────────────────────────
# Usage: ./latency_test --db mysql|postgresql|rocksdb|cassandra|leveldb --ops 10000
add_executable(latency_test
    src/latency_test.cpp
)
target_link_libraries(latency_test
    PRIVATE adapter_mysql
    PRIVATE adapter_postgres
    PRIVATE adapter_rocksdb
    PRIVATE adapter_cassandra
    PRIVATE adapter_leveldb
)

# ── Legacy stub binary ────────────────────────────────────────────────────────
add_executable(db_analyzer
    src/main.cpp
)
target_link_libraries(db_analyzer
    PRIVATE analyzer_core
)

