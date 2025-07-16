# 1. 项目基础设置
cmake_minimum_required(VERSION 3.14)

# 将项目语言设为 C++。CXX 是 CMake 为避免特殊字符和歧义而使用的标准名称。
project(my_malloc_project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. 定义你的 my_malloc 库
# ------------------------------------------
# 添加一个空的源文件来“欺骗”CMake
add_library(my_malloc_lib src/dummy.cpp) 

# 设置头文件目录。${PROJECT_SOURCE_DIR} 指向项目根目录。
# 这使得我们可以使用 #include <my_malloc/sys/mman.hpp> 这种方式包含头文件。
target_include_directories(my_malloc_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)

# 3. 配置 Google Test
# 开启测试功能
enable_testing()

# 包含 Google Test 的子目录
add_subdirectory(third_party/googletest)

# 4. 创建测试可执行文件
#    注意，这里是 .cpp 测试文件
add_executable(run_tests tests/test_mman.cpp)

# 5. 链接你的库和 Google Test 到测试程序
#    链接 gtest_main，它包含了 gtest 和一个现成的 main() 函数
target_link_libraries(run_tests PRIVATE my_malloc_lib gtest_main)

# 6. 将测试可执行文件注册到 CTest
add_test(NAME MmanUnitTests COMMAND run_tests)