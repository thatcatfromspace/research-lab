cmake_minimum_required(VERSION 3.10)
project(db_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_library(analyzer_lib
    src/Analyzer.cpp
    src/MySQLAdapter.cpp
    src/PostgreSQLAdapter.cpp
    src/RocksDBAdapter.cpp
    src/LevelDBAdapter.cpp
    src/CassandraAdapter.cpp
    src/ScyllaDBAdapter.cpp
)

target_link_libraries(analyzer_lib mysqlclient pq rocksdb leveldb cassandra)

add_executable(db_analyzer
    src/main.cpp
)

target_link_libraries(db_analyzer analyzer_lib)
