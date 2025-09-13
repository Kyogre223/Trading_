cmake_minimum_required(VERSION 3.28.3)
project (feeds LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)


# Warnings & opts
add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-psabi
        -fno-exceptions -fno-rtti          # hot path friendly
        -fno-omit-frame-pointer            # keep for perf profiling
        -march=native -O3
)
file(GLOB_RECURSE TRADER_SRC CONFIGURE_DEPENDS
        src/app/*.cpp src/hot/*.cpp src/cold/*.cpp src/util/*.cpp)



add_executable(feeds ${TRADER_SRC})