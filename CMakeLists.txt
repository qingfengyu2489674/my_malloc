# CMakeLists.txt (Top-Level)

# 1. 项目基础设置
cmake_minimum_required(VERSION 3.14)
project(my_malloc C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 保持这个设置，它对 IDE 支持非常有帮助
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- option() 已经被移除，不再需要 ---

# 2. 将 src 目录的构建任务委托给 src/CMakeLists.txt
add_subdirectory(src)

# 3. 配置 Google Test
enable_testing()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest/CMakeLists.txt")
    message(FATAL_ERROR "GoogleTest submodule not found. Please run 'git submodule update --init --recursive'")
endif()

add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

# 4. 将 tests 目录的构建任务委托给 tests/CMakeLists.txt
add_subdirectory(tests)