# 递归地查找所有子目录中符合 "test_*.cpp" 模式的源文件
file(GLOB_RECURSE test_sources "test_*.cpp")

# 遍历找到的每一个测试文件
foreach(test_source ${test_sources})
    # 从完整路径中提取出文件名（不含扩展名），作为测试目标的名字
    get_filename_component(test_name ${test_source} NAME_WE)

    # 为这个测试文件创建一个可执行程序
    add_executable(${test_name} ${test_source})

    # ========================== 这是最终的修改 ==========================
    # 同样检查在顶层定义的全局测试开关是否被打开
    if(MY_MALLOC_ENABLE_TESTING)
        # 如果是，就为当前的测试目标也添加这个编译定义。
        # 这确保了测试文件本身（如 test_ThreadHeap_destructor.cpp）
        # 在编译时能看到 #if defined(MY_MALLOC_TESTING) 里的函数声明。
        target_compile_definitions(${test_name} PRIVATE MY_MALLOC_TESTING)
    endif()
    # =================================================================

    # 将我们的库 (my_malloc) 和 GoogleTest (gtest_main) 链接到测试程序
    target_link_libraries(${test_name} PRIVATE 
        my_malloc
        gtest_main
    )

    # 将这个可执行程序注册为一个 CTest 测试
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()