#include <gtest/gtest.h>
#include "my_malloc/ThreadHeap.hpp"
#include "my_malloc/internal/MappedSegment.hpp"
#include "my_malloc/internal/definitions.hpp"

// 使我们能够访问 ThreadHeap 的私有成员进行测试
class ThreadHeapFriend : public my_malloc::ThreadHeap {
public:
    // 暴露 acquire_pages 以便直接测试
    void* test_acquire_pages(uint16_t num_pages) {
        return acquire_pages(num_pages);
    }
    
    // 暴露内部状态以便验证
    my_malloc::internal::MappedSegment* get_active_segments() {
        return active_segments_;
    }

    my_malloc::internal::LargeSlabHeader** get_free_slabs() {
        return free_slabs_;
    }
};

class AcquireSlabTest : public ::testing::Test {
protected:
    ThreadHeapFriend* heap_ = nullptr;

    void SetUp() override {
        heap_ = new ThreadHeapFriend();
    }

    void TearDown() override {
        delete heap_;
    }
};

using namespace my_malloc;

// 测试1：当堆为空时，分配会创建一个新的Segment
TEST_F(AcquireSlabTest, FromNewSegmentWhenHeapIsEmpty) {
    ASSERT_EQ(heap_->get_active_segments(), nullptr);

    void* slab = heap_->test_acquire_pages(1);
    ASSERT_NE(slab, nullptr);

    internal::MappedSegment* seg = heap_->get_active_segments();
    ASSERT_NE(seg, nullptr);
    EXPECT_EQ(internal::MappedSegment::from_ptr(slab), seg);
    EXPECT_EQ(seg->list_node.next, nullptr);
}


// 测试2：从现有的、有空间的 active segment 中分配 (此测试逻辑需重大修正)
TEST_F(AcquireSlabTest, ReuseSlabFromFreeList) {
    // 步骤 1: 创建第一个 segment (seg1) 并从中分配一个 slab (slab1)
    void* slab1 = heap_->test_acquire_pages(10);
    ASSERT_NE(slab1, nullptr);
    internal::MappedSegment* seg1 = heap_->get_active_segments();
    ASSERT_EQ(internal::MappedSegment::from_ptr(slab1), seg1);

    // 步骤 2: 释放 slab1，它现在应该进入 free_slabs_ 列表
    heap_->release_slab(slab1, 10);
    ASSERT_NE(heap_->get_free_slabs()[9], nullptr) << "Slab should be in the free list after release.";

    // 步骤 3: 再次请求同样大小的 slab，它应该从 free_slabs_ 中被重用
    void* slab2 = heap_->test_acquire_pages(10);
    ASSERT_NE(slab2, nullptr);

    // 验证: 重用的 slab (slab2) 就是我们之前释放的那个 (slab1)
    EXPECT_EQ(slab2, slab1);
    
    // 验证: 因为是从 free_slabs_ 重用的，所以 active_segments_ 链表不应该有任何变化
    EXPECT_EQ(heap_->get_active_segments(), seg1);

    // 验证: free_slabs_ 中对应的链表现在应该是空的
    EXPECT_EQ(heap_->get_free_slabs()[9], nullptr);
}


// 测试3: 当 active segment 已满时，回退到创建新 segment
TEST_F(AcquireSlabTest, FallbackToNewSegmentWhenActiveIsFull) {
    // 步骤 1: 填满第一个 segment
    const size_t metadata_pages = (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    
    void* slab1 = heap_->test_acquire_pages(available_pages);
    ASSERT_NE(slab1, nullptr);
    internal::MappedSegment* seg1 = heap_->get_active_segments();
    ASSERT_EQ(internal::MappedSegment::from_ptr(slab1), seg1);

    // 步骤 2: 再次请求，即使只有一页，也应该触发创建新 segment (seg2)
    void* slab2 = heap_->test_acquire_pages(1);
    ASSERT_NE(slab2, nullptr);
    
    internal::MappedSegment* seg2 = heap_->get_active_segments();
    ASSERT_NE(seg2, nullptr);
    
    // 验证: 新的 slab (slab2) 来自新的 segment (seg2)
    EXPECT_NE(seg1, seg2);
    EXPECT_EQ(internal::MappedSegment::from_ptr(slab2), seg2);

    // 验证: 新的 segment (seg2) 现在是 active list 的头部，它的 next 指向旧的 (seg1)
    EXPECT_EQ(seg2, heap_->get_active_segments());
    EXPECT_EQ(seg2->list_node.next, seg1);
}

// 测试4: 请求的 slab 大于整个 segment 的可用空间
TEST_F(AcquireSlabTest, RequestSlabLargerThanSegment) {
    void* slab = heap_->test_acquire_pages(internal::SEGMENT_SIZE / internal::PAGE_SIZE + 1);
    EXPECT_EQ(slab, nullptr);
}

// 测试5: 请求的 slab 对于一个全新的 segment 来说略大
TEST_F(AcquireSlabTest, RequestSlabSlightlyTooLargeForNewSegment) {
    const size_t metadata_pages = (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;

    void* slab = heap_->test_acquire_pages(available_pages + 1);
    EXPECT_EQ(slab, nullptr);
}