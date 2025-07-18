// file: tests/test_acquire_slab.cpp

#include <gtest/gtest.h>
#include <my_malloc/ThreadHeap.hpp>
#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/definitions.hpp>

#include <vector>
#include <cstdint>
#include <cstdlib> // 为了 posix_memalign 和 free

namespace my_malloc {

// 测试 acquire_large_slab 的功能
class AcquireSlabTest : public ::testing::Test {
protected:
    ThreadHeap* heap_ = nullptr;
    // 用于跟踪我们为测试手动创建的 Segment 的内存，
    // 以便在测试结束后进行清理。
    std::vector<void*> raw_segment_memory_to_free_; 

    void SetUp() override {
        heap_ = new ThreadHeap();
    }

    void TearDown() override {
        // 在析构 heap_ 之前，必须将它内部指向我们手动创建的内存的指针清空，
        // 否则 ~ThreadHeap 会尝试对这些内存调用 MappedSegment::destroy (即 munmap)，
        // 而这些内存是用 posix_memalign 分配的，会导致程序崩溃。
        heap_->active_segments_ = nullptr; 
        heap_->free_segments_ = nullptr;
        delete heap_;

        // 现在可以安全地释放我们为测试分配的裸内存了。
        for (void* mem : raw_segment_memory_to_free_) {
            ::free(mem);
        }
        raw_segment_memory_to_free_.clear();
    }
    
    // 手动创建一个对齐的 MappedSegment，以精确模拟 MappedSegment::create() 的行为。
    internal::MappedSegment* create_aligned_segment_for_test() {
        void* mem = nullptr;
        // 使用 posix_memalign 获取对齐内存，这对于 MappedSegment::from_ptr 至关重要。
        if (::posix_memalign(&mem, internal::SEGMENT_SIZE, sizeof(internal::MappedSegment)) != 0) {
            return nullptr;
        }
        
        // 使用 placement new 直接调用 public 构造函数。
        internal::MappedSegment* seg = new (mem) internal::MappedSegment();
        
        // 手动进行必要的初始化
        seg->set_owner_heap(heap_);
        // 标记所有页为 FREE，这是 MappedSegment::create() 应该做的
        for (size_t i = 0; i < (internal::SEGMENT_SIZE / internal::PAGE_SIZE); ++i) {
            seg->page_descriptors_[i].status = internal::PageStatus::FREE;
        }
        
        // 跟踪这块内存，以便在 TearDown 中释放
        raw_segment_memory_to_free_.push_back(mem);
        return seg;
    }
};

// ===================================================================================
// 测试用例 1: 从一个空的 Heap 中获取 Slab
// ===================================================================================
TEST_F(AcquireSlabTest, FromNewSegmentWhenHeapIsEmpty) {
    // 初始时 active_segments_ 应该为 null
    ASSERT_EQ(heap_->active_segments_, nullptr);
    
    void* slab = heap_->acquire_large_slab(1);
    ASSERT_NE(slab, nullptr);

    // 验证 heap 的 active_segments_ 链表现在有了一个节点
    ASSERT_NE(heap_->active_segments_, nullptr);
    EXPECT_EQ(heap_->active_segments_->list_node.next, nullptr);

    // 验证 slab 确实来自这个新的 segment
    EXPECT_EQ(internal::MappedSegment::from_ptr(slab), heap_->active_segments_);
}

// ===================================================================================
// 测试用例 2: 从一个已存在且有足够空间的 Active Segment 中获取 Slab
// ===================================================================================
TEST_F(AcquireSlabTest, FromExistingActiveSegmentWithSpace) {
    // 设置: 创建一个 segment，并手动将其设为 heap 的 active segment。
    internal::MappedSegment* seg1 = create_aligned_segment_for_test();
    // 直接访问和修改 public 成员
    heap_->active_segments_ = seg1;
    
    // 操作1: 请求 10 页内存。
    void* slab1 = heap_->acquire_large_slab(10);
    ASSERT_NE(slab1, nullptr);
    EXPECT_EQ(internal::MappedSegment::from_ptr(slab1), seg1);

    // 操作2: 再次请求 20 页内存。
    void* slab2 = heap_->acquire_large_slab(20);
    ASSERT_NE(slab2, nullptr);
    EXPECT_EQ(internal::MappedSegment::from_ptr(slab2), seg1);

    // 验证: 没有创建新的 segment
    EXPECT_EQ(heap_->active_segments_, seg1);
    EXPECT_EQ(heap_->active_segments_->list_node.next, nullptr);
}

// ===================================================================================
// 测试用例 3: 当唯一的 Active Segment 满了之后，会自动创建并使用新的 Segment
// ===================================================================================
TEST_F(AcquireSlabTest, FallbackToNewSegmentWhenActiveIsFull) {
    internal::MappedSegment* seg1 = create_aligned_segment_for_test();
    heap_->active_segments_ = seg1;

    // 操作1: 耗尽 seg1 的所有可用空间。
    const size_t total_pages = internal::SEGMENT_SIZE / internal::PAGE_SIZE;
    const size_t metadata_pages = (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const uint16_t available_pages = total_pages - metadata_pages;
    
    void* slab1 = heap_->acquire_large_slab(available_pages);
    ASSERT_NE(slab1, nullptr);
    EXPECT_EQ(internal::MappedSegment::from_ptr(slab1), seg1);
    
    // 操作2: 再次请求 1 页。此时 seg1 应该返回 nullptr，迫使 acquire_large_slab 创建新 segment。
    void* slab2 = heap_->acquire_large_slab(1);
    ASSERT_NE(slab2, nullptr);

    // 验证: 新的 slab2 应该来自一个新的 segment (seg2)，并且这个新 segment 现在是链表头。
    internal::MappedSegment* seg2 = internal::MappedSegment::from_ptr(slab2);
    EXPECT_NE(seg2, seg1);
    ASSERT_NE(heap_->active_segments_, nullptr);
    EXPECT_EQ(heap_->active_segments_, seg2);      // seg2 应该是新的链表头
    EXPECT_EQ(seg2->list_node.next, seg1); // seg1 应该是链表的第二个节点
}



// 测试用例 4 & 5: 边界条件测试
// ===================================================================================
// 验证的逻辑: `acquire_large_slab` 和 `find_and_allocate_slab` 的参数检查和空间计算
TEST_F(AcquireSlabTest, RequestSlabLargerThanSegment) {
    const uint16_t too_large_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) + 1;
    void* slab = heap_->acquire_large_slab(too_large_pages);
    EXPECT_EQ(slab, nullptr);
}

TEST_F(AcquireSlabTest, RequestSlabSlightlyTooLargeForNewSegment) {
    const size_t metadata_pages = (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const uint16_t max_possible_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    
    // 请求一个比最大可能值多一页的 slab，应该失败
    void* slab = heap_->acquire_large_slab(max_possible_pages + 1);
    EXPECT_EQ(slab, nullptr);
    
    // 请求一个正好是最大可能值的 slab，应该成功
    void* slab2 = heap_->acquire_large_slab(max_possible_pages);
    EXPECT_NE(slab2, nullptr);
}

}