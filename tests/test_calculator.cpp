#include "gtest/gtest.h"
#include "calculator.hpp"

// 测试 Calculator 类的 add 方法
TEST(CalculatorTest, HandlesPositiveNumbers) {
    Calculator calc;
    EXPECT_EQ(calc.add(2, 3), 5);
}

TEST(CalculatorTest, HandlesNegativeNumbers) {
    Calculator calc;
    EXPECT_EQ(calc.add(-2, -3), -5);
}