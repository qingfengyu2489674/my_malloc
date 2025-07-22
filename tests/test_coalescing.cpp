#include <gtest/gtest.h>
#include "my_malloc/ThreadHeap.hpp"
#include "my_malloc/internal/MappedSegment.hpp"
#include "my_malloc/internal/definitions.hpp"
#include "my_malloc/internal/AllocSlab.hpp"
#include "my_malloc/internal/SlabConfig.hpp"

namespace my_malloc {

class ThreadHeapFriend : public ThreadHeap {
public:
    internal::LargeSlabHeader* get_freelist_head(uint16_t num_pages) {
        if (num_pages == 0 || num_pages > (internal::SEGMENT_SIZE / internal::PAGE_SIZE)) {
            return nullptr;
        }
        return free_slabs_[num_pages - 1];
    }
    
    // 辅助函数，用于获取一个 user_ptr 对应的 slab 大小（页数）
    uint16_t get_slab_pages(void* user_ptr) {
        if (!user_ptr) return 0;
        
        // --- 【修复点】: 加上 internal:: 命名空间前缀 ---
        void* header_ptr = static_cast<char*>(user_ptr) - sizeof(internal::LargeSlabHeader);
        auto* header = static_cast<internal::LargeSlabHeader*>(header_ptr);
        // --- 【修复结束】 ---
        
        return header->num_pages_;
    }
};

class CoalescingTest : public ::testing::Test {
protected:
    ThreadHeapFriend* heap_ = nullptr;
    
    // --- 【修复点】: 同样加上 internal:: ---
    const size_t header_size = sizeof(internal::LargeSlabHeader);

    void SetUp() override {
        heap_ = new ThreadHeapFriend();
    }
    void TearDown() override {
        delete heap_;
    }

    void expect_freelist_is_empty(uint16_t num_pages) {
        EXPECT_EQ(heap_->get_freelist_head(num_pages), nullptr)
            << "Freelist for " << num_pages << " pages should be empty.";
    }

    // 分配函数，确保是 Large Object
    void* allocate_large(size_t user_size) {
        return heap_->allocate(internal::MAX_SMALL_OBJECT_SIZE + user_size);
    }
};

// ===================================================================================
// 场景 1: 无合并
// ===================================================================================
TEST_F(CoalescingTest, NoCoalescingWhenNeighborsAreAllocated) {
    const size_t user_size = internal::MAX_SMALL_OBJECT_SIZE + 1;
    const size_t total_alloc_size = user_size + header_size;
    const uint16_t expected_pages = (total_alloc_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    
    void* user_ptr_a = heap_->allocate(user_size);
    void* user_ptr_b = heap_->allocate(user_size);
    void* user_ptr_c = heap_->allocate(user_size);
    
    ASSERT_NE(user_ptr_a, nullptr);
    ASSERT_NE(user_ptr_b, nullptr);
    ASSERT_NE(user_ptr_c, nullptr);
    
    heap_->free(user_ptr_b);

    internal::LargeSlabHeader* free_slab = heap_->get_freelist_head(expected_pages);
    ASSERT_NE(free_slab, nullptr);
    EXPECT_EQ(free_slab->num_pages_, expected_pages);
    
    heap_->free(user_ptr_a);
    heap_->free(user_ptr_c);
}


// ===================================================================================
// 场景 2: 向后合并
// ===================================================================================
TEST_F(CoalescingTest, CoalesceWithNextBlock) {
    void* user_ptr_a = allocate_large(10 * internal::PAGE_SIZE);
    void* user_ptr_b = allocate_large(20 * internal::PAGE_SIZE);
    void* user_ptr_c = allocate_large(30 * internal::PAGE_SIZE);
    
    uint16_t pages_a = heap_->get_slab_pages(user_ptr_a);
    uint16_t pages_b = heap_->get_slab_pages(user_ptr_b);
    
    heap_->free(user_ptr_c);

    heap_->free(user_ptr_b);

    ASSERT_NE(heap_->get_freelist_head(349 + pages_b), nullptr);

    heap_->free(user_ptr_a);

    ASSERT_NE(heap_->get_freelist_head(349 + pages_b + pages_a), nullptr);

}

// in tests/test_coalescing.cpp

// ===================================================================================
// 场景 3: 向前合并 (最终修正版)
// ===================================================================================
TEST_F(CoalescingTest, CoalesceWithPreviousBlock) {
    // 1. 准备布局: [Free A | ToBeFreed B | Allocated C]
    // 我们需要精确控制这个布局。
    
    // a. 分配三个连续的块 A, B, C
    void* user_ptr_a = allocate_large(10 * internal::PAGE_SIZE);
    void* user_ptr_b = allocate_large(20 * internal::PAGE_SIZE);
    void* user_ptr_c = allocate_large(30 * internal::PAGE_SIZE);
    ASSERT_NE(user_ptr_a, nullptr);
    ASSERT_NE(user_ptr_b, nullptr);
    ASSERT_NE(user_ptr_c, nullptr);

    // 获取它们的真实页数
    uint16_t pages_a = heap_->get_slab_pages(user_ptr_a);
    uint16_t pages_b = heap_->get_slab_pages(user_ptr_b);
    
    // b. 首先释放 A，创造一个空闲的前邻居
    heap_->free(user_ptr_a);
    
    // c. 验证初始状态：freelist 中只有 A
    ASSERT_NE(heap_->get_freelist_head(pages_a), nullptr);
    expect_freelist_is_empty(pages_b);

    // 2. 【执行操作】: 释放 B。
    //    此时 B 的前面是空闲的 A，后面是被占用的 C。
    //    B 应该只与 A 发生向前合并。
    heap_->free(user_ptr_b);

    // 3. 【验证结果】:
    // a. A 和 B 原来各自的 freelist 应该都空了
    expect_freelist_is_empty(pages_a);
    expect_freelist_is_empty(pages_b);

    // b. 应该出现一个合并后大小的新空闲块
    uint16_t merged_pages = pages_a + pages_b;
    internal::LargeSlabHeader* merged_slab = heap_->get_freelist_head(merged_pages);
    ASSERT_NE(merged_slab, nullptr) << "Block A and B were not merged correctly.";
    
    // c. 验证合并后的块大小
    EXPECT_EQ(merged_slab->num_pages_, merged_pages);

    // d. 【关键】验证合并后的块头部是 A 的头部
    void* header_ptr_a = static_cast<char*>(user_ptr_a) - header_size;
    EXPECT_EQ(static_cast<void*>(merged_slab), header_ptr_a);
    
    // 4. 【清理】: 释放最后的 C
    heap_->free(user_ptr_c);
}

// in tests/test_coalescing.cpp

// ===================================================================================
// 场景 4: 双向合并 (最终修正版)
// ===================================================================================
TEST_F(CoalescingTest, CoalesceWithBothNeighbors) {
    // 1. 准备布局: [Allocated A | Allocated B | Allocated C]
    //    由于是从一个大的空闲块中连续分裂出来的，它们在物理上是连续的。
    void* user_ptr_a = allocate_large(10 * internal::PAGE_SIZE);
    void* user_ptr_b = allocate_large(20 * internal::PAGE_SIZE);
    void* user_ptr_c = allocate_large(30 * internal::PAGE_SIZE);
    ASSERT_NE(user_ptr_a, nullptr);
    ASSERT_NE(user_ptr_b, nullptr);
    ASSERT_NE(user_ptr_c, nullptr);

    // 获取它们的真实页数
    uint16_t pages_a = heap_->get_slab_pages(user_ptr_a);
    uint16_t pages_b = heap_->get_slab_pages(user_ptr_b);
    uint16_t pages_c = heap_->get_slab_pages(user_ptr_c);
    
    // 2. 创造 [Free A | Allocated B | Free C] 布局
    heap_->free(user_ptr_a);
    heap_->free(user_ptr_c);
    
    // 3. 验证初始状态：freelist 中有 A 和 C
    ASSERT_NE(heap_->get_freelist_head(pages_a), nullptr);
    ASSERT_NE(heap_->get_freelist_head(509 - pages_a - pages_b), nullptr);
    expect_freelist_is_empty(pages_b);

    // 4. 【执行操作】: 释放 B。
    //    此时 B 的前面是空闲的 A，后面是空闲的 C。
    //    B 应该与 A 和 C 同时合并。
    heap_->free(user_ptr_b);

    // 5. 【验证结果】:
    // a. A, B, C 原来各自的 freelist 应该都空了
    expect_freelist_is_empty(pages_a);
    expect_freelist_is_empty(pages_b);
    expect_freelist_is_empty(pages_c);

    // b. 应该出现一个合并了三者大小的新空闲块
    internal::LargeSlabHeader* merged_slab = heap_->get_freelist_head(509);
    ASSERT_NE(merged_slab, nullptr) << "Blocks A, B, and C were not merged correctly.";
    
    // c. 验证合并后的块大小
    EXPECT_EQ(merged_slab->num_pages_, 509);

    // d. 【关键】验证合并后的块头部是 A 的头部
    void* header_ptr_a = static_cast<char*>(user_ptr_a) - header_size;
    EXPECT_EQ(static_cast<void*>(merged_slab), header_ptr_a);
}

} // namespace my_malloc