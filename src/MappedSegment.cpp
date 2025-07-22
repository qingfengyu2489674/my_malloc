#include <my_malloc/internal/MappedSegment.hpp>

#include <new>
#include <cassert>

namespace my_malloc {

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
    const size_t mmap_buffer_size = segment_size + (SEGMENT_SIZE - PAGE_SIZE);

    void* base_ptr = mmap(nullptr, mmap_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base_ptr == MAP_FAILED) {
        return nullptr;
    }

    uintptr_t base_addr_val = reinterpret_cast<uintptr_t>(base_ptr);
    uintptr_t aligned_addr_val = (base_addr_val + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1);
    void* aligned_ptr = reinterpret_cast<void*>(aligned_addr_val);

    size_t head_trim_size = aligned_addr_val - base_addr_val;
    if (head_trim_size > 0) {
        munmap(base_ptr, head_trim_size);
    }
    
    size_t tail_trim_size = (base_addr_val + mmap_buffer_size) - (aligned_addr_val + segment_size);
    if (tail_trim_size > 0) {
        void* tail_start = reinterpret_cast<void*>(aligned_addr_val + segment_size);
        munmap(tail_start, tail_trim_size);
    }

    MappedSegment* segment = new (aligned_ptr) MappedSegment();

    segment->total_size_ = segment_size;

    return segment;
}


void MappedSegment::destroy(MappedSegment* segment) {
    if (segment) {
        size_t total_size = segment->total_size_;
        segment->~MappedSegment();
        ::munmap(segment, total_size);
    }
}



} // namespace my_malloc