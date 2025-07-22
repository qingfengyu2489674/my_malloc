#include <gtest/gtest.h>
#include <vector>
#include <numeric>
#include <algorithm>
#include "my_malloc/ThreadHeap.hpp"
#include "my_malloc/internal/MappedSegment.hpp"
#include "my_malloc/internal/definitions.hpp"
#include "my_malloc/internal/AllocSlab.hpp"
#include "my_malloc/internal/SlabConfig.hpp"

namespace my_malloc {

class ThreadHeapFriend : public ThreadHeap {
public:
    LargeSlabHeader* get_freelist_head(uint16_t num_pages) {
        if (num_pages == 0 || num_pages > (SEGMENT_SIZE / PAGE_SIZE)) {
            return nullptr;
        }
        return free_slabs_[num_pages - 1];
    }

    uint16_t get_slab_pages_from_user_ptr(void* user_ptr) {
        if (!user_ptr) return 0;
        // 注意：这个辅助函数只对 Large Object 有效
        void* header_ptr = static_cast<char*>(user_ptr) - sizeof(my_malloc::LargeSlabHeader);
        auto* header = static_cast<LargeSlabHeader*>(header_ptr);
        return header->num_pages_;
    }

    MappedSegment* get_active_segments() {
        return active_segments_;
    }
};

class LifecycleAndCoalescingE2ETest : public ::testing::Test {
protected:
    ThreadHeapFriend* heap_ = nullptr;
    const size_t header_size = sizeof(LargeSlabHeader);

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
};

// ===================================================================================
// 端到端测试：模拟真实使用场景，验证分裂与合并
// ===================================================================================
TEST_F(LifecycleAndCoalescingE2ETest, FullLifecycleOfSegment) {
    // 1. 初始状态验证
    ASSERT_EQ(heap_->get_active_segments(), nullptr) << "Heap should start with no active segments.";

    // 2. 【分裂场景】第一次分配 (Large Object)，触发创建第一个 Segment
    //    这将创建一个新 Segment，并将其大部分空间格式化为一个大的 Free Slab，然后从中分裂。
    const size_t large_user_size_A = MAX_SMALL_OBJECT_SIZE + 10 * PAGE_SIZE;
    void* ptr_A = heap_->allocate(large_user_size_A);
    ASSERT_NE(ptr_A, nullptr);

    // 验证：
    // a. 现在应该有一个 active segment
    MappedSegment* seg1 = heap_->get_active_segments();
    ASSERT_NE(seg1, nullptr);
    EXPECT_EQ(seg1->list_node.next, nullptr);

    // b. freelist 中应该有一个大的剩余块
    uint16_t pages_A = heap_->get_slab_pages_from_user_ptr(ptr_A);
    const size_t metadata_pages = (sizeof(MappedSegment) + PAGE_SIZE - 1) / PAGE_SIZE;
    const uint16_t total_available_pages = (SEGMENT_SIZE / PAGE_SIZE) - metadata_pages;
    uint16_t remaining_pages_1 = total_available_pages - pages_A;
    
    LargeSlabHeader* remainder1 = heap_->get_freelist_head(remaining_pages_1);
    ASSERT_NE(remainder1, nullptr) << "A large free slab should exist after the first allocation.";
    EXPECT_EQ(remainder1->num_pages_, remaining_pages_1);

    // 3. 【分裂场景】第二次分配 (Small Object)，应该从上一步的大的 Free Slab 中分裂
    const size_t small_user_size_B = 128;
    void* ptr_B = heap_->allocate(small_user_size_B);
    ASSERT_NE(ptr_B, nullptr);
    
    // 验证：
    // a. 仍然只有一个 active segment
    EXPECT_EQ(heap_->get_active_segments(), seg1);
    
    // b. 之前的那个大 free slab 应该消失了
    expect_freelist_is_empty(remaining_pages_1);

    // c. 出现了一个新的、更小的剩余块
    const auto& info_B = SlabConfig::get_instance().get_info(
        SlabConfig::get_instance().get_size_class_index(small_user_size_B)
    );
    uint16_t pages_B = info_B.slab_pages;
    uint16_t remaining_pages_2 = remaining_pages_1 - pages_B;
    LargeSlabHeader* remainder2 = heap_->get_freelist_head(remaining_pages_2);
    ASSERT_NE(remainder2, nullptr) << "A smaller free slab should exist after the second allocation.";
    EXPECT_EQ(remainder2->num_pages_, remaining_pages_2);

    // 4. 【分裂场景】第三次分配 (另一个 Large Object)
    const size_t large_user_size_C = MAX_SMALL_OBJECT_SIZE + 50 * PAGE_SIZE;
    void* ptr_C = heap_->allocate(large_user_size_C);
    ASSERT_NE(ptr_C, nullptr);
    uint16_t pages_C = heap_->get_slab_pages_from_user_ptr(ptr_C);

    // 此时物理布局为：[A | B | C | Final Remainder]
    
    // 5. 【合并场景】开始释放并验证合并
    
    // a. 释放 A: [Free A | Alloc B | Alloc C | Free remainder] -> A 独立，不合并
    heap_->free(ptr_A);
    ASSERT_NE(heap_->get_freelist_head(pages_A), nullptr) << "Block A should be in the freelist.";
    
    // b. 释放 C: [Free A | Alloc B | Free C | Free remainder] -> C 会与 remainder 向后合并
    heap_->free(ptr_C);
    expect_freelist_is_empty(pages_C); // C 自身大小的 freelist 应该是空的

    uint16_t remaining_pages_3 = remaining_pages_2 - pages_C;
    uint16_t merged_C_size = pages_C + remaining_pages_3;
    LargeSlabHeader* merged_C = heap_->get_freelist_head(merged_C_size);
    ASSERT_NE(merged_C, nullptr) << "Block C and the final remainder should have coalesced.";
    EXPECT_EQ(merged_C->num_pages_, merged_C_size);

    // c. 释放 B: [Free A | Free B | Free (C + remainder)] -> B 会与前后都合并
    heap_->free(ptr_B);

    // 验证最终状态：
    // 所有中间块的 freelist 都应该是空的
    expect_freelist_is_empty(pages_A);
    expect_freelist_is_empty(pages_B);
    expect_freelist_is_empty(merged_C_size);

    // 最终应该合并回一个占满整个 Segment 的大空闲块
    LargeSlabHeader* final_block = heap_->get_freelist_head(total_available_pages);
    ASSERT_NE(final_block, nullptr) << "All blocks should coalesce back to a single segment-sized block.";
    EXPECT_EQ(final_block->num_pages_, total_available_pages);
}



} // namespace my_malloc