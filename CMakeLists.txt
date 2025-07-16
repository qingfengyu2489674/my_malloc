cmake_minimum_required(VERSION 3.10)
project(mymalloc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 开启测试框架
enable_testing()

# 添加头文件目录 (这里使用 target_include_directories 是更好的现代实践)
# include_directories(include) 

# 构建 mymalloc 静态库
# !! 确保你的源文件被添加进来 !!
add_library(mymalloc STATIC 
    src/mymalloc.c
    # src/chunk.c, etc.
)
# 让 mymalloc 库知道自己的头文件在哪里
target_include_directories(mymalloc PUBLIC include)


# ------------------- 测试目标 -------------------
# 包含 Unity 框架
add_subdirectory(tests/unity) 

# 创建测试可执行文件
# !! 把所有测试源文件都加进来 !!
add_executable(run_tests
    tests/test_main.c
    tests/test_simple_add.c
)

# 链接 mymalloc 库和 Unity 框架到测试程序
target_link_libraries(run_tests PRIVATE mymalloc unity)

# 添加测试到 CTest
add_test(NAME simple_add_test COMMAND run_tests)