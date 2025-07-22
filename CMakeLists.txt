# CMakeLists.txt (Top-Level)

# 1. 项目基础设置
cmake_minimum_required(VERSION 3.14)
project(my_malloc C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 保持这个设置，它对 IDE 支持非常有帮助
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)



# ====================================================================
#  在这里添加全局编译选项
# ====================================================================

# --- 通用编译警告选项 (推荐所有配置都开启) ---
# 这些选项会帮助你写出更健壮、更安全的代码
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG)
    # add_compile_options() 会将选项应用到所有目标
    add_compile_options(
        -Wall                   # 开启大部分警告
        -Wextra                 # 开启额外的警告
        -Wpedantic              # 遵循严格的 ISO C++ 标准
        -Wno-pedantic           # 禁用关于非标准语法扩展的警告 (针对语句表达式)
        -Wnon-virtual-dtor      # 警告基类缺少虚析构函数
        -Wold-style-cast        # 警告 C 风格的类型转换
        -Wcast-align            # 警告可能导致对齐问题的指针转换
        -Wunused                # 警告未使用的变量/参数
        -Woverloaded-virtual    # 警告派生类中的虚函数重载
    )
endif()


# --- 按构建类型 (Build Type) 设置特定选项 ---
# 这样你可以通过 cmake -DCMAKE_BUILD_TYPE=Debug 来切换配置
# set(CMAKE_CXX_FLAGS_DEBUG "-g -O0") 这种方式更常用，且更灵活

# 为 Debug 构建添加调试信息和关闭优化
set(CMAKE_C_FLAGS_DEBUG "-g -O0" CACHE STRING "Flags used by the C compiler for debug builds." FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0" CACHE STRING "Flags used by the C++ compiler for debug builds." FORCE)

# 为 Release 构建开启优化
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Flags used by the C compiler for release builds." FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Flags used by the C++ compiler for release builds." FORCE)

# ====================================================================
#  全局编译选项设置结束
# ====================================================================


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