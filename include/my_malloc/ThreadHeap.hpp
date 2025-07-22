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


class ThreadHeap {
    public:

    ThreadHeap();
    ~ThreadHeap();

    ThreadHeap(const ThreadHeap&) = delete;
    ThreadHeap& operator=(const ThreadHeap&) = delete;
    ThreadHeap(ThreadHeap&&) = delete;
    ThreadHeap& operator=(ThreadHeap&&) = delete;


    void* allocate(size_t size);
    void free(void* ptr);
    void push_pending_free(void* ptr);

// private:

    struct SlabCache {
        internal::SmallSlabHeader list_head;
        SlabCache() : list_head() {}
    };

    struct PendingFreeNode {
        PendingFreeNode* next;
    };


    std::mutex lock_;

    std::atomic<PendingFreeNode*> pending_free_list_head_{nullptr};
    std::atomic<size_t> pending_free_count_{0};

    SlabCache slab_caches_[internal::MAX_NUM_SIZE_CLASSES];
    internal::LargeSlabHeader* free_slabs_[internal::SEGMENT_SIZE / internal::PAGE_SIZE]{};

    internal::MappedSegment* active_segments_{nullptr};
    internal::MappedSegment* huge_segments_{nullptr};


    void process_pending_frees();

    internal::SmallSlabHeader* allocate_small_slab(size_t class_id);
    void* allocate_large_slab(uint16_t num_pages);
    void* acquire_pages(uint16_t num_pages);

    internal::LargeSlabHeader* initialize_as_free_slab(void* slab_ptr, uint16_t num_pages);

    void* split_slab(internal::LargeSlabHeader* slab_to_split, uint16_t required_pages);
    void release_slab(void* slab_ptr, uint16_t num_pages);

    void prepend_to_freelist(internal::LargeSlabHeader* node_to_add);
    void remove_from_freelist(internal::LargeSlabHeader* node_to_remove);
};

} // namespace my_malloc