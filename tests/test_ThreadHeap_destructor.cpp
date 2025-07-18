// file: tests/test_ThreadHeap_destructor.cpp

#include <gtest/gtest.h>
#include <my_malloc/ThreadHeap.hpp>
#include <my_malloc/internal/MappedSegment.hpp>
#include <vector>
#include <algorithm> // for std::sort and std::find
#include <new>       // for placement new

namespace my_malloc {


// ======================= Test Fixture =======================

// ======================= Mocking Infrastructure =======================
// 我们需要一个地方来记录哪些 Segment 被 "销毁" 了。
// 这是一个全局（但在命名空间内）的向量，用于跟踪对 MappedSegment::destroy 的调用。
static std::vector<internal::MappedSegment*> destroyed_segments_log;

// 这是 MappedSegment::destroy 的一个 "模拟" (mock) 实现。
// 它不会真的调用 munmap，而是简单地记录下哪个 segment 指针被传递给了它。
// 这个实现将仅在链接此测试文件时被使用，从而覆盖 MappedSegment.cpp 中的真实实现。
namespace internal {

MappedSegment::MappedSegment() {
    // Intentionally left empty for mock object.
}


void MappedSegment::destroy(MappedSegment* segment) {
    if (segment) {
        destroyed_segments_log.push_back(segment);
    }
}
} // namespace internal

// ======================= Test Fixture =======================

class ThreadHeapDestructorTest : public ::testing::Test {
protected:
    // 每个测试开始前执行
    void SetUp() override {
        // 清空上一轮测试留下的日志
        destroyed_segments_log.clear();
        // 确保 heap_ 是一个全新的实例
        heap_ = new ThreadHeap();
    }

    // 每个测试结束后执行
    void TearDown() override {
        delete heap_;
        
        // 清理我们在测试中手动分配的内存，防止测试本身内存泄漏
        for (void* mem : allocated_mock_segments_mem_) {
            delete[] static_cast<char*>(mem);
        }
        allocated_mock_segments_mem_.clear();
    }

    // 创建一个 "假的" MappedSegment 对象用于测试。
    // 它使用 placement-new 在我们自己分配的内存上构造对象，
    // 因为我们不能直接调用 new MappedSegment()。
    internal::MappedSegment* create_mock_segment() {
        // 分配足够的裸内存
        void* mem = new char[sizeof(internal::MappedSegment)];
        allocated_mock_segments_mem_.push_back(mem);
        // 使用 placement new 在这块内存上构造 MappedSegment 对象
        // 因为这个测试类是 MappedSegment 的友元，所以可以访问其私有构造函数
        return new (mem) internal::MappedSegment();
    }
    
    // 辅助函数，将一组 segments 链接成一个链表
    void link_segments(const std::vector<internal::MappedSegment*>& segments, internal::MappedSegment** list_head) {
        if (segments.empty()) {
            *list_head = nullptr;
            return;
        }

        *list_head = segments.front();
        for (size_t i = 0; i < segments.size() - 1; ++i) {
            segments[i]->list_node.next = segments[i + 1];
        }
        segments.back()->list_node.next = nullptr;
    }
    
    // 验证被销毁的 segments 是否和预期的一致
    void verify_destroyed(const std::vector<internal::MappedSegment*>& expected) {
        // 排序以进行确定性比较
        std::sort(destroyed_segments_log.begin(), destroyed_segments_log.end());
        
        auto sorted_expected = expected;
        std::sort(sorted_expected.begin(), sorted_expected.end());
        
        ASSERT_EQ(destroyed_segments_log.size(), sorted_expected.size());
        EXPECT_EQ(destroyed_segments_log, sorted_expected);
    }


    ThreadHeap* heap_{nullptr};
    std::vector<void*> allocated_mock_segments_mem_;
};


// ======================= Test Cases =======================

TEST_F(ThreadHeapDestructorTest, WithEmptyHeap) {
    // 触发析构函数
    delete heap_;
    heap_ = nullptr; // 防止 TearDown 再次 delete

    // 验证：不应该有任何 segment 被销毁
    EXPECT_TRUE(destroyed_segments_log.empty());
}

TEST_F(ThreadHeapDestructorTest, CleansUpSingleActiveSegment) {
    auto seg1 = create_mock_segment();
    heap_->active_segments_ = seg1; // 直接访问私有成员

    delete heap_;
    heap_ = nullptr;

    verify_destroyed({seg1});
}

TEST_F(ThreadHeapDestructorTest, CleansUpMultipleActiveSegments) {
    auto seg1 = create_mock_segment();
    auto seg2 = create_mock_segment();
    auto seg3 = create_mock_segment();
    std::vector<internal::MappedSegment*> segments = {seg1, seg2, seg3};
    link_segments(segments, &heap_->active_segments_);
    
    delete heap_;
    heap_ = nullptr;

    verify_destroyed(segments);
}

TEST_F(ThreadHeapDestructorTest, CleansUpSingleFreeSegment) {
    auto seg1 = create_mock_segment();
    heap_->free_segments_ = seg1; // 直接访问私有成员

    delete heap_;
    heap_ = nullptr;

    verify_destroyed({seg1});
}

TEST_F(ThreadHeapDestructorTest, CleansUpMultipleFreeSegments) {
    auto seg1 = create_mock_segment();
    auto seg2 = create_mock_segment();
    std::vector<internal::MappedSegment*> segments = {seg1, seg2};
    link_segments(segments, &heap_->free_segments_);

    delete heap_;
    heap_ = nullptr;
    
    verify_destroyed(segments);
}

TEST_F(ThreadHeapDestructorTest, CleansUpBothActiveAndFreeSegments) {
    // 准备 active segments
    auto a_seg1 = create_mock_segment();
    auto a_seg2 = create_mock_segment();
    std::vector<internal::MappedSegment*> active_segs = {a_seg1, a_seg2};
    link_segments(active_segs, &heap_->active_segments_);

    // 准备 free segments
    auto f_seg1 = create_mock_segment();
    auto f_seg2 = create_mock_segment();
    auto f_seg3 = create_mock_segment();
    std::vector<internal::MappedSegment*> free_segs = {f_seg1, f_seg2, f_seg3};
    link_segments(free_segs, &heap_->free_segments_);

    delete heap_;
    heap_ = nullptr;

    // 验证所有 segments 都被销毁
    std::vector<internal::MappedSegment*> all_segs;
    all_segs.insert(all_segs.end(), active_segs.begin(), active_segs.end());
    all_segs.insert(all_segs.end(), free_segs.begin(), free_segs.end());
    verify_destroyed(all_segs);
}

} // namespace my_malloc