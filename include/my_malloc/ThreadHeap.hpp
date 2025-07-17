#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>

// Include the new, detailed component headers
#include <my_malloc/internal/AllocSlab.hpp>
#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/SlabConfig.hpp>
#include <my_malloc/internal/definitions.hpp>

namespace my_malloc {

// Forward declare internal components that ThreadHeap uses.
namespace internal {
class MappedSegment;
}

/**
 * @class ThreadHeap
 * @brief 线程私有的内存管理器，是内存分配和释放操作的“大脑”。
 *
 * 每个工作线程都将拥有一个 ThreadHeap 实例，以实现无锁的快速分配。
 * ThreadHeap 负责管理 MappedSegment (从OS获取的大块内存) 和 AllocSlab (用于具体分配的单元)。
 */
class ThreadHeap {
public:
    /**
     * @brief 构造函数。初始化所有内部数据结构。
     */
    ThreadHeap();

    /**
     * @brief 析构函数。必须将所有通过 mmap 获取的内存归还给操作系统。
     */
    ~ThreadHeap();

    // 禁止拷贝和移动，因为 ThreadHeap 管理着独占的内存资源。
    ThreadHeap(const ThreadHeap&) = delete;
    ThreadHeap& operator=(const ThreadHeap&) = delete;
    ThreadHeap(ThreadHeap&&) = delete;
    ThreadHeap& operator=(ThreadHeap&&) = delete;

    /**
     * @brief 公共分配接口。
     * @param size 请求的内存大小（字节）。
     * @return 成功则返回指向分配内存的指针，失败则返回 nullptr。
     */
    void* allocate(size_t size);

    /**
     * @brief [同步] 为源于本 Heap 的内存执行释放操作。
     *
     * 这是一个线程安全的方法，它会自己处理加锁，用于同 Heap 释放。
     * @param ptr 要释放的内存指针。
     */
    void free(void* ptr);

    /**
     * @brief [异步] 将一个待释放指针推入本 Heap 的延迟释放队列（用于跨线程释放）。
     *
     * 这是一个无锁操作，供其他线程调用。
     * @param ptr 要释放的内存指针。
     */
    void push_pending_free(void* ptr);

private:
    // --- 私有内嵌数据结构 ---

    // 用于管理特定尺寸 SmallSlab 的容器，本质是一个双向链表头。
    // 使用哨兵节点简化链表操作。
    struct SlabCache {
        internal::SmallSlabHeader list_head;
        // 默认构造函数已足够，list_head 会被其自己的默认构造函数初始化。
        SlabCache() : list_head() {}
    };

    // 构成无锁延迟释放队列的单向链表节点。
    struct PendingFreeNode {
        PendingFreeNode* next;
    };

    // --- 核心成员变量 ---

    // 保护此 Heap 内部状态的互斥锁。
    std::mutex lock_;

    // --- 跨线程延迟释放队列 ---
    std::atomic<PendingFreeNode*> pending_free_list_head_{nullptr};
    std::atomic<size_t> pending_free_count_{0};

    // --- Small 对象管理 ---
    // 按 size class 索引的 Slab 缓存数组。
    SlabCache slab_caches_[internal::MAX_NUM_SIZE_CLASSES];

    // --- Large 对象及空闲空间管理 ---
    // 一个 FreeSlabNode 链表数组，按下标（代表页数-1）索引。
    // a[i] 链接着所有大小为 (i+1) 页的空闲 Slab。
    internal::FreeSlabNode* free_slabs_[internal::SEGMENT_SIZE / internal::PAGE_SIZE]{};

    // --- Segment 管理 ---
    internal::MappedSegment* active_segments_{nullptr};
    internal::MappedSegment* free_segments_{nullptr};

    // --- 私有辅助函数 (核心逻辑) ---

    /**
     * @brief [实现] 内部的、非线程安全的释放逻辑。
     * 假定调用者已经获取了锁。
     * @param ptr 待释放的指针。
     */
    void internal_free(void* ptr);

    /**
     * @brief 处理积压的跨线程释放请求。
     */
    void process_pending_frees();

    /**
     * @brief 资源获取的核心：按需获取一块指定页数大小的连续内存。
     */
    void* acquire_slab(uint16_t num_pages);

    /**
     * @brief 资源回收的核心：将一块完全空闲的 Slab 归还到系统中。
     */
    void release_slab(void* slab_ptr, uint16_t num_pages);

    /**
     * @brief 为 Small 对象分配一个新的、初始化的 Slab。
     */
    internal::SmallSlabHeader* allocate_small_slab(size_t class_id);
};

} // namespace my_malloc