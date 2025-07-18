# src/CMakeLists.txt

# 找到当前目录下所有的 .c 和 .cpp 源文件
file(GLOB_RECURSE lib_sources 
    "*.c"
    "*.cpp"
)

# 创建 my_malloc 库
add_library(my_malloc STATIC ${lib_sources})

# --- if() 语句块已经被移除，不再需要 ---

# 为 my_malloc 库设置 include 目录
# PUBLIC：任何链接到 my_malloc 的目标都将自动获得这个 include 路径
target_include_directories(my_malloc PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # 注意路径是 ../include
)