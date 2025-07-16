# 1. 项目基础设置
cmake_minimum_required(VERSION 3.14)

# 将项目语言设为 C/C++。因为你的 src 目录可能有 .c 文件
project(my_malloc C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# --- 2. 定义你的 my_malloc 库 (自动化版本) ---

# 找到 src/ 目录下所有的 .c 和 .cpp 源文件
# GLOB_RECURSE 会递归地查找所有子目录
file(GLOB_RECURSE lib_sources 
    "src/*.c"
    "src/*.cpp"
)

# 检查是否找到了任何源文件
if(lib_sources)
    # 如果找到了源文件，就创建一个静态库 (STATIC) 或动态库 (SHARED)
    # 静态库通常更简单，我们先用静态库
    add_library(my_malloc STATIC ${lib_sources})

    # 将 include 目录设为 PUBLIC，这样链接到 my_malloc 的目标
    # 会自动获得这个 include 路径
    target_include_directories(my_malloc PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
else()
    # 如果 src/ 目录是空的，或者只有头文件，
    # 我们就退回到创建一个 INTERFACE 库
    add_library(my_malloc INTERFACE)
    target_include_directories(my_malloc INTERFACE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
endif()


# --- 3. 配置 Google Test ---
# 开启测试功能
enable_testing()

# 检查 submodule 是否存在
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest/CMakeLists.txt")
    message(FATAL_ERROR "GoogleTest submodule not found. Please run 'git submodule update --init --recursive'")
endif()

# 包含 Google Test 的子目录
# 设置 EXCLUDE_FROM_ALL 避免在 "make" 时编译 gtest 自身的测试
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)


# --- 4. 处理测试 ---
# 将测试的构建逻辑委托给 tests/CMakeLists.txt
add_subdirectory(tests)