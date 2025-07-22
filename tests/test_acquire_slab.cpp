#include <gtest/gtest.h>
#include "my_malloc/ThreadHeap.hpp"
#include "my_malloc/internal/MappedSegment.hpp"
#include "my_malloc/internal/definitions.hpp"
#include "my_malloc/internal/AllocSlab.hpp"

namespace my_malloc {

class ThreadHeapFriend : public my_malloc::ThreadHeap {
public:
    void* test_acquire_pages(uint16_t num_pages) { return acquire_pages(num_pages); }
    void test_release_slab(void* ptr, uint16_t num_pages) { release_slab(ptr, num_pages); }
    my_malloc::MappedSegment* get_active_segments() { return active_segments_; }
    my_malloc::LargeSlabHeader* get_freelist_head(uint16_t num_pages) {
        if (num_pages == 0 || num_pages > 512) return nullptr;
        return free_slabs_[num_pages - 1];
    }
};

class AcquireSlabTest : public ::testing::Test {
protected:
    ThreadHeapFriend* heap_ = nullptr;
    void SetUp() override { heap_ = new ThreadHeapFriend(); }
    void TearDown() override { delete heap_; }
};


// ===================================================================================
// 测试 3: 创建新 Segment - freelist 完全为空
// ===================================================================================
TEST_F(AcquireSlabTest, CreateNewSegmentWhenFreeListIsEmpty) {
    // 确认初始状态为空
    ASSERT_EQ(heap_->get_active_segments(), nullptr);
    ASSERT_EQ(heap_->get_freelist_head(10), nullptr);

    // 操作：请求 10 页
    void* slab10_header = heap_->test_acquire_pages(10);
    ASSERT_NE(slab10_header, nullptr);

    // 验证：
    // 1. 创建了一个新的 active segment
    MappedSegment* seg1 = heap_->get_active_segments();
    ASSERT_NE(seg1, nullptr);
    EXPECT_EQ(MappedSegment::get_segment(slab10_header), seg1);

    // 2. freelist 中应该有一个大的剩余块
    const size_t metadata_pages = (sizeof(MappedSegment) + PAGE_SIZE - 1) / PAGE_SIZE;
    const uint16_t available_pages = (SEGMENT_SIZE / PAGE_SIZE) - metadata_pages;
    const uint16_t remaining_pages = available_pages - 10;
    
    LargeSlabHeader* remainder = heap_->get_freelist_head(remaining_pages);
    ASSERT_NE(remainder, nullptr);
    EXPECT_EQ(remainder->num_pages_, remaining_pages);
}


// ===================================================================================
// 边界条件测试 (保持不变)
// ===================================================================================
TEST_F(AcquireSlabTest, RequestSlabLargerThanSegment) {
    void* slab = heap_->test_acquire_pages(SEGMENT_SIZE / PAGE_SIZE + 1);
    EXPECT_EQ(slab, nullptr);
}


} // namespace my_malloc