#include <my_malloc/ThreadHeap.hpp>

// 实现文件需要包含所有相关的内部头文件
#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/AllocSlab.hpp>
#include <my_malloc/internal/SlabConfig.hpp>

#include <cassert>
#include <new>      // for placement new
#include <utility>  // for std::move

namespace my_malloc {

// 使用 internal 命名空间可以简化后续代码，在 .cpp 文件中是安全的
using namespace internal;

// #############################################################################
// ### 阶段一：构造与析构 (已实现)                                         ###
// #############################################################################

/**
 * @brief 构造函数实现。
 * 
 * 依赖于头文件中的类内成员初始化，所有成员变量都已具备有效的初始状态。
 * - lock_ 由其默认构造函数初始化。
 * - 原子变量被初始化为 nullptr 和 0。
 * - slab_caches_ 数组中的哨兵节点被初始化为空环。
 * - free_slabs_, active_segments_, free_segments_ 指针被初始化为 nullptr。
 * 
 * 因此，构造函数体为空。
 */
ThreadHeap::ThreadHeap() {
    // 所有初始化均由头文件中的类内成员初始化完成。
}

/**
 * @brief 析构函数实现。
 * 
 * 这是防止内存泄漏的关键。我们必须遍历 active 和 free 两个 Segment 链表，
 * 并将每一个 Segment 通过 munmap 归还给操作系统。
 */
ThreadHeap::~ThreadHeap() {
    // 定义一个辅助 lambda 函数来销毁整个 segment 链表
    auto destroy_segment_list = [](MappedSegment* list_head) {
        MappedSegment* current = list_head;
        while (current) {
            MappedSegment* next_to_destroy = current;
            current = current->list_node.next;
            MappedSegment::destroy(next_to_destroy);
        }
    };

    // 销毁所有活跃的 Segments
    destroy_segment_list(active_segments_);
    active_segments_ = nullptr;

    // 销毁所有空闲的 Segments
    destroy_segment_list(free_segments_);
    free_segments_ = nullptr;

    // 此处无需处理 pending_free_list_，因为在析构时，
    // 所有关联的 Segment 都将被销毁，其中的内存自然失效。
    // 强制处理可能导致访问已被 munmap 的内存。
}


// #############################################################################
// ### 函数实现占位符 (待后续阶段填充)                                     ###
// #############################################################################

// in: src/ThreadHeap.cpp

void* ThreadHeap::allocate(size_t size) {
    // 0. 处理无效请求
    if (size == 0) {
        return nullptr;
    }

    // (我们将在下一个阶段实现小对象分配，这里先跳过)
    // if (size <= MAX_SMALL_OBJECT_SIZE) {
    //     // ... 小对象分配逻辑 ...
    // }

    // ==========================================================
    // ### 大对象分配逻辑 (Large Object Allocation) ###
    // ==========================================================

    // 1. 计算满足 size 需要多少个页
    //    除了 size 本身，我们还需要一个 PageDescriptor 的空间来跟踪这次分配，
    //    但 PageDescriptor 已经存在于 MappedSegment 的元数据中了，所以这里不需要额外空间。
    //    我们只需要计算 size 跨越了多少个页。
    const size_t num_pages = (size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;

    // 2. 调用 acquire_slab 获取连续的内存页
    //    我们直接将计算出的页数传递给我们已经测试过的核心函数。
    void* ptr = acquire_slab(static_cast<uint16_t>(num_pages));

    // 3. 返回指针
    //    acquire_slab 已经处理了所有失败情况（返回 nullptr）。
    //    我们在这里直接返回它的结果即可。
    return ptr;
}

// ... (rest of the file)

void ThreadHeap::free(void* ptr) {
    // 公共的 free 接口。目前是单线程，所以直接调用内部实现。
    internal_free(ptr);
}

void ThreadHeap::push_pending_free(void* ptr) {
    // TODO: 阶段五实现
}

void ThreadHeap::internal_free(void* ptr) {
    if (ptr == nullptr) {
        return;
    }

    // 1. 根据指针找到它所属的 MappedSegment
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(ptr);

    // (在这里可以添加一个检查，确保 segment->get_owner_heap() == this)

    // 2. 从 Segment 中找到管理这个指针的 PageDescriptor
    internal::PageDescriptor* start_desc = segment->page_descriptor_from_ptr(ptr);

    // 3. 检查这是否是一个合法的、已分配的大对象起始地址
    if (start_desc->status != internal::PageStatus::LARGE_SLAB_START) {
        // 错误：用户尝试释放一个无效的指针 (不是分配的起始地址，或者是其他类型的内存)
        // 在生产环境中，这里应该记录错误或使程序崩溃。
        // assert(false && "Invalid pointer passed to free()");
        return;
    }

    // 4. 从描述符中获取它的大小
    uint16_t num_pages = start_desc->num_pages;

    // 5. 遍历这个 slab 占用的所有页，将它们的 PageDescriptor 状态重置为 FREE
    for (uint16_t i = 0; i < num_pages; ++i) {
        char* current_page_ptr = static_cast<char*>(ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* desc = segment->page_descriptor_from_ptr(current_page_ptr);
        desc->status = internal::PageStatus::FREE;
        // (我们暂时不清空 num_pages 和 slab_ptr 字段，保持简单)
    }
    
    // (在这个版本中，我们不做任何内存复用，所以没有对 release_slab 的调用)
}

void ThreadHeap::process_pending_frees() {
    // TODO: 阶段五实现
}

// file: src/ThreadHeap.cpp

void* ThreadHeap::acquire_slab(uint16_t num_pages) {
    if (num_pages == 0 || num_pages > (internal::SEGMENT_SIZE / internal::PAGE_SIZE)) {
        return nullptr;
    }

    // ===============================================
    // ### 优先级 2: 从 active_segments_ 切割 ###
    // ===============================================
    // 遍历当前所有的 active segment，尝试在其中找到空间
    internal::MappedSegment* current_seg = active_segments_;
    while (current_seg) {
        // 使用你已经实现的、简单的线性分配函数
        void* slab = current_seg->linear_allocate_pages(num_pages, internal::PageStatus::LARGE_SLAB_START, internal::PageStatus::LARGE_SLAB_CONT);
        if (slab != nullptr) {
            // 在现有的 segment 中成功找到了空间，直接返回
            return slab;
        }
        // 当前 segment 空间不足 (水位线太高)，继续检查下一个
        current_seg = current_seg->list_node.next;
    }

    // ===========================================
    // ### 优先级 3: 启用 free_segments_ ###
    // ===========================================
    // (我们暂时不实现这个，因为 free_segments_ 链表目前为空，
    // 只有在实现了 free 功能后它才会有内容。所以这部分逻辑暂时可以省略)


    // ======================================================
    // ### 优先级 4: 向 OS 申请新 Segment ###
    // ======================================================
    // 如果所有现有的 active segment 都满了，就需要创建一个新的
    internal::MappedSegment* new_seg = internal::MappedSegment::create();
    if (new_seg == nullptr) {
        return nullptr; // 系统内存耗尽
    }
    
    new_seg->set_owner_heap(this);

    // 将新 segment 加入到 active 链表的头部
    new_seg->list_node.next = active_segments_;
    active_segments_ = new_seg;

    // 在这个全新的 segment 中进行分配
    void* slab = new_seg->linear_allocate_pages(num_pages, internal::PageStatus::LARGE_SLAB_START, internal::PageStatus::LARGE_SLAB_CONT);
    
    if (slab == nullptr) {
        // 防御性代码：请求的大小对于一个新 segment 也太大了
        active_segments_ = new_seg->list_node.next; // 撤销操作
        internal::MappedSegment::destroy(new_seg);
        return nullptr;
    }
    
    return slab;
}


void ThreadHeap::release_slab(void* slab_ptr, uint16_t num_pages) {
    // TODO: 阶段四实现
}

SmallSlabHeader* ThreadHeap::allocate_small_slab(size_t class_id) {
    // TODO: 阶段三/四实现
    return nullptr;
}


} // namespace my_malloc