cmake_minimum_required(VERSION 3.14)
project(db_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── System header lookup ───────────────────────────────────────────────────────
find_package(nlohmann_json REQUIRED)

include_directories(include)

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
    PRIVATE mariadb          # libmariadb.so from mariadb-libs
)

# ── Latency test binary ───────────────────────────────────────────────────────
# Usage: ./latency_test --db mysql --ops 10000
add_executable(latency_test
    src/latency_test.cpp
)
target_link_libraries(latency_test
    PRIVATE adapter_mysql
)

# ── Legacy stub binary ────────────────────────────────────────────────────────
add_executable(db_analyzer
    src/main.cpp
)
target_link_libraries(db_analyzer
    PRIVATE analyzer_core
)
