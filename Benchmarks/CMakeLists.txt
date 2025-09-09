cmake_minimum_required(VERSION 3.14)
project(benchmarks CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)
find_package(benchmark REQUIRED) # needs benchmark installed (or a prefix path) 
add_executable(doublebuf_bench Double_Buffer_Mutex.cpp)
target_link_libraries(doublebuf_bench PRIVATE benchmark::benchmark benchmark::benchmark_main pthread)
