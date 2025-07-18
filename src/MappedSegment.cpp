#include <my_malloc/internal/MappedSegment.hpp>

#include <new>
#include <cassert>

namespace my_malloc {
namespace internal {

constexpr size_t MMAP_BUFFER_SIZE = SEGMENT_SIZE + (SEGMENT_SIZE - PAGE_SIZE);


MappedSegment::MappedSegment() : owner_heap_(nullptr) {
    const size_t metadata_size = sizeof(MappedSegment);
    const size_t num_metadata_pages = (metadata_size + PAGE_SIZE - 1) / PAGE_SIZE;

    page_descriptors_[0].status = PageStatus::METADATA_START;

    // !! 警告 !!：这个指针绝对不能被解引用（dereference）。它只是一个存储整数的容器。
    page_descriptors_[0].slab_ptr = reinterpret_cast<AllocSlab*>(metadata_size);

    for (size_t i = 1; i < num_metadata_pages; ++i) {
        page_descriptors_[i].status = PageStatus::METADATA_CONT;
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


// 用这个新函数替换旧的 find_and_allocate_slab
void* MappedSegment::linear_allocate_pages(uint16_t num_pages, PageStatus start_status, PageStatus cont_status) {
    // 检查是否还有足够的剩余空间 (这部分逻辑不变)
    if (next_free_page_idx_ + num_pages > (SEGMENT_SIZE / PAGE_SIZE)) {
        return nullptr;
    }
    
    // 如果是第一次从这个 Segment 分配，需要跳过元数据区域 (逻辑不变)
    if (next_free_page_idx_ == 0) {
        const size_t metadata_pages = (sizeof(MappedSegment) + PAGE_SIZE - 1) / PAGE_SIZE;

        // 标记元数据页 (使用你设计的新状态)
        page_descriptors_[0].status = PageStatus::METADATA_START;
        for (size_t i = 1; i < metadata_pages; ++i) {
            page_descriptors_[i].status = PageStatus::METADATA_CONT;
        }
        
        next_free_page_idx_ = metadata_pages;

        // 再次检查
        if (next_free_page_idx_ + num_pages > (SEGMENT_SIZE / PAGE_SIZE)) {
            return nullptr;
        }
    }

    // 分配空间 (逻辑不变)
    const uint16_t start_page_idx = next_free_page_idx_;
    void* slab_ptr = reinterpret_cast<char*>(this) + (start_page_idx * PAGE_SIZE);

    // ==========================================================
    // ### 关键改变：使用传入的参数来更新 Page Descriptors ###
    // ==========================================================
    PageDescriptor* start_desc = &page_descriptors_[start_page_idx];
    start_desc->status = start_status; // 使用调用者指定的起始状态
    start_desc->num_pages = num_pages;
    start_desc->slab_ptr = static_cast<AllocSlab*>(slab_ptr); // slab_ptr 的类型应该是 void*

    for (uint16_t i = 1; i < num_pages; ++i) {
        page_descriptors_[start_page_idx + i].status = cont_status; // 使用调用者指定的后续状态
        page_descriptors_[start_page_idx + i].slab_ptr = static_cast<AllocSlab*>(slab_ptr);
    }
    // ==========================================================

    // 更新下一个空闲页的索引 (逻辑不变)
    next_free_page_idx_ += num_pages;

    return slab_ptr;
}

} // namespace internal
} // namespace my_malloc