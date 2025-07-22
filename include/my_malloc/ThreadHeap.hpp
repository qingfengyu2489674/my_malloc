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
        SmallSlabHeader list_head;
        SlabCache() : list_head() {}
    };

    struct PendingFreeNode {
        PendingFreeNode* next;
    };


    std::mutex lock_;

    std::atomic<PendingFreeNode*> pending_free_list_head_{nullptr};
    std::atomic<size_t> pending_free_count_{0};

    SlabCache slab_caches_[MAX_NUM_SIZE_CLASSES];
    LargeSlabHeader* free_slabs_[SEGMENT_SIZE / PAGE_SIZE]{};

    MappedSegment* active_segments_{nullptr};
    MappedSegment* huge_segments_{nullptr};

    void* allocate_from_small_slab_cache(size_t class_id);
    void* allocate_huge_slab(size_t size);


    void process_pending_frees();

    SmallSlabHeader* allocate_small_slab(size_t class_id);
    void* allocate_large_slab(uint16_t num_pages);
    void* acquire_pages(uint16_t num_pages);

    LargeSlabHeader* initialize_as_free_slab(void* slab_ptr, uint16_t num_pages);

    void free_huge_slab(MappedSegment* segment);
    void free_large_slab(void* slab_ptr);
    void free_in_small_slab(void* ptr, SmallSlabHeader* header);

    void* split_slab(LargeSlabHeader* slab_to_split, uint16_t required_pages);
    void release_slab(void* slab_ptr, uint16_t num_pages);

    void prepend_to_freelist(LargeSlabHeader* node_to_add);
    void remove_from_freelist(LargeSlabHeader* node_to_remove);
};

} // namespace my_malloc