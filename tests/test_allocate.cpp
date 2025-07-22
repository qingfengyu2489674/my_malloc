#include <gtest/gtest.h>
#include "my_malloc/ThreadHeap.hpp"
#include "my_malloc/internal/MappedSegment.hpp"
#include "my_malloc/internal/definitions.hpp"
#include "my_malloc/internal/AllocSlab.hpp"
#include "my_malloc/internal/SlabConfig.hpp"

namespace my_malloc {

class AllocateTest : public ::testing::Test {
protected:
    ThreadHeap* heap_ = nullptr;
    const size_t header_size = sizeof(internal::LargeSlabHeader);

    void SetUp() override {
        heap_ = new ThreadHeap();
    }
    void TearDown() override {
        delete heap_;
    }

    // 辅助函数：从用户指针反向计算头部指针
    static void* get_header_from_user_ptr(void* user_ptr) {
        if (!user_ptr) return nullptr;
        // Huge objects have no header, user_ptr is inside the MappedSegment header space
        // A bit of a hack to distinguish, a better way would be a magic number or type field.
        // For now, let's assume if it's not a standard segment, it might be huge.
        auto* seg = internal::MappedSegment::get_segment(user_ptr);
        if (seg->page_descriptors_[0].status == internal::PageStatus::HUGE_SLAB) {
            return seg; // For huge objects, the "header" is the segment itself
        }
        return static_cast<char*>(user_ptr) - sizeof(internal::LargeSlabHeader);
    }
};

// ===================================================================================
// 测试用例 1 & 2 合并: 分配和验证一个跨越多页的大对象
// ===================================================================================
TEST_F(AllocateTest, AllocateAndVerifyMultiPageLargeObject) {
    const size_t user_size = internal::MAX_SMALL_OBJECT_SIZE + 100;
    void* user_ptr = heap_->allocate(user_size);
    ASSERT_NE(user_ptr, nullptr);

    // 1. 获取头部信息
    void* header_ptr = get_header_from_user_ptr(user_ptr);
    const size_t total_alloc_size = user_size + header_size;
    const uint16_t expected_pages = (total_alloc_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;

    // 2. 验证嵌入的头部元数据
    auto* header = static_cast<internal::LargeSlabHeader*>(header_ptr);
    EXPECT_EQ(header->num_pages_, expected_pages);

    // 3. 验证每一页的 PageDescriptor
    internal::MappedSegment* seg = internal::MappedSegment::get_segment(header_ptr);
    for (uint16_t i = 0; i < expected_pages; ++i) {
        SCOPED_TRACE("Verifying page " + std::to_string(i));
        void* current_page_ptr = static_cast<char*>(header_ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* desc = seg->get_page_desc(current_page_ptr);
        
        EXPECT_EQ(desc->status, internal::PageStatus::LARGE_SLAB);
        EXPECT_EQ(desc->slab_ptr, header_ptr);
    }
    
    heap_->free(user_ptr);
}


// ===================================================================================
// 测试用例 3: 分配占满 Segment 的对象以触发新 Segment
// ===================================================================================
TEST_F(AllocateTest, AllocationSpansAcrossSegments) {
    const size_t metadata_pages = (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    
    // 请求一个几乎占满整个 Segment 可用空间的用户区
    const size_t user_size1 = available_pages * internal::PAGE_SIZE - header_size;
    
    void* user_ptr1 = heap_->allocate(user_size1);
    ASSERT_NE(user_ptr1, nullptr);
    internal::MappedSegment* seg1 = internal::MappedSegment::get_segment(user_ptr1);

    // 再请求一个很小的 Large Object，freelist 中没有合适的，应创建新 Segment
    void* user_ptr2 = heap_->allocate(internal::MAX_SMALL_OBJECT_SIZE + 1);
    ASSERT_NE(user_ptr2, nullptr);
    internal::MappedSegment* seg2 = internal::MappedSegment::get_segment(user_ptr2);

    EXPECT_NE(seg1, seg2);
    
    heap_->free(user_ptr1);
    heap_->free(user_ptr2);
}

// ===================================================================================
// 测试用例 4: 处理无效或边界请求
// ===================================================================================
TEST_F(AllocateTest, HandlesInvalidOrEdgeCaseSizes) {
    EXPECT_EQ(heap_->allocate(0), nullptr);

    // Huge Object allocation
    const size_t huge_size = internal::SEGMENT_SIZE * 2;
    void* user_ptr = heap_->allocate(huge_size);
    ASSERT_NE(user_ptr, nullptr);

    // For huge objects, there's no embedded header. The "header" is the segment itself.
    internal::MappedSegment* seg = internal::MappedSegment::get_segment(user_ptr);
    internal::PageDescriptor* desc = &seg->page_descriptors_[0];
    EXPECT_EQ(desc->status, internal::PageStatus::HUGE_SLAB);

    heap_->free(user_ptr);
}


// ===================================================================================
// 测试用例 5 & 6 & 7 & 8 & 9 (Huge Object 和边界测试)
// 保持这些测试的逻辑，只更新函数名
// ===================================================================================
TEST_F(AllocateTest, AllocateObjectAtHugeThreshold) {
    const size_t header_size_seg = sizeof(internal::MappedSegment);
    const size_t metadata_pages = (header_size_seg + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;

    const size_t header_size_large = sizeof(internal::LargeSlabHeader);
    const size_t large_metadata_pages = (header_size_large + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;  

    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages - large_metadata_pages;
    const size_t huge_object_threshold = available_pages * internal::PAGE_SIZE;
    
    void* user_ptr = heap_->allocate(huge_object_threshold);
    ASSERT_NE(user_ptr, nullptr);

    internal::MappedSegment* seg = internal::MappedSegment::get_segment(user_ptr);
    internal::PageDescriptor* desc = seg->get_page_desc(user_ptr);
    
    // It should be a LARGE_SLAB, not HUGE_SLAB
    EXPECT_EQ(desc->status, internal::PageStatus::LARGE_SLAB);

    heap_->free(user_ptr);
}

TEST_F(AllocateTest, InterleaveLargeAndHugeAllocations) {
    void* large_ptr1 = heap_->allocate(internal::MAX_SMALL_OBJECT_SIZE + 1);
    ASSERT_NE(large_ptr1, nullptr);
    internal::MappedSegment* regular_seg1 = internal::MappedSegment::get_segment(large_ptr1);

    void* huge_ptr = heap_->allocate(internal::SEGMENT_SIZE);
    ASSERT_NE(huge_ptr, nullptr);
    internal::MappedSegment* huge_seg = internal::MappedSegment::get_segment(huge_ptr);
    
    void* large_ptr2 = heap_->allocate(internal::MAX_SMALL_OBJECT_SIZE + 2);
    ASSERT_NE(large_ptr2, nullptr);
    internal::MappedSegment* regular_seg2 = internal::MappedSegment::get_segment(large_ptr2);

    EXPECT_NE(huge_seg, regular_seg1);
    EXPECT_NE(huge_seg, regular_seg2);

    EXPECT_EQ(huge_seg->page_descriptors_[0].status, internal::PageStatus::HUGE_SLAB);
    EXPECT_EQ(regular_seg1->get_page_desc(large_ptr1)->status, internal::PageStatus::LARGE_SLAB);
    EXPECT_EQ(regular_seg2->get_page_desc(large_ptr2)->status, internal::PageStatus::LARGE_SLAB);

    heap_->free(huge_ptr);
    heap_->free(large_ptr1);
    heap_->free(large_ptr2);
}

} // namespace my_malloc