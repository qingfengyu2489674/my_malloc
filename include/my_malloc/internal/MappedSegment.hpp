// include/my_malloc/alloc_internals/MappedSegment.hpp
#ifndef MY_MALLOC_ALLOC_INTERNALS_MAPPED_SEGMENT_HPP
#define MY_MALLOC_ALLOC_INTERNALS_MAPPED_SEGMENT_HPP

#include <cstddef>
#include <cstdint>
#include <atomic>

#include <my_malloc/internal/definitions.hpp>
#include <my_malloc/sys/mman.hpp>

namespace my_malloc {

class ThreadHeap;

class MappedSegment {
public:
    struct ListNode {
        MappedSegment* next = nullptr;
        MappedSegment* prev = nullptr;
    };

    ListNode list_node;

    static MappedSegment* create(size_t segment_size = SEGMENT_SIZE);
    static void destroy(MappedSegment* segment);

    static MappedSegment* get_segment(const void* ptr);

    ThreadHeap* get_owner_heap() const { 
        return owner_heap_; 
    }
    
    void set_owner_heap(ThreadHeap* heap) { 
        owner_heap_ = heap; 
    }
    
    PageDescriptor* get_page_desc(const void* ptr);
    const PageDescriptor* get_page_desc(const void* ptr) const;
    
// private:

    MappedSegment();
    ~MappedSegment();

    MappedSegment(const MappedSegment&) = delete;
    MappedSegment& operator=(const MappedSegment&) = delete;
    MappedSegment(MappedSegment&&) = delete;
    MappedSegment& operator=(MappedSegment&&) = delete;

    ThreadHeap* owner_heap_;
    PageDescriptor page_descriptors_[SEGMENT_SIZE / PAGE_SIZE];
    
    size_t total_size_;

    uint16_t next_free_page_idx_ = 0;
};


inline MappedSegment* MappedSegment::get_segment(const void* ptr) {
    return reinterpret_cast<MappedSegment*>(
        reinterpret_cast<uintptr_t>(ptr) & ~(SEGMENT_SIZE - 1)
    );
}

inline PageDescriptor* MappedSegment::get_page_desc(const void* ptr) {
    const size_t page_index = (reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(this)) / PAGE_SIZE;
    return &page_descriptors_[page_index];
}

inline const PageDescriptor* MappedSegment::get_page_desc(const void* ptr) const {
    const size_t page_index = (reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(this)) / PAGE_SIZE;
    return &page_descriptors_[page_index];
}

} // namespace my_malloc

#endif // MY_MALLOC_ALLOC_INTERNALS_MAPPED_SEGMENT_HPP