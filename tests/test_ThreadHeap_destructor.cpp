// file: tests/test_ThreadHeap_destructor.cpp

#include <gtest/gtest.h>
#include <my_malloc/ThreadHeap.hpp>
#include <my_malloc/internal/MappedSegment.hpp>
#include <vector>
#include <algorithm>
#include <new>

namespace my_malloc {

// 测试 ThreadHeap 的析构函数
class ThreadHeapDestructorTest : public ::testing::Test {
protected:
    // 用于保存为测试创建的所有 MappedSegment 的内存，以便在测试结束后清理。
    std::vector<void*> allocated_raw_memory_;

    // 每个测试结束后执行
    void TearDown() override {
        // 析构函数应该已经通过 munmap (在真实实现中) "释放" 了 segment 的内存映射。
        // 但我们为 MappedSegment 对象本身分配的内存需要在这里手动清理。
        for (void* mem : allocated_raw_memory_) {
            // 我们不能调用 delete，因为内存上可能已经没有有效的 MappedSegment 对象了。
            // 我们直接释放裸内存。
            ::operator delete(mem);
        }
        allocated_raw_memory_.clear();
    }

    // 创建一个 "假的" MappedSegment 对象用于测试。
    internal::MappedSegment* create_mock_segment() {
        // 使用我们的测试专用“后门”函数
        auto* seg = internal::MappedSegment::create_for_test();
        // create_for_test 内部调用了 ::operator new，我们需要跟踪这个内存
        allocated_raw_memory_.push_back(seg);
        return seg;
    }
    
    // 辅助函数，将一组 segments 链接成一个单向链表
    void link_segments(const std::vector<internal::MappedSegment*>& segments, internal::MappedSegment*& list_head) {
        if (segments.empty()) {
            list_head = nullptr;
            return;
        }
        list_head = segments.front();
        for (size_t i = 0; i < segments.size() - 1; ++i) {
            segments[i]->list_node.next = segments[i + 1];
        }
        segments.back()->list_node.next = nullptr;
    }
};


// ======================= Test Cases =======================

// 测试1：析构一个完全空的 Heap
TEST_F(ThreadHeapDestructorTest, WithEmptyHeap) {
    ThreadHeap* heap = new ThreadHeap();
    // 核心测试：确保析构一个空的 heap 不会因为空指针解引用而崩溃
    delete heap;
    SUCCEED() << "Destructing an empty heap did not crash.";
}

// 测试2：只存在一个 Active Segment
TEST_F(ThreadHeapDestructorTest, WithSingleActiveSegment) {
    ThreadHeap* heap = new ThreadHeap();
    
    internal::MappedSegment* active_head = create_mock_segment();
    
    heap->set_internal_segments_for_test(active_head, nullptr);

    delete heap;
    SUCCEED() << "Heap with a single active segment destructed successfully.";
}

// 测试3：存在多个 Active Segments
TEST_F(ThreadHeapDestructorTest, WithMultipleActiveSegments) {
    ThreadHeap* heap = new ThreadHeap();
    
    std::vector<internal::MappedSegment*> active_segs;
    for(int i = 0; i < 5; ++i) active_segs.push_back(create_mock_segment());
    
    internal::MappedSegment* active_head = nullptr;
    link_segments(active_segs, active_head);
    
    heap->set_internal_segments_for_test(active_head, nullptr);

    delete heap;
    SUCCEED() << "Heap with multiple active segments destructed successfully.";
}

// 测试4：只存在一个 Free Segment
TEST_F(ThreadHeapDestructorTest, WithSingleFreeSegment) {
    ThreadHeap* heap = new ThreadHeap();
    
    internal::MappedSegment* free_head = create_mock_segment();
    
    heap->set_internal_segments_for_test(nullptr, free_head);

    delete heap;
    SUCCEED() << "Heap with a single free segment destructed successfully.";
}

// 测试5：存在多个 Free Segments
TEST_F(ThreadHeapDestructorTest, WithMultipleFreeSegments) {
    ThreadHeap* heap = new ThreadHeap();
    
    std::vector<internal::MappedSegment*> free_segs;
    for(int i = 0; i < 4; ++i) free_segs.push_back(create_mock_segment());
    
    internal::MappedSegment* free_head = nullptr;
    link_segments(free_segs, free_head);
    
    heap->set_internal_segments_for_test(nullptr, free_head);

    delete heap;
    SUCCEED() << "Heap with multiple free segments destructed successfully.";
}

// 测试6：同时存在 Active 和 Free Segments
TEST_F(ThreadHeapDestructorTest, WithBothActiveAndFreeSegments) {
    ThreadHeap* heap = new ThreadHeap();
    
    // 准备数据
    std::vector<internal::MappedSegment*> active_segs, free_segs;
    for(int i = 0; i < 3; ++i) active_segs.push_back(create_mock_segment());
    for(int i = 0; i < 2; ++i) free_segs.push_back(create_mock_segment());
    
    internal::MappedSegment* active_head = nullptr;
    internal::MappedSegment* free_head = nullptr;
    link_segments(active_segs, active_head);
    link_segments(free_segs, free_head);

    // 使用“后门”设置内部状态
    heap->set_internal_segments_for_test(active_head, free_head);

    // 核心测试：调用析构函数
    delete heap;

    // 如果程序能顺利执行到这里，说明没有崩溃
    SUCCEED() << "Heap with 3 active and 2 free segments destructed successfully.";
}

} // namespace my_malloc