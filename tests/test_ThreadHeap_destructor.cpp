// file: tests/test_ThreadHeap_destructor.cpp

#include <gtest/gtest.h>
#include <my_malloc/ThreadHeap.hpp>
#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/definitions.hpp>

#include <vector>
#include <algorithm>
#include <new>
#include <cstdlib> // 为了 posix_memalign 和 free

namespace my_malloc {

// 测试 ThreadHeap 的析构函数
class ThreadHeapDestructorTest : public ::testing::Test {
protected:
    ThreadHeap* heap_ = nullptr;
    // 用于跟踪我们为测试手动创建的 Segment 的内存
    std::vector<void*> raw_segment_memory_to_free_;

    void SetUp() override {
        heap_ = new ThreadHeap();
    }

    void TearDown() override {
        // 在析构 heap_ 之前，清空它内部的指针，
        // 防止 ~ThreadHeap 对我们手动管理的内存调用 munmap。
        heap_->active_segments_ = nullptr;
        heap_->free_segments_ = nullptr;
        delete heap_;

        // 安全地释放我们为测试分配的裸内存。
        for (void* mem : raw_segment_memory_to_free_) {
            ::free(mem);
        }
        raw_segment_memory_to_free_.clear();
    }

    // 手动创建一个对齐的 MappedSegment，以进行精确的测试设置
    internal::MappedSegment* create_aligned_segment_for_test() {
        void* mem = nullptr;
        if (::posix_memalign(&mem, internal::SEGMENT_SIZE, sizeof(internal::MappedSegment)) != 0) {
            return nullptr;
        }
        
        // 直接调用 public 构造函数
        internal::MappedSegment* seg = new (mem) internal::MappedSegment();
        seg->set_owner_heap(heap_);
        
        raw_segment_memory_to_free_.push_back(mem);
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

TEST_F(ThreadHeapDestructorTest, WithEmptyHeap) {
    // 这里的 heap_ 是在 SetUp 中创建的，它的 active/free segments 都是 nullptr
    // TearDown 会负责 delete heap_，从而触发析构函数
    // 如果析构一个空 heap 会崩溃，测试将在这里失败
    SUCCEED();
}

TEST_F(ThreadHeapDestructorTest, WithSingleActiveSegment) {
    internal::MappedSegment* seg = create_aligned_segment_for_test();
    // 直接访问成员变量来设置内部状态
    heap_->active_segments_ = seg;
    
    // TearDown 将会 delete heap_
    // 在 delete 之前，我们会把 active_segments_ 设为 nullptr，
    // 所以这里的测试实际上是验证“设置-清理”这个过程不会导致内存问题
    SUCCEED();
}


// --- 真正有效的析构函数测试 ---
// 上面的测试因为 TearDown 的清理逻辑，无法真正测试析构函数本身。
// 我们需要一个新的测试模式：在测试用例内部创建和销毁 heap。

class RealDestructorTest : public ::testing::Test {
    // 这个测试夹具不需要 SetUp 和 TearDown，因为所有对象都在测试内部管理
};

TEST_F(RealDestructorTest, DestructorCleansUpAllSegments) {
    // 我们需要一种方法来验证 MappedSegment::destroy 被调用了。
    // 在完全白盒模式下，我们可以给 MappedSegment 添加一个静态计数器。
    
    // 暂时，我们只进行“不崩溃测试”
    
    ThreadHeap* heap = new ThreadHeap();
    
    // 创建一些模拟的 Segment
    std::vector<internal::MappedSegment*> active_segs, free_segs;
    for(int i = 0; i < 3; ++i) active_segs.push_back(internal::MappedSegment::create());
    for(int i = 0; i < 2; ++i) free_segs.push_back(internal::MappedSegment::create());
    
    // 链接它们
    if (!active_segs.empty()) {
        heap->active_segments_ = active_segs.front();
        for (size_t i = 0; i < active_segs.size() - 1; ++i) active_segs[i]->list_node.next = active_segs[i+1];
    }
    if (!free_segs.empty()) {
        heap->free_segments_ = free_segs.front();
        for (size_t i = 0; i < free_segs.size() - 1; ++i) free_segs[i]->list_node.next = free_segs[i+1];
    }
    
    // 核心测试：delete heap。
    // 这将调用 ~ThreadHeap()，它应该遍历并 destroy (即 munmap) 所有 5 个 segment。
    // 如果逻辑有误，这里会崩溃或内存泄漏（通过 valgrind 等工具检测）。
    delete heap;
    
    SUCCEED() << "Heap with real segments destructed without crashing.";
}
}