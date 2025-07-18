#include <my_malloc/internal/MappedSegment.hpp>

#include <sys/mman.h>
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
        page_descriptors_[i].status = PageStatus::METADATA_SUBPAGE;
    }
}


MappedSegment::~MappedSegment() {
}

MappedSegment* MappedSegment::create() {
    void* base_ptr = mmap(nullptr, MMAP_BUFFER_SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (base_ptr == MAP_FAILED) {
        return nullptr;
    }

    uintptr_t base_addr_val = reinterpret_cast<uintptr_t>(base_ptr);
    uintptr_t aligned_addr_val = (base_addr_val + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1);
    
    void* aligned_ptr = reinterpret_cast<void*>(aligned_addr_val);
    
    assert(aligned_addr_val >= base_addr_val);
    assert((aligned_addr_val + SEGMENT_SIZE) <= (base_addr_val + MMAP_BUFFER_SIZE));

    size_t head_trim_size = aligned_addr_val - base_addr_val;
    size_t tail_trim_size = (base_addr_val + MMAP_BUFFER_SIZE) - (aligned_addr_val + SEGMENT_SIZE);

    if (head_trim_size > 0) {
        munmap(base_ptr, head_trim_size);
    }
    if (tail_trim_size > 0) {
        void* tail_start = reinterpret_cast<void*>(aligned_addr_val + SEGMENT_SIZE);
        munmap(tail_start, tail_trim_size);
    }

    return new (aligned_ptr) MappedSegment();
}

void MappedSegment::destroy(MappedSegment* segment) {
    if (!segment) {
        return;
    }

    segment->~MappedSegment();
    munmap(segment, SEGMENT_SIZE);
}

void* MappedSegment::find_and_allocate_slab(uint16_t num_pages) {
    // 检查是否还有足够的剩余空间
    if (next_free_page_idx_ + num_pages > (SEGMENT_SIZE / PAGE_SIZE)) {
        return nullptr; // 空间不足
    }
    
    // 如果是第一次从这个 Segment 分配，需要跳过元数据区域
    if (next_free_page_idx_ == 0) {
        // 我们需要计算元数据占了多少页
        const size_t metadata_bytes = sizeof(MappedSegment);
        // 向上取整计算页数
        const size_t metadata_pages = (metadata_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

        // 标记这些页为元数据
        for (size_t i = 0; i < metadata_pages; ++i) {
            page_descriptors_[i].status = PageStatus::METADATA_START;
        }
        
        next_free_page_idx_ = metadata_pages;

        // 再次检查分配是否会超出范围
        if (next_free_page_idx_ + num_pages > (SEGMENT_SIZE / PAGE_SIZE)) {
            return nullptr;
        }
    }

    // 分配空间
    const uint16_t start_page_idx = next_free_page_idx_;
    void* slab_ptr = reinterpret_cast<char*>(this) + (start_page_idx * PAGE_SIZE);

    // 更新 Page Descriptors
    PageDescriptor* start_desc = &page_descriptors_[start_page_idx];
    start_desc->status = PageStatus::SLAB_START;
    start_desc->num_pages = num_pages;
    start_desc->slab_ptr = static_cast<AllocSlab*>(slab_ptr);

    for (uint16_t i = 1; i < num_pages; ++i) {
        page_descriptors_[start_page_idx + i].status = PageStatus::SLAB_SUBPAGE;
        page_descriptors_[start_page_idx + i].slab_ptr = static_cast<AllocSlab*>(slab_ptr);
    }

    // 更新下一个空闲页的索引
    next_free_page_idx_ += num_pages;

    return slab_ptr;
}


} // namespace internal
} // namespace my_malloc