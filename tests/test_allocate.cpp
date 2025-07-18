// file: tests/test_allocate.cpp

#include <gtest/gtest.h>
#include <my_malloc/ThreadHeap.hpp>
#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/definitions.hpp>

namespace my_malloc {

class AllocateTest : public ::testing::Test {
protected:
    ThreadHeap* heap_ = nullptr;

    void SetUp() override {
        heap_ = new ThreadHeap();
    }

    void TearDown() override {
        delete heap_;
    }
};

// ===================================================================================
// 测试用例 1: 分配一个刚好一页大小的大对象 (保持不变)
// ===================================================================================
TEST_F(AllocateTest, AllocateOnePageLargeObject) {
    void* ptr = heap_->allocate(internal::PAGE_SIZE);
    ASSERT_NE(ptr, nullptr);

    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc = seg->page_descriptor_from_ptr(ptr);
    
    EXPECT_EQ(desc->status, internal::PageStatus::LARGE_SLAB_START);
    EXPECT_EQ(desc->num_pages, 1);
    // slab_ptr 的类型是 AllocSlab*，直接比较指针值
    EXPECT_EQ(desc->slab_ptr, reinterpret_cast<internal::AllocSlab*>(ptr));
}

// ===================================================================================
// 测试用例 2: 分配一个跨越多页的大对象 (保持不变, 修正一处比较)
// ===================================================================================
TEST_F(AllocateTest, AllocateMultiPageLargeObject) {
    size_t requested_size = internal::PAGE_SIZE * 3 + 100;
    void* ptr = heap_->allocate(requested_size);
    ASSERT_NE(ptr, nullptr);

    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* start_desc = seg->page_descriptor_from_ptr(ptr);
    
    EXPECT_EQ(start_desc->status, internal::PageStatus::LARGE_SLAB_START);
    EXPECT_EQ(start_desc->num_pages, 4);
    EXPECT_EQ(start_desc->slab_ptr, reinterpret_cast<internal::AllocSlab*>(ptr));

    for (int i = 1; i < 4; ++i) {
        char* next_page_ptr = static_cast<char*>(ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* cont_desc = seg->page_descriptor_from_ptr(next_page_ptr);
        EXPECT_EQ(cont_desc->status, internal::PageStatus::LARGE_SLAB_CONT);
        EXPECT_EQ(cont_desc->slab_ptr, reinterpret_cast<internal::AllocSlab*>(ptr));
    }
}

// ===================================================================================
// 测试用例 3: 分配一个非常大的对象，触发创建新 Segment (保持不变)
// ===================================================================================
TEST_F(AllocateTest, AllocationSpansAcrossSegments) {
    const size_t header_size = sizeof(internal::MappedSegment);
    const size_t metadata_pages = (header_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    const size_t size1 = available_pages * internal::PAGE_SIZE;
    
    void* ptr1 = heap_->allocate(size1);
    ASSERT_NE(ptr1, nullptr);
    internal::MappedSegment* seg1 = internal::MappedSegment::from_ptr(ptr1);

    void* ptr2 = heap_->allocate(internal::PAGE_SIZE);
    ASSERT_NE(ptr2, nullptr);
    internal::MappedSegment* seg2 = internal::MappedSegment::from_ptr(ptr2);

    EXPECT_NE(seg1, seg2);
}

// ===================================================================================
// 测试用例 4: 处理无效或边界请求 (已更新)
// ===================================================================================
TEST_F(AllocateTest, HandlesInvalidOrEdgeCaseSizes) {
    // 1. 请求 0 字节，应该返回 nullptr (这个行为不变)
    EXPECT_EQ(heap_->allocate(0), nullptr);

    // 2. (新行为) 请求一个超大的尺寸，现在应该作为巨型对象被成功分配
    const size_t too_large_size = internal::SEGMENT_SIZE + 1;
    void* ptr = heap_->allocate(too_large_size);
    
    // 验证它被成功分配
    ASSERT_NE(ptr, nullptr);

    // 验证它确实是作为巨型对象被处理的
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    ASSERT_NE(seg, nullptr);
    internal::PageDescriptor* desc = &seg->page_descriptors_[0];
    EXPECT_EQ(desc->status, internal::PageStatus::HUGE_SLAB);

    // 验证完毕后，释放这块内存
    heap_->free(ptr);
}


// ===================================================================================
// 测试用例 5: 专门测试巨型对象的分配与释放 (新增)
// ===================================================================================
TEST_F(AllocateTest, AllocateAndFreeHugeObject) {
    // 1. 分配一个 4MB 的巨型对象
    const size_t huge_size = internal::SEGMENT_SIZE * 2;
    void* ptr = heap_->allocate(huge_size);
    ASSERT_NE(ptr, nullptr);
    
    // 验证它被 huge_segments_ 链表管理 (需要移除 private)
    // 如果不移除 private，我们可以通过行为来间接验证
    // ASSERT_NE(heap_->huge_segments_, nullptr);
    // EXPECT_EQ(heap_->huge_segments_->list_node.next, nullptr);
    
    // 验证元数据
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    // EXPECT_EQ(seg, heap_->huge_segments_);
    EXPECT_EQ(seg->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);

    // 2. 释放
    heap_->free(ptr);

    // 验证 huge_segments_ 链表现在是空的 (同样需要移除 private)
    // 我们可以通过再次分配来间接验证
    // EXPECT_EQ(heap_->huge_segments_, nullptr);
}


// in: tests/test_allocate.cpp

// ... (你现有的所有测试用例保持不变) ...

// ===================================================================================
// 测试用例 6: 分配一个正好等于阈值大小的对象
// ===================================================================================
// 目的: 验证边界条件，确保大于阈值的才算 Huge Object
TEST_F(AllocateTest, AllocateObjectAtHugeThreshold) {
    const size_t header_size = sizeof(internal::MappedSegment);
    
    const size_t metadata_pages = (header_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    const size_t huge_object_threshold = available_pages * internal::PAGE_SIZE;
    
    // 操作：请求一个正好等于阈值的大小 (这应该被当作 Large Object 处理)
    void* ptr = heap_->allocate(huge_object_threshold);
    ASSERT_NE(ptr, nullptr);

    // 验证：它应该来自一个常规的 active_segment，而不是 huge_segment
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc = seg->page_descriptor_from_ptr(ptr);
    
    EXPECT_EQ(desc->status, internal::PageStatus::LARGE_SLAB_START);

    // 释放它 (虽然不是测试重点，但保持环境干净)
    heap_->free(ptr);
}


// ===================================================================================
// 测试用例 7: 连续分配多个巨型对象
// ===================================================================================
// 目的: 验证 huge_segments_ 链表是否能正确地串联多个节点
TEST_F(AllocateTest, AllocateMultipleHugeObjects) {
    const size_t huge_size1 = internal::SEGMENT_SIZE;
    const size_t huge_size2 = internal::SEGMENT_SIZE * 2;

    // 操作：连续分配两个巨型对象
    void* ptr1 = heap_->allocate(huge_size1);
    void* ptr2 = heap_->allocate(huge_size2);

    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);

    // 验证元数据
    internal::MappedSegment* seg1 = internal::MappedSegment::from_ptr(ptr1);
    internal::MappedSegment* seg2 = internal::MappedSegment::from_ptr(ptr2);

    ASSERT_NE(seg1, seg2);
    EXPECT_EQ(seg1->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);
    EXPECT_EQ(seg2->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);

    // 验证链表结构 (需要移除 private 才能直接验证)
    // 如果不移除，我们可以通过 free 的行为来间接验证
    // EXPECT_EQ(heap_->huge_segments_, seg2);
    // EXPECT_EQ(seg2->list_node.next, seg1);

    // 释放它们，不应该影响对方
    heap_->free(ptr1);
    heap_->free(ptr2);
}

// ===================================================================================
// 测试用例 8: 交错分配大对象和巨型对象
// ===================================================================================
// 目的: 验证两种分配路径不会互相干扰
TEST_F(AllocateTest, InterleaveLargeAndHugeAllocations) {
    // 1. 分配一个大对象
    void* large_ptr1 = heap_->allocate(internal::PAGE_SIZE * 10);
    ASSERT_NE(large_ptr1, nullptr);
    internal::MappedSegment* regular_seg1 = internal::MappedSegment::from_ptr(large_ptr1);

    // 2. 分配一个巨型对象
    void* huge_ptr = heap_->allocate(internal::SEGMENT_SIZE);
    ASSERT_NE(huge_ptr, nullptr);
    internal::MappedSegment* huge_seg = internal::MappedSegment::from_ptr(huge_ptr);
    
    // 3. 再分配一个大对象
    void* large_ptr2 = heap_->allocate(internal::PAGE_SIZE * 20);
    ASSERT_NE(large_ptr2, nullptr);
    internal::MappedSegment* regular_seg2 = internal::MappedSegment::from_ptr(large_ptr2);

    // 验证：
    // a. 巨型对象有自己独立的 Segment
    EXPECT_NE(huge_seg, regular_seg1);
    EXPECT_NE(huge_seg, regular_seg2);

    // b. 两个大对象应该来自同一个常规 Segment (因为空间足够)
    EXPECT_EQ(regular_seg1, regular_seg2);

    // c. 验证元数据状态
    EXPECT_EQ(huge_seg->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);
    EXPECT_EQ(regular_seg1->page_descriptor_from_ptr(large_ptr1)->status, internal::PageStatus::LARGE_SLAB_START);
    EXPECT_EQ(regular_seg2->page_descriptor_from_ptr(large_ptr2)->status, internal::PageStatus::LARGE_SLAB_START);

    // 按任意顺序释放
    heap_->free(huge_ptr);
    heap_->free(large_ptr1);
    heap_->free(large_ptr2);
}


// ===================================================================================
// 测试用例 9: 验证巨型对象释放后，链表被正确维护
// ===================================================================================
// 目的: 专门测试 free 函数中对 huge_segments_ 链表的移除逻辑
TEST_F(AllocateTest, FreeHugeObjectMaintainsListIntegrity) {
    // 创建三个巨型对象，形成一个链表: seg3 -> seg2 -> seg1
    void* ptr1 = heap_->allocate(internal::SEGMENT_SIZE + 1);
    void* ptr2 = heap_->allocate(internal::SEGMENT_SIZE + 2);
    void* ptr3 = heap_->allocate(internal::SEGMENT_SIZE + 3);
    
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr3, nullptr);

    // 验证释放中间节点 (ptr2)
    heap_->free(ptr2);
    // (需要移除 private 才能直接验证 heap_->huge_segments_ 链表现在是 seg3 -> seg1)

    // 验证释放头节点 (ptr3)
    heap_->free(ptr3);
    // (需要移除 private 才能直接验证 heap_->huge_segments_ 链表现在只剩下 seg1)
    
    // 验证释放尾节点 (ptr1)
    heap_->free(ptr1);
    // (需要移除 private 才能直接验证 heap_->huge_segments_ 链表现在是 nullptr)

    // 只要上面的 free 调用不崩溃，就说明链表操作没有引发致命错误
    SUCCEED();
}


} // namespace my_malloc