#include <gtest/gtest.h>
#include <my_malloc/internal/AllocSlab.hpp>
#include <my_malloc/internal/SlabConfig.hpp>
#include <vector>
#include <algorithm> // For std::random_shuffle
#include <set>       // For std::set
#include <new>       // For placement new

using namespace my_malloc::internal;

// --- 测试固件 (Test Fixture) ---
// 这个固件负责模拟一块 Slab 内存，并在其上构造 SmallSlabHeader。
class AllocSlabTest : public ::testing::Test {
protected:
    // 为测试准备一块足够大的内存缓冲区，模拟从 MappedSegment 切出来的 Slab。
    // 我们选择一个中等大小的尺寸类别来进行测试，例如 class_id=3 (对应 block_size=32)。
    static constexpr uint16_t TEST_CLASS_ID = 5;
    
    // 我们需要从 SlabConfig 获取标准的页面数，而不是硬编码。
    // 但 SetUp 是非静态的，所以我们不能在这里直接获取。我们将在 SetUp 内部进行。
    static constexpr size_t SLAB_BUFFER_SIZE = 256 * 1024; // 分配一个足够大的缓冲区，能容纳大多数Slab

    // `slab_buffer_` 是我们模拟的 Slab 内存。
    alignas(16) char slab_buffer_[SLAB_BUFFER_SIZE];
    
    // `slab_header_` 是指向构造在 slab_buffer_ 上的 SmallSlabHeader 的指针。
    SmallSlabHeader* slab_header_ = nullptr;
    const SlabConfigInfo* slab_info_ = nullptr;

    void SetUp() override {
        // 在每个测试开始前，都在我们的缓冲区上重新构造一个 SlabHeader。
        // 这确保了测试之间的隔离。

        // 1. 获取要测试的尺寸类别的配置信息。
        const auto& config = SlabConfig::get_instance();
        slab_info_ = &config.get_info(TEST_CLASS_ID);
        
        // 2. 确保我们的测试缓冲区足够大，能容纳这个标准尺寸的 Slab。
        size_t required_size = slab_info_->slab_pages * PAGE_SIZE;
        ASSERT_GE(SLAB_BUFFER_SIZE, required_size)
            << "Test buffer is too small for the chosen size class.";

        // 3. [核心修改] 使用带参数的 placement new 在缓冲区的起始位置直接构造并初始化 Header。
        slab_header_ = new (slab_buffer_) SmallSlabHeader(TEST_CLASS_ID);
    }

    void TearDown() override {
        // placement new 创建的对象需要显式调用析构函数。
        if (slab_header_) {
            slab_header_->~SmallSlabHeader();
        }
    }
};


// --- 测试用例集 (基本保持不变) ---

// 测试1: 初始化状态验证
// 检查构造函数之后，Slab 的各个状态是否正确。
TEST_F(AllocSlabTest, InitializationIsCorrect) {
    ASSERT_NE(slab_header_, nullptr);
    ASSERT_NE(slab_info_, nullptr);

    EXPECT_EQ(slab_header_->slab_class_id, TEST_CLASS_ID);
    EXPECT_EQ(slab_header_->free_count, slab_info_->slab_capacity);
    EXPECT_FALSE(slab_header_->is_full());
    EXPECT_TRUE(slab_header_->is_empty());
}

// 测试2: 完整分配流程
// 连续分配所有块，直到 Slab 变满。
TEST_F(AllocSlabTest, FullAllocationCycle) {
    const size_t capacity = slab_info_->slab_capacity;
    std::set<void*> allocated_pointers;

    for (size_t i = 0; i < capacity; ++i) {
        SCOPED_TRACE("Allocating block " + std::to_string(i + 1));
        
        void* block = slab_header_->allocate_block();
        ASSERT_NE(block, nullptr) << "Allocation should succeed when slab is not full.";
        
        EXPECT_TRUE(allocated_pointers.find(block) == allocated_pointers.end()) << "Allocated pointer is not unique.";
        allocated_pointers.insert(block);

        EXPECT_EQ(slab_header_->free_count, capacity - (i + 1));
    }

    EXPECT_TRUE(slab_header_->is_full());
    EXPECT_EQ(slab_header_->free_count, 0);

    void* extra_block = slab_header_->allocate_block();
    EXPECT_EQ(extra_block, nullptr) << "Allocation should fail when slab is full.";
}

// 测试3: 分配与释放
// 测试交错的分配和释放操作是否正确。
TEST_F(AllocSlabTest, AllocateAndFreeInterleaved) {
    std::vector<void*> pointers;
    
    for (int i = 0; i < 5; ++i) {
        pointers.push_back(slab_header_->allocate_block());
    }
    ASSERT_EQ(slab_header_->free_count, slab_info_->slab_capacity - 5);

    // 释放时不再需要传入 block_size
    slab_header_->free_block(pointers[1]);
    slab_header_->free_block(pointers[3]);
    ASSERT_EQ(slab_header_->free_count, slab_info_->slab_capacity - 3);

    void* p6 = slab_header_->allocate_block();
    void* p7 = slab_header_->allocate_block();
    ASSERT_NE(p6, nullptr);
    ASSERT_NE(p7, nullptr);

    bool p6_reused = (p6 == pointers[1] || p6 == pointers[3]);
    bool p7_reused = (p7 == pointers[1] || p7 == pointers[3]);
    EXPECT_TRUE(p6_reused);
    EXPECT_TRUE(p7_reused);
    EXPECT_NE(p6, p7);

    ASSERT_EQ(slab_header_->free_count, slab_info_->slab_capacity - 5);
}

// 测试4: 完整生命周期 (分配满 -> 释放空)
TEST_F(AllocSlabTest, FullLifecycle) {
    const size_t capacity = slab_info_->slab_capacity;
    std::vector<void*> pointers;
    
    for (size_t i = 0; i < capacity; ++i) {
        pointers.push_back(slab_header_->allocate_block());
    }
    ASSERT_TRUE(slab_header_->is_full());

    // 使用 std::random_shuffle 在 C++17 中已被弃用，建议使用 std::shuffle
    // 但为了简单，这里保持不变，或者你可以用 C++11 的 std::shuffle
    std::random_shuffle(pointers.begin(), pointers.end());
    for (size_t i = 0; i < capacity; ++i) {
        SCOPED_TRACE("Freeing block " + std::to_string(i + 1));
        slab_header_->free_block(pointers[i]);
        EXPECT_EQ(slab_header_->free_count, i + 1);
    }

    ASSERT_TRUE(slab_header_->is_empty());
    ASSERT_EQ(slab_header_->free_count, capacity);
}

// 测试5: 边界和断言检查 (Double-Free)
TEST_F(AllocSlabTest, DoubleFreeAssertion) {
    void* block = slab_header_->allocate_block();
    ASSERT_NE(block, nullptr);
    
    slab_header_->free_block(block);

#if !defined(NDEBUG)
    EXPECT_DEATH(slab_header_->free_block(block), "Attempting to double-free a block.");
#else
    GTEST_LOG_(INFO) << "Skipping death test for double-free in release mode.";
#endif
}