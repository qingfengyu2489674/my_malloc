// file: tests/test_recycling.cpp (已修改以匹配配置)

#include <gtest/gtest.h>

// 包含被测试的类
#include <my_malloc/ThreadHeap.hpp>

// 包含测试中需要用到的所有内部定义
#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/AllocSlab.hpp>
#include <my_malloc/internal/SlabConfig.hpp>
#include <my_malloc/internal/definitions.hpp>

// 包含辅助工具
#include <vector>
#include <algorithm>

namespace my_malloc {

// 使用一个专用的测试固件，以便将来添加更复杂的设置
class RecyclingTest : public ::testing::Test {
protected:
    ThreadHeap* heap_ = nullptr;

    void SetUp() override {
        heap_ = new ThreadHeap();
    }

    void TearDown() override {
        delete heap_;
    }

    // 辅助函数，计算请求大小所需的确切页数
    size_t get_pages_for_size(size_t size) {
        return (size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    }
};

// ===================================================================================
// 测试用例 1: 核心功能 - 回收并重用一个同样大小的大对象
// ===================================================================================
TEST_F(RecyclingTest, ReuseFreedLargeSlabOfSameSize) {
    // 1. 准备: 请求一个明确的大对象尺寸
    const size_t size = internal::MAX_SMALL_OBJECT_SIZE + 1;
    
    // 2. 第一次分配
    void* ptr1 = heap_->allocate(size);
    ASSERT_NE(ptr1, nullptr) << "Initial allocation failed.";

    // 3. 释放
    heap_->free(ptr1);

    // 4. 第二次分配同样大小
    void* ptr2 = heap_->allocate(size);
    ASSERT_NE(ptr2, nullptr) << "Second allocation failed.";

    // 5. 核心验证
    EXPECT_EQ(ptr1, ptr2) << "Allocator should reuse the exact same memory block for a subsequent allocation of the same size.";

    // 6. 清理
    heap_->free(ptr2);
}


// ===================================================================================
// 测试用例 2: 验证释放后 PageDescriptor 的状态
// ===================================================================================
TEST_F(RecyclingTest, PageDescriptorsAreMarkedAsFreeAfterRelease) {
    // 1. 准备: 请求一个明确的大对象尺寸
    const size_t size = internal::MAX_SMALL_OBJECT_SIZE + 1024;
    const size_t num_pages = get_pages_for_size(size);
    
    // 2. 分配并获取元数据指针
    void* ptr = heap_->allocate(size);
    ASSERT_NE(ptr, nullptr);
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);

    // 3. 释放
    heap_->free(ptr);
    
    // 4. 验证
    for (size_t i = 0; i < num_pages; ++i) {
        char* page_ptr = static_cast<char*>(ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* desc = seg->page_descriptor_from_ptr(page_ptr);
        
        EXPECT_EQ(desc->status, internal::PageStatus::FREE) 
            << "Page " << i << " should be marked as FREE after slab release.";
    }
}


// ===================================================================================
// 测试用例 3: 连续回收多个同样大小的对象
// ===================================================================================
TEST_F(RecyclingTest, ReuseMultipleFreedSlabsOfSameSize) {
    // 1. 准备: 请求一个明确的大对象尺寸
    const size_t size = internal::MAX_SMALL_OBJECT_SIZE + 4096;
    const int N = 3;
    std::vector<void*> ptrs;
    ptrs.reserve(N);

    // 2. 分配 N 个对象
    for (int i = 0; i < N; ++i) {
        ptrs.push_back(heap_->allocate(size));
        ASSERT_NE(ptrs.back(), nullptr);
    }
    
    // 3. 按顺序释放它们 (ptr[0], ptr[1], ptr[2])
    for (int i = 0; i < N; ++i) {
        heap_->free(ptrs[i]);
    }
    
    // 4. 重新分配，并验证重用顺序 (LIFO)
    EXPECT_EQ(heap_->allocate(size), ptrs[2]) << "Should reuse the last freed block first.";
    EXPECT_EQ(heap_->allocate(size), ptrs[1]) << "Should reuse the second to last freed block second.";
    EXPECT_EQ(heap_->allocate(size), ptrs[0]) << "Should reuse the first freed block last.";

    // 5. 清理
    heap_->free(ptrs[0]);
    heap_->free(ptrs[1]);
    heap_->free(ptrs[2]);
}


// ===================================================================================
// 测试用例 4: 不同大小的大对象回收与重用
// ===================================================================================
TEST_F(RecyclingTest, ReuseSlabsOfDifferentSizes) {
    // 1. 准备: 两个不同的大对象尺寸
    const size_t size_A = internal::MAX_SMALL_OBJECT_SIZE + 1; 
    const size_t size_B = internal::MAX_SMALL_OBJECT_SIZE + 8192;

    // 2. 分配并释放
    void* ptr_A1 = heap_->allocate(size_A);
    void* ptr_B1 = heap_->allocate(size_B);
    ASSERT_NE(ptr_A1, nullptr);
    ASSERT_NE(ptr_B1, nullptr);

    heap_->free(ptr_A1);
    heap_->free(ptr_B1);

    // 3. 验证
    void* ptr_B2 = heap_->allocate(size_B);
    EXPECT_EQ(ptr_B1, ptr_B2) << "Allocation for size B should reuse the block freed for size B.";
    
    void* ptr_A2 = heap_->allocate(size_A);
    EXPECT_EQ(ptr_A1, ptr_A2) << "Allocation for size A should reuse the block freed for size A.";

    // 4. 清理
    heap_->free(ptr_A2);
    heap_->free(ptr_B2);
}

// ===================================================================================
// 测试用例 5: 边界情况 - 无法重用 (请求大小不同)
// ===================================================================================
TEST_F(RecyclingTest, FallbackToNewAllocationWhenNoExactSizeMatch) {
    // 1. 准备: 一个大对象尺寸
    const size_t size_A = internal::MAX_SMALL_OBJECT_SIZE + 1;
    
    // 2. 分配并释放
    void* ptr_A = heap_->allocate(size_A);
    ASSERT_NE(ptr_A, nullptr);
    heap_->free(ptr_A); // 现在 free_slabs_ 中有一个块

    // 3. 操作: 请求一个不同的、也是大对象的尺寸
    const size_t size_B = size_A + internal::PAGE_SIZE;
    void* ptr_B = heap_->allocate(size_B);
    ASSERT_NE(ptr_B, nullptr);

    // 4. 验证
    EXPECT_NE(ptr_A, ptr_B) << "Should allocate new memory when no exact-sized free block is available.";

    // 5. 清理
    heap_->free(ptr_A);
    heap_->free(ptr_B);
}

// ===================================================================================
// 测试用例 6: 回收已清空的小对象 Slab (集成测试)
// ===================================================================================
TEST_F(RecyclingTest, ReuseMemoryFromAnEmptiedSmallSlabForLargeAllocation) {
    // 1. 准备: 获取一个 Small Slab 的信息
    const auto& config = internal::SlabConfig::get_instance();
    const size_t class_id = 5; // 选一个 size class 作为例子
    const auto& info = config.get_info(class_id);
    const size_t block_size = info.block_size;
    const size_t num_blocks_in_slab = info.slab_capacity;
    const size_t num_pages_for_slab = info.slab_pages;
    
    ASSERT_GT(num_blocks_in_slab, 0) << "Test setup error: chosen class_id has no capacity.";
    
    std::vector<void*> small_ptrs;
    small_ptrs.reserve(num_blocks_in_slab);
    
    // 2. 分配满一个 Small Slab
    for (size_t i = 0; i < num_blocks_in_slab; ++i) {
        small_ptrs.push_back(heap_->allocate(block_size));
        ASSERT_NE(small_ptrs.back(), nullptr);
    }
    void* slab_address = internal::MappedSegment::from_ptr(small_ptrs[0])->page_descriptor_from_ptr(small_ptrs[0])->slab_ptr;

    // 3. 全部释放，这将触发 release_slab
    for (auto p : small_ptrs) {
        heap_->free(p);
    }

    // 4. 操作: 分配一个大对象，其大小恰好等于被回收的 Small Slab
    // 注意：这个大对象的尺寸必须大于 MAX_SMALL_OBJECT_SIZE 才能走大对象路径
    const size_t large_alloc_size = num_pages_for_slab * internal::PAGE_SIZE;
    if (large_alloc_size <= internal::MAX_SMALL_OBJECT_SIZE) {
        // 如果 Small Slab 本身就不够大，这个测试无法按预期进行
        // 我们可以跳过，或者选择一个更大的 class_id 来测试
        GTEST_SKIP() << "The chosen small slab size is not large enough to be reallocated as a large object.";
    }

    void* large_ptr = heap_->allocate(large_alloc_size);
    ASSERT_NE(large_ptr, nullptr);

    // 5. 核心验证
    EXPECT_EQ(large_ptr, slab_address) << "A large allocation should reuse the memory from a fully emptied small slab.";
    
    // 6. 清理
    heap_->free(large_ptr);
}


} // namespace my_malloc