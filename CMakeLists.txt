cmake_minimum_required(VERSION 3.15)
project(TitanDB LANGUAGES CXX)

add_executable(titandb_server src/main.cpp src/net/epoll_server.cpp)

# Best Practice: Modern CMake Target Configurations
target_compile_features(titandb_server PRIVATE cxx_std_20)
target_compile_options(titandb_server PRIVATE -Wall -Wextra -Wpedantic -O3 -flto)
target_include_directories(titandb_server PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(titandb_server PRIVATE Threads::Threads)
