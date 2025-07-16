#include "unity.h"

// 声明我们将在另一个文件中定义的测试组运行函数
void run_simple_add_tests(void);


void setUp(void) {
    // 这个函数会在每个测试用例运行前被调用
    // 你可以在这里放置准备代码，比如分配内存
}

void tearDown(void) {
    // 这个函数会在每个测试用例运行后被调用
    // 你可以在这里放置清理代码，比如释放内存
}

// 测试主函数
int main(void) {
    UNITY_BEGIN(); // 初始化 Unity

    RUN_TEST(run_simple_add_tests); // 运行 "simple_add" 的测试组

    return UNITY_END(); // 结束并报告结果
}