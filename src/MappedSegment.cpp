#include <my_malloc/internal/MappedSegment.hpp>

#include <new>
#include <cassert>

namespace my_malloc {
namespace internal {

constexpr size_t MMAP_BUFFER_SIZE = SEGMENT_SIZE + (SEGMENT_SIZE - PAGE_SIZE);


MappedSegment::MappedSegment() : owner_heap_(nullptr) {
    const size_t metadata_size = sizeof(MappedSegment);
    const size_t num_metadata_pages = (metadata_size + PAGE_SIZE - 1) / PAGE_SIZE;

    page_descriptors_[0].status = PageStatus::METADATA;
    page_descriptors_[0].slab_ptr = this;

    for (size_t i = 1; i < num_metadata_pages; ++i) {
        page_descriptors_[i].status = PageStatus::METADATA;
        page_descriptors_[i].slab_ptr = this;
    }
}


MappedSegment::~MappedSegment() {
}

MappedSegment* MappedSegment::create(size_t segment_size /* = SEGMENT_SIZE */) {
    // 1. 使用“过量申请再裁剪”技巧来保证 SEGMENT_SIZE 对齐
    const size_t mmap_buffer_size = segment_size + (SEGMENT_SIZE - PAGE_SIZE);

    void* base_ptr = mmap(nullptr, mmap_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base_ptr == MAP_FAILED) {
        return nullptr;
    }

    // 2. 计算对齐后的地址
    uintptr_t base_addr_val = reinterpret_cast<uintptr_t>(base_ptr);
    uintptr_t aligned_addr_val = (base_addr_val + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1);
    void* aligned_ptr = reinterpret_cast<void*>(aligned_addr_val);

    // 3. 裁剪掉头尾多余的部分
    size_t head_trim_size = aligned_addr_val - base_addr_val;
    if (head_trim_size > 0) {
        munmap(base_ptr, head_trim_size);
    }
    
    size_t tail_trim_size = (base_addr_val + mmap_buffer_size) - (aligned_addr_val + segment_size);
    if (tail_trim_size > 0) {
        void* tail_start = reinterpret_cast<void*>(aligned_addr_val + segment_size);
        munmap(tail_start, tail_trim_size);
    }

    // 4. 使用 placement new 构造对象
    MappedSegment* segment = new (aligned_ptr) MappedSegment();

    // 5. 让 Segment 记住自己的总大小
    segment->total_size_ = segment_size;

    return segment;
}


// ====================================================================
// ### 更新 destroy() 以处理可变大小 ###
// ====================================================================
void MappedSegment::destroy(MappedSegment* segment) {
    if (segment) {
        // 从 segment 自身获取需要 munmap 的大小
        size_t total_size = segment->total_size_;
        segment->~MappedSegment();
        ::munmap(segment, total_size);
    }
}


// in: src/MappedSegment.cpp

/**
 * @brief [重构后] 使用简单的线性分配器划分出一块连续的页面。
 * 
 * 此函数的唯一职责是找到下一个可用的、包含 num_pages 个页面的连续块。
 * 它 **不会** 设置所分配页面的状态，这个责任属于调用者。
 * 它 **会** 处理 Segment 自身元数据页的一次性初始化。
 * 
 * @param num_pages 要连续分配的页面数量。
 * @return 指向已分配页面起始位置的指针；如果空间不足，则返回 nullptr。
 */
void* MappedSegment::linear_allocate_pages(uint16_t num_pages) {
    // 检查是否还有足够的剩余空间。
    if (next_free_page_idx_ + num_pages > (SEGMENT_SIZE / PAGE_SIZE)) {
        return nullptr;
    }
    
    // 如果这是第一次从这个 Segment 分配，我们必须首先为 MappedSegment 元数据本身留出空间。
    if (next_free_page_idx_ == 0) {
        const size_t metadata_pages = (sizeof(MappedSegment) + PAGE_SIZE - 1) / PAGE_SIZE;

        // 标记元数据页。这是 MappedSegment 的内部事务。
        page_descriptors_[0].status = PageStatus::METADATA;
        for (size_t i = 1; i < metadata_pages; ++i) {
            page_descriptors_[i].status = PageStatus::METADATA;
        }
        
        // 将“水位线”移过元数据区域。
        next_free_page_idx_ = metadata_pages;

        // 在留出元数据空间后，再次检查空间是否充足。
        if (next_free_page_idx_ + num_pages > (SEGMENT_SIZE / PAGE_SIZE)) {
            return nullptr;
        }
    }

    // 分配空间
    const uint16_t start_page_idx = next_free_page_idx_;
    void* allocated_pages = reinterpret_cast<char*>(this) + (start_page_idx * PAGE_SIZE);

    // 更新下一个空闲页的索引。
    next_free_page_idx_ += num_pages;

    // 返回纯净的、未被标记状态的内存指针。
    return allocated_pages;
}

} // namespace internal
} // namespace my_malloc