
file(GLOB_RECURSE lib_sources 
    "*.c"
    "*.cpp"
)

add_library(my_malloc STATIC ${lib_sources})


target_include_directories(my_malloc PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # 注意路径是 ../include
)