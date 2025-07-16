// include/my_malloc/alloc_internals/MappedSegment.hpp
#ifndef MY_MALLOC_ALLOC_INTERNALS_MAPPED_SEGMENT_HPP
#define MY_MALLOC_ALLOC_INTERNALS_MAPPED_SEGMENT_HPP

#include <cstddef>
#include <cstdint>
#include <atomic>

// 包含我们定义的核心常量和辅助结构
#include <my_malloc/alloc_internals/common.hpp>

namespace my_malloc {
namespace internal {

// 前向声明，避免循环依赖
class ThreadHeap;

/**
 * @class MappedSegment
 * @brief 代表一块直接从操作系统 mmap 而来的、自包含元数据的大块内存。
 */
class alignas(SEGMENT_SIZE) MappedSegment {
public:
    // +++ [新增] 将链表指针封装在一个公共的嵌套结构体中 +++
    /**
     * @struct ListNode
     * @brief 用于将 MappedSegment 链接成双向链表的节点。
     *        将其设为 public，允许高性能的外部链表操作。
     */
    struct ListNode {
        MappedSegment* next = nullptr;
        MappedSegment* prev = nullptr;
    };

    // --- 公共数据成员 ---
    ListNode list_node; // ThreadHeap 可以直接访问此成员来操作链表

    // --- 静态工厂与析构方法 ---
    static MappedSegment* create();
    static void destroy(MappedSegment* segment);

    // --- 静态工具 ---
    static MappedSegment* from_ptr(const void* ptr);

    // --- 公共成员接口 ---
    ThreadHeap* get_owner_heap() const { return owner_heap_; }
    void set_owner_heap(ThreadHeap* heap) { owner_heap_ = heap; }
    
    PageDescriptor* page_descriptor_from_ptr(const void* ptr);
    const PageDescriptor* page_descriptor_from_ptr(const void* ptr) const;

private:
    // --- 构造/析构函数设为私有 ---
    MappedSegment();
    ~MappedSegment();

    // --- 禁止拷贝与移动 ---
    MappedSegment(const MappedSegment&) = delete;
    MappedSegment& operator=(const MappedSegment&) = delete;
    MappedSegment(MappedSegment&&) = delete;
    MappedSegment& operator=(MappedSegment&&) = delete;

    // --- 私有数据成员 ---
    ThreadHeap* owner_heap_; // 核心数据，保持私有

    // 页描述符数组，核心元数据，保持私有
    PageDescriptor page_descriptors_[SEGMENT_SIZE / PAGE_SIZE];

    // --- [移除] next_ 和 prev_ 已经移入公共的 list_node 中 ---

    // --- [移除] 不再需要友元声明 ---
    // friend class ThreadHeap; 
};


// --- 内联函数实现 ---

inline MappedSegment* MappedSegment::from_ptr(const void* ptr) {
    return reinterpret_cast<MappedSegment*>(
        (uintptr_t)ptr & ~(SEGMENT_SIZE - 1)
    );
}

inline PageDescriptor* MappedSegment::page_descriptor_from_ptr(const void* ptr) {
    const size_t page_index = ((uintptr_t)this - (uintptr_t)ptr) / PAGE_SIZE; // 修正：应该是 ptr - this
    return &page_descriptors_[page_index];
}

inline const PageDescriptor* MappedSegment::page_descriptor_from_ptr(const void* ptr) const {
    const size_t page_index = ((uintptr_t)ptr - (uintptr_t)this) / PAGE_SIZE;
    return &page_descriptors_[page_index];
}


} // namespace internal
} // namespace my_malloc

#endif // MY_MALLOC_ALLOC_INTERNALS_MAPPED_SEGMENT_HPP