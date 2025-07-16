#include "unity.h"
#include "mymalloc.h" // 包含我们要测试的函数的头文件

// 这是我们的第一个测试用例
void test_the_adder_should_add_two_numbers(void) {
    // 使用 Unity 的断言宏来判断结果是否正确
    // "期望值是 5，实际值是 simple_add(2, 3) 的结果"
    TEST_ASSERT_EQUAL_INT(5, simple_add(2, 3));
}

// 这是 "测试组运行函数"，它会运行这个文件里的所有测试用例
// 它的名字必须和 test_main.c 里的声明一致
void run_simple_add_tests(void) {
    RUN_TEST(test_the_adder_should_add_two_numbers);
    // 如果你还有其他测试用例，在这里继续添加 RUN_TEST(...)
}