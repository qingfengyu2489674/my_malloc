# tests/CMakeLists.txt

# 递归地查找所有子目录中符合 "test_*.cpp" 模式的源文件
file(GLOB_RECURSE test_sources "test_*.cpp")

# 遍历找到的每一个测试文件
foreach(test_source ${test_sources})
    # 从完整路径中提取出文件名（不含扩展名），作为测试目标的名字
    get_filename_component(test_name ${test_source} NAME_WE)

    # 为这个测试文件创建一个可执行程序
    add_executable(${test_name} ${test_source})

    # --- if() 语句块已经被移除，不再需要 ---

    # 将我们的库 (my_malloc) 和 GoogleTest (gtest_main) 链接到测试程序
    target_link_libraries(${test_name} PRIVATE 
        my_malloc
        gtest_main
    )

    # 将这个可执行程序注册为一个 CTest 测试
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()