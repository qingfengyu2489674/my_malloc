# 1. 指定 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 声明项目名称和语言
project(mymalloc_project C)

# 3. 添加一个可执行文件目标
#    命令: add_executable(<可执行文件名> <源文件1> <源文件2> ...)
add_executable(my_app main.c)