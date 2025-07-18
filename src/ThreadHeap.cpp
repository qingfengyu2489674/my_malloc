#include <my_malloc/ThreadHeap.hpp>

#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/AllocSlab.hpp>
#include <my_malloc/internal/SlabConfig.hpp>

#include <cassert>
#include <new>
#include <utility>

namespace my_malloc {

using namespace internal;

//==============================================================================
// Public Interface: Constructor, Destructor & Core API
//==============================================================================

ThreadHeap::ThreadHeap() {
}

ThreadHeap::~ThreadHeap() {
    auto destroy_segment_list = [](MappedSegment* list_head) {
        MappedSegment* current = list_head;
        while (current) {
            MappedSegment* next_to_destroy = current;
            current = current->list_node.next;
            MappedSegment::destroy(next_to_destroy);
        }
    };

    destroy_segment_list(active_segments_);
    active_segments_ = nullptr;

    destroy_segment_list(free_segments_);
    free_segments_ = nullptr;

    destroy_segment_list(huge_segments_);
    huge_segments_ = nullptr;
}

void* ThreadHeap::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    const size_t header_size = sizeof(internal::MappedSegment);
    const size_t metadata_pages = (header_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    const size_t huge_object_threshold = available_pages * internal::PAGE_SIZE;

    if (size > huge_object_threshold) {
        const size_t total_size = (header_size + size + internal::PAGE_SIZE - 1) & ~(internal::PAGE_SIZE - 1);
        internal::MappedSegment* huge_seg = internal::MappedSegment::create(total_size);
        if (huge_seg == nullptr) { return nullptr; }

        huge_seg->set_owner_heap(this);
        internal::PageDescriptor* desc = &huge_seg->page_descriptors_[0];
        desc->status = internal::PageStatus::HUGE_SLAB;

        {   
            std::lock_guard<std::mutex> guard(huge_segments_lock_);
            huge_seg->list_node.next = huge_segments_;
            huge_segments_ = huge_seg;
        }

        return reinterpret_cast<char*>(huge_seg) + header_size;
    } 
    else if (size > internal::MAX_SMALL_OBJECT_SIZE){ 
        const size_t num_pages = (size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
        void* ptr = acquire_large_slab(static_cast<uint16_t>(num_pages));
        return ptr;
    }
    else {
        std::lock_guard<std::mutex> guard(lock_);

        const auto& config = internal::SlabConfig::get_instance();
        size_t class_id = config.get_size_class_index(size);
        SlabCache& cache = slab_caches_[class_id];
        
        if (cache.list_head.next != &cache.list_head) {
            internal::SmallSlabHeader* slab = cache.list_head.next;
            void* ptr = slab->allocate_block();

            if (slab->is_full()) {
                slab->prev->next = slab->next;
                slab->next->prev = slab->prev;
                slab->next = nullptr;
                slab->prev = nullptr;
            }
            
            return ptr;
        }

        internal::SmallSlabHeader* new_slab = allocate_small_slab(class_id);
        if (new_slab == nullptr) {
            return nullptr; 
        }

        new_slab->next = cache.list_head.next;
        new_slab->prev = &cache.list_head;
        cache.list_head.next->prev = new_slab;
        cache.list_head.next = new_slab;

        void* ptr = new_slab->allocate_block();
        if (new_slab->is_full()) {
            new_slab->prev->next = new_slab->next;
            new_slab->next->prev = new_slab->prev;
            new_slab->next = nullptr;
            new_slab->prev = nullptr;
        }
        return ptr;
    }
}

void ThreadHeap::free(void* ptr) {
    if (ptr == nullptr) { return; }

    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc0 = &segment->page_descriptors_[0];

    if (desc0->status == internal::PageStatus::HUGE_SLAB) {
        { 
            std::lock_guard<std::mutex> guard(huge_segments_lock_);
            if (huge_segments_ == segment) {
                huge_segments_ = segment->list_node.next;
            } else {
                for (internal::MappedSegment* curr = huge_segments_; curr && curr->list_node.next; curr = curr->list_node.next) {
                    if (curr->list_node.next == segment) {
                        curr->list_node.next = segment->list_node.next;
                        break;
                    }
                }
            }
        } 
        internal::MappedSegment::destroy(segment);
    } else {
        std::lock_guard<std::mutex> guard(lock_);
        slab_free(ptr);
    }
}

void ThreadHeap::push_pending_free(void* ptr) {
}

//==============================================================================
// Private Implementation: Helper Functions
//==============================================================================

void ThreadHeap::slab_free(void* ptr) {
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc = segment->page_descriptor_from_ptr(ptr);

    switch (desc->status) {
        case internal::PageStatus::SMALL_SLAB_START:
        case internal::PageStatus::SMALL_SLAB_CONT: {

            auto* slab = reinterpret_cast<internal::SmallSlabHeader*>(desc->slab_ptr);
            assert(slab != nullptr);

            bool was_full = slab->is_full();

            slab->free_block(ptr);

            if (slab->is_empty()) {

                slab->prev->next = slab->next;
                slab->next->prev = slab->prev;
                const auto& config = internal::SlabConfig::get_instance();
                const auto& info = config.get_info(slab->slab_class_id);
                release_slab(slab, info.slab_pages);
            } 

            else if (was_full) {
                size_t class_id = slab->slab_class_id;
                SlabCache& cache = slab_caches_[class_id];

                slab->next = cache.list_head.next;
                slab->prev = &cache.list_head;
                cache.list_head.next->prev = slab;
                cache.list_head.next = slab;
            }
            break;
        }

        case internal::PageStatus::LARGE_SLAB_START: {
 
            uint16_t num_pages = desc->num_pages;
            release_slab(ptr, num_pages);
            break;
        }

        default:
            break;
    }
}

void ThreadHeap::process_pending_frees() {
}

void* ThreadHeap::acquire_large_slab(uint16_t num_pages) {
    void* slab_ptr = acquire_pages(num_pages);
    if (slab_ptr == nullptr) {
        return nullptr;
    }

    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(slab_ptr);
    internal::PageDescriptor* start_desc = segment->page_descriptor_from_ptr(slab_ptr);
    start_desc->status = internal::PageStatus::LARGE_SLAB_START;
    start_desc->num_pages = num_pages;
    start_desc->slab_ptr = reinterpret_cast<internal::AllocSlab*>(slab_ptr);

    for (uint16_t i = 1; i < num_pages; ++i) {
        internal::PageDescriptor* cont_desc = segment->page_descriptor_from_ptr(
            static_cast<char*>(slab_ptr) + i * internal::PAGE_SIZE
        );
        cont_desc->status = internal::PageStatus::LARGE_SLAB_CONT;
        cont_desc->num_pages = 0;
        cont_desc->slab_ptr = reinterpret_cast<internal::AllocSlab*>(slab_ptr);
    }
    
    return slab_ptr;
}

internal::SmallSlabHeader* ThreadHeap::allocate_small_slab(size_t class_id) {
    const auto& config = internal::SlabConfig::get_instance();
    const auto& info = config.get_info(class_id);
    uint16_t num_pages = info.slab_pages;
    if (num_pages == 0) {
        return nullptr; 
    }

    void* slab_ptr = acquire_pages(num_pages);
    if (slab_ptr == nullptr) {
        return nullptr;
    }
    
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(slab_ptr);
    internal::SmallSlabHeader* slab_header = new (slab_ptr) internal::SmallSlabHeader(class_id);

    for (uint16_t i = 0; i < num_pages; ++i) {
        internal::PageDescriptor* desc = segment->page_descriptor_from_ptr(
            static_cast<char*>(slab_ptr) + i * internal::PAGE_SIZE
        );
        desc->status = (i == 0) ? internal::PageStatus::SMALL_SLAB_START : internal::PageStatus::SMALL_SLAB_CONT;
        desc->num_pages = num_pages;
        desc->slab_ptr = reinterpret_cast<internal::AllocSlab*>(slab_header);
    }

    return slab_header;
}

void* ThreadHeap::acquire_pages(uint16_t num_pages) {
    if (num_pages == 0 || num_pages > (internal::SEGMENT_SIZE / internal::PAGE_SIZE)) {
        return nullptr;
    }

    internal::MappedSegment* current_seg = active_segments_;
    while (current_seg) {
        void* pages = current_seg->linear_allocate_pages(num_pages); 
        if (pages != nullptr) {
            return pages;
        }
        current_seg = current_seg->list_node.next;
    }

    internal::MappedSegment* new_seg = internal::MappedSegment::create();
    if (new_seg == nullptr) {
        return nullptr;
    }
    
    new_seg->set_owner_heap(this);
    new_seg->list_node.next = active_segments_;
    active_segments_ = new_seg;

    void* pages = new_seg->linear_allocate_pages(num_pages);
    
    if (pages == nullptr) {
        active_segments_ = new_seg->list_node.next;
        internal::MappedSegment::destroy(new_seg);
        return nullptr;
    }
    
    return pages;
}

void ThreadHeap::release_slab(void* slab_ptr, uint16_t num_pages) {
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(slab_ptr);
    
    for (uint16_t i = 0; i < num_pages; ++i) {
        char* current_page_ptr = static_cast<char*>(slab_ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* desc = segment->page_descriptor_from_ptr(current_page_ptr);
        desc->status = internal::PageStatus::FREE;
        // 在后续阶段，这里还需要清空 desc->slab_ptr 和 desc->num_pages
    }
}

} // namespace my_malloc