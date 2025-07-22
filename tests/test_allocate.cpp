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
// 测试用例 1: 分配一个大对象
// ===================================================================================
TEST_F(AllocateTest, AllocateOnePageLargeObject) {
    const size_t large_size = internal::MAX_SMALL_OBJECT_SIZE + 1;
    void* ptr = heap_->allocate(large_size);
    ASSERT_NE(ptr, nullptr);
    
    // const size_t num_pages = (large_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;

    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc = seg->page_descriptor_from_ptr(ptr);
    
    EXPECT_EQ(desc->status, internal::PageStatus::LARGE_SLAB);
    
    // *** MODIFICATION: Removed assertion for num_pages as it no longer exists. ***
    // EXPECT_EQ(desc->num_pages, num_pages); 
    
    EXPECT_EQ(desc->slab_ptr, ptr);
}

// ===================================================================================
// 测试用例 2: 分配一个跨越多页的大对象
// ===================================================================================
TEST_F(AllocateTest, AllocateMultiPageLargeObject) {
    size_t requested_size = internal::MAX_SMALL_OBJECT_SIZE + 100;
    // const size_t num_pages = (requested_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    
    void* ptr = heap_->allocate(requested_size);
    ASSERT_NE(ptr, nullptr);

    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* start_desc = seg->page_descriptor_from_ptr(ptr);
    
    EXPECT_EQ(start_desc->status, internal::PageStatus::LARGE_SLAB);

    // *** MODIFICATION: Removed assertion for num_pages as it no longer exists. ***
    // EXPECT_EQ(start_desc->num_pages, num_pages);

    EXPECT_EQ(start_desc->slab_ptr, ptr);

    // 计算页数用于循环，但不再用于断言
    const size_t num_pages_for_loop = (requested_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    for (size_t i = 1; i < num_pages_for_loop; ++i) {
        char* next_page_ptr = static_cast<char*>(ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* cont_desc = seg->page_descriptor_from_ptr(next_page_ptr);
        EXPECT_EQ(cont_desc->status, internal::PageStatus::LARGE_SLAB);
        EXPECT_EQ(cont_desc->slab_ptr, ptr);
    }
}

// ===================================================================================
// 测试用例 3: 分配一个非常大的对象，触发创建新 Segment
// ===================================================================================
TEST_F(AllocateTest, AllocationSpansAcrossSegments) {
    // 这个测试不依赖 num_pages，所以不需要修改
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
// 测试用例 4: 处理无效或边界请求
// ===================================================================================
TEST_F(AllocateTest, HandlesInvalidOrEdgeCaseSizes) {
    // 这个测试不依赖 num_pages，所以不需要修改
    EXPECT_EQ(heap_->allocate(0), nullptr);

    const size_t too_large_size = internal::SEGMENT_SIZE + 1;
    void* ptr = heap_->allocate(too_large_size);
    
    ASSERT_NE(ptr, nullptr);

    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    ASSERT_NE(seg, nullptr);
    internal::PageDescriptor* desc = &seg->page_descriptors_[0];
    EXPECT_EQ(desc->status, internal::PageStatus::HUGE_SLAB);

    heap_->free(ptr);
}


// ===================================================================================
// 测试用例 5: 专门测试巨型对象的分配与释放
// ===================================================================================
TEST_F(AllocateTest, AllocateAndFreeHugeObject) {
    // 这个测试不依赖 num_pages，所以不需要修改
    const size_t huge_size = internal::SEGMENT_SIZE * 2;
    void* ptr = heap_->allocate(huge_size);
    ASSERT_NE(ptr, nullptr);
    
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    EXPECT_EQ(seg->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);

    heap_->free(ptr);

    SUCCEED();
}

// ===================================================================================
// 测试用例 6: 分配一个正好等于阈值大小的对象
// ===================================================================================
TEST_F(AllocateTest, AllocateObjectAtHugeThreshold) {
    // 这个测试不依赖 num_pages，所以不需要修改
    const size_t header_size = sizeof(internal::MappedSegment);
    const size_t metadata_pages = (header_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    const size_t huge_object_threshold = available_pages * internal::PAGE_SIZE;
    
    void* ptr = heap_->allocate(huge_object_threshold);
    ASSERT_NE(ptr, nullptr);

    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc = seg->page_descriptor_from_ptr(ptr);
    
    EXPECT_EQ(desc->status, internal::PageStatus::LARGE_SLAB);

    heap_->free(ptr);
}

// ... (后续的测试用例 7, 8, 9 也不依赖于 PageDescriptor 中的 num_pages，因此无需修改) ...

// ===================================================================================
// 测试用例 7: 连续分配多个巨型对象
// ===================================================================================
TEST_F(AllocateTest, AllocateMultipleHugeObjects) {
    const size_t huge_size1 = internal::SEGMENT_SIZE;
    const size_t huge_size2 = internal::SEGMENT_SIZE * 2;

    void* ptr1 = heap_->allocate(huge_size1);
    void* ptr2 = heap_->allocate(huge_size2);
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);

    internal::MappedSegment* seg1 = internal::MappedSegment::from_ptr(ptr1);
    internal::MappedSegment* seg2 = internal::MappedSegment::from_ptr(ptr2);
    ASSERT_NE(seg1, seg2);
    EXPECT_EQ(seg1->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);
    EXPECT_EQ(seg2->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);

    heap_->free(ptr1);
    heap_->free(ptr2);
}

// ===================================================================================
// 测试用例 8: 交错分配大对象和巨型对象
// ===================================================================================
TEST_F(AllocateTest, InterleaveLargeAndHugeAllocations) {
    void* large_ptr1 = heap_->allocate(internal::MAX_SMALL_OBJECT_SIZE + 1);
    ASSERT_NE(large_ptr1, nullptr);
    internal::MappedSegment* regular_seg1 = internal::MappedSegment::from_ptr(large_ptr1);

    void* huge_ptr = heap_->allocate(internal::SEGMENT_SIZE);
    ASSERT_NE(huge_ptr, nullptr);
    internal::MappedSegment* huge_seg = internal::MappedSegment::from_ptr(huge_ptr);
    
    void* large_ptr2 = heap_->allocate(internal::MAX_SMALL_OBJECT_SIZE + 2);
    ASSERT_NE(large_ptr2, nullptr);
    internal::MappedSegment* regular_seg2 = internal::MappedSegment::from_ptr(large_ptr2);

    EXPECT_NE(huge_seg, regular_seg1);
    EXPECT_NE(huge_seg, regular_seg2);

    EXPECT_EQ(huge_seg->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);
    EXPECT_EQ(regular_seg1->page_descriptor_from_ptr(large_ptr1)->status, internal::PageStatus::LARGE_SLAB);
    EXPECT_EQ(regular_seg2->page_descriptor_from_ptr(large_ptr2)->status, internal::PageStatus::LARGE_SLAB);

    heap_->free(huge_ptr);
    heap_->free(large_ptr1);
    heap_->free(large_ptr2);
}

// ===================================================================================
// 测试用例 9: 验证巨型对象释放后，链表被正确维护
// ===================================================================================
TEST_F(AllocateTest, FreeHugeObjectMaintainsListIntegrity) {
    void* ptr1 = heap_->allocate(internal::SEGMENT_SIZE + 1);
    void* ptr2 = heap_->allocate(internal::SEGMENT_SIZE + 2);
    void* ptr3 = heap_->allocate(internal::SEGMENT_SIZE + 3);
    
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr3, nullptr);

    heap_->free(ptr2);
    heap_->free(ptr3);
    heap_->free(ptr1);
    
    SUCCEED();
}

} // namespace my_malloc