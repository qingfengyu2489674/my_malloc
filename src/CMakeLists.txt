# src/CMakeLists.txt

# 找到当前目录下所有的 .c 和 .cpp 源文件
file(GLOB_RECURSE lib_sources 
    "*.c"
    "*.cpp"
)

# 创建 my_malloc 库
add_library(my_malloc STATIC ${lib_sources})

# -------------------- 这是核心逻辑 --------------------
# 检查在顶层 CMakeLists.txt 中定义的全局开关
if(MY_MALLOC_ENABLE_TESTING)
    # 如果开关是 ON，就为 my_malloc 库自身添加编译定义
    # 这将确保测试“后门”在库代码中被正确编译
    target_compile_definitions(my_malloc PUBLIC MY_MALLOC_TESTING)
endif()
# ----------------------------------------------------

# 为 my_malloc 库设置 include 目录
# PUBLIC：任何链接到 my_malloc 的目标都将自动获得这个 include 路径
target_include_directories(my_malloc PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # 注意路径是 ../include
)