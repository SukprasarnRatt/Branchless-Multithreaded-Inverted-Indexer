cmake_minimum_required(VERSION 3.10)

project(BranchlessMultithreadedInvertedIndexer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(tokenizer
    src/main.cpp
    src/tokenizer.cpp
)

add_executable(search
    src/search.cpp
)

target_include_directories(tokenizer PRIVATE include)
target_include_directories(search PRIVATE include)

target_compile_options(tokenizer PRIVATE -O3 -march=native)
target_compile_options(search PRIVATE -O3 -march=native)

target_link_libraries(tokenizer
    PRIVATE
    Threads::Threads
    numa
)

target_link_libraries(search
    PRIVATE
    Threads::Threads
)