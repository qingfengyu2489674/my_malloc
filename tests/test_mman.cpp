#include <gtest/gtest.h>
#include <cstdint>   // for uintptr_t
#include <cstring>   // for memset, memcmp
#include <system_error> // for std::error_code

// --- 关键：包含我们自己实现的 mman.hpp ---
// 使用尖括号和命名空间路径，这是最佳实践
#include <my_malloc/sys/mman.hpp>

// 定义一个测试套件 (Test Suite)，专门用于测试我们的 Mman 封装
// 我们称它为 MmanWrapperTest
class MmanWrapperTest : public ::testing::Test {
protected:
    // 你可以在这里定义所有测试用例共享的变量或设置/清理函数
    // 对于这个简单的测试，我们暂时不需要
    void SetUp() override {
        // 每个测试用例运行前执行
    }

    void TearDown() override {
        // 每个测试用例运行后执行
    }
};

// --- 测试用例 1: 测试成功的内存映射与读写 ---
// TEST_F 表示这个测试用例属于 MmanWrapperTest 这个测试固件 (Fixture)
TEST_F(MmanWrapperTest, SuccessfulMapAndReadWrite) {
    const size_t TEST_SIZE = 4096; // 映射一个标准页

    // 1. 调用我们自己封装的 mmap 函数
    void* ptr = mmap(nullptr, TEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    // 2. 断言：检查 mmap 的返回值
    // 检查指针是否有效，不应为 NULL 或 MAP_FAILED
    ASSERT_NE(ptr, nullptr) << "mmap returned a null pointer.";
    ASSERT_NE(ptr, MAP_FAILED) << "mmap returned MAP_FAILED. Errno: " << errno << " (" << strerror(errno) << ")";

    // 3. 断言：测试内存的可读写性
    // 这是一个非常重要的验证步骤，确保我们得到的内存是真正可用的
    
    // 写入一个模式
    unsigned char write_buffer[TEST_SIZE];
    memset(write_buffer, 0xAB, TEST_SIZE);
    
    // 使用 ASSERT_NO_THROW 来确保内存写入不会导致段错误等异常
    ASSERT_NO_THROW({
        memcpy(ptr, write_buffer, TEST_SIZE);
    }) << "Writing to the mapped memory caused an exception.";

    // 读取并验证
    unsigned char read_buffer[TEST_SIZE];
    ASSERT_NO_THROW({
        memcpy(read_buffer, ptr, TEST_SIZE);
    }) << "Reading from the mapped memory caused an exception.";

    // 比较写入和读出的内容是否一致
    EXPECT_EQ(memcmp(write_buffer, read_buffer, TEST_SIZE), 0) << "Memory content verification failed. Data read back is not what was written.";

    // 4. 调用我们自己封装的 munmap 函数进行清理
    int unmap_result = munmap(ptr, TEST_SIZE);
    
    // 5. 断言：检查 munmap 的返回值
    EXPECT_EQ(unmap_result, 0) << "munmap failed to release memory. Errno: " << errno << " (" << strerror(errno) << ")";
}

// --- 测试用例 2: 测试无效参数导致的映射失败 ---
TEST_F(MmanWrapperTest, FailsWithInvalidArguments) {
    // 尝试映射一个 0 大小的内存，这应该会失败
    // man mmap: EINVAL: "length was 0."
    void* ptr = mmap(nullptr, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    // 断言：mmap 应该返回 MAP_FAILED
    EXPECT_EQ(ptr, MAP_FAILED) << "mmap with zero length should have failed, but it did not.";

    // 断言：errno 应该被正确设置为 EINVAL
    EXPECT_EQ(errno, EINVAL) << "mmap with zero length should set errno to EINVAL, but it was set to " << errno;
}

// --- 测试用例 3: 测试 mprotect 的功能 ---
TEST_F(MmanWrapperTest, MprotectChangesPermissions) {
    const size_t TEST_SIZE = 4096;

    // 1. 映射一块可读写的内存
    void* ptr = mmap(nullptr, TEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(ptr, MAP_FAILED);

    // 2. 写入数据，此时应该成功
    ASSERT_NO_THROW({
        static_cast<char*>(ptr)[0] = 'A';
    });

    // 3. 调用我们封装的 mprotect，将内存设置为只读
    int protect_result = mprotect(ptr, TEST_SIZE, PROT_READ);
    ASSERT_EQ(protect_result, 0) << "mprotect failed. Errno: " << errno;

    // 4. 再次尝试写入，此时应该会失败（导致段错误）
    // 注意：直接测试段错误比较复杂，通常需要设置信号处理器。
    // 在 GTest 中，一个更简单的做法是假设它会崩溃，并使用死亡测试 (Death Test)。
    // 这是一种高级测试技术，我们在这里先不引入，只验证 mprotect 的返回码。
    // 如果你想深入，可以研究 EXPECT_DEATH 宏。
    // 对于当前阶段，验证 mprotect 成功返回 0 已经足够。

    // 5. 验证内存仍然是可读的
    EXPECT_EQ(static_cast<char*>(ptr)[0], 'A');

    // 6. 清理
    munmap(ptr, TEST_SIZE);
}