#include <my_malloc/ThreadHeap.hpp>

#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/AllocSlab.hpp>
#include <my_malloc/internal/SlabConfig.hpp>

#include <cassert>
#include <new>
#include <utility>
#include <cstring>

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

    destroy_segment_list(huge_segments_);
    huge_segments_ = nullptr;
}

void* ThreadHeap::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    const size_t segment_header_size = sizeof(internal::MappedSegment);
    const size_t segment_metadata = (segment_header_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;

    const size_t large_slab_header = sizeof(internal::LargeSlabHeader);
    const size_t large_slab_metadata = (large_slab_header + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;

    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - segment_metadata - large_slab_metadata;
    const size_t huge_object_threshold = available_pages * internal::PAGE_SIZE;

    if (size > huge_object_threshold) {
        const size_t total_size = (segment_header_size + size + internal::PAGE_SIZE - 1) & ~(internal::PAGE_SIZE - 1);
        internal::MappedSegment* huge_seg = internal::MappedSegment::create(total_size);
        if (huge_seg == nullptr) { return nullptr; }

        huge_seg->set_owner_heap(this);
        internal::PageDescriptor* desc = &huge_seg->page_descriptors_[0];
        desc->status = internal::PageStatus::HUGE_SLAB;

        {   
            std::lock_guard<std::mutex> guard(lock_);
                    
            huge_seg->list_node.next = huge_segments_;
            huge_seg->list_node.prev = nullptr;

            if (huge_segments_ != nullptr) {
                huge_segments_->list_node.prev = huge_seg;
            }

            huge_segments_ = huge_seg;
        }

        return reinterpret_cast<char*>(huge_seg) + segment_metadata * internal::PAGE_SIZE;
    } 
    else if (size > internal::MAX_SMALL_OBJECT_SIZE){ 
        const size_t total_size = size + sizeof(internal::LargeSlabHeader);
        const size_t num_pages = (total_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
        void* ptr = allocate_large_slab(static_cast<uint16_t>(num_pages));
        return ptr;
    }
    else {
        std::lock_guard<std::mutex> guard(lock_);

        const auto& config = internal::SlabConfig::get_instance();
        size_t class_id = config.get_size_class_index(size);
        SlabCache& cache = slab_caches_[class_id];
        
        if (cache.list_head.next_ != &cache.list_head) {
            internal::SmallSlabHeader* slab = cache.list_head.next_;
            void* ptr = slab->allocate_block();

            if (slab->is_full()) {
                slab->prev_->next_ = slab->next_;
                slab->next_->prev_ = slab->prev_;
                slab->next_ = nullptr;
                slab->prev_ = nullptr;
            }
            
            return ptr;
        }

        internal::SmallSlabHeader* new_slab = allocate_small_slab(class_id);
        if (new_slab == nullptr) {
            return nullptr; 
        }

        new_slab->next_ = cache.list_head.next_;
        new_slab->prev_ = &cache.list_head;
        cache.list_head.next_->prev_ = new_slab;
        cache.list_head.next_ = new_slab;

        void* ptr = new_slab->allocate_block();
        if (new_slab->is_full()) {
            new_slab->prev_->next_ = new_slab->next_;
            new_slab->next_->prev_ = new_slab->prev_;
            new_slab->next_ = nullptr;
            new_slab->prev_ = nullptr;
        }
        return ptr;
    }
}

void ThreadHeap::free(void* ptr) {
    if (ptr == nullptr) { return; }


    internal::MappedSegment* segment = internal::MappedSegment::get_segment(ptr);
    internal::PageDescriptor* desc_at_ptr = segment->get_page_desc(ptr);

    if (segment->page_descriptors_[0].status == internal::PageStatus::HUGE_SLAB) {

        {
            std::lock_guard<std::mutex> guard(lock_);

            MappedSegment* prev_node = segment->list_node.prev;
            MappedSegment* next_node = segment->list_node.next;

            if (prev_node != nullptr) {
                prev_node->list_node.next = next_node;
            } 
            else {
                assert(huge_segments_ == segment);
                huge_segments_ = next_node;
            }

            if (next_node != nullptr) {
                next_node->list_node.prev = prev_node;
            }
        } 
        internal::MappedSegment::destroy(segment);
        return;
    }

    void* header_ptr = desc_at_ptr->slab_ptr;
    if (header_ptr == nullptr) {
        return;
    }

    internal::PageDescriptor* desc_at_header = segment->get_page_desc(header_ptr);

    std::lock_guard<std::mutex> guard(lock_);

    switch (desc_at_header->status) {
        case internal::PageStatus::LARGE_SLAB: {
            auto* header = static_cast<internal::LargeSlabHeader*>(header_ptr);
            release_slab(header_ptr, header->num_pages_);
            break;
        }

        case internal::PageStatus::SMALL_SLAB: {
            auto* header = reinterpret_cast<internal::SmallSlabHeader*>(header_ptr);
            
            bool was_full = header->is_full();
            header->free_block(ptr);

            if (header->is_empty()) {
   
                if (header->prev_ != nullptr && header->next_ != nullptr) {
                    header->prev_->next_ = header->next_;
                    header->next_->prev_ = header->prev_;
                }

                const auto& config = internal::SlabConfig::get_instance();
                const auto& info = config.get_info(header->slab_class_id_);
                release_slab(header, info.slab_pages);
            } 
            else if (was_full) {
                size_t class_id = header->slab_class_id_;
                SlabCache& cache = slab_caches_[class_id];

                header->next_ = cache.list_head.next_;
                header->prev_ = &cache.list_head;
                cache.list_head.next_->prev_ = header;
                cache.list_head.next_ = header;
            }
            break;
        }
        
        default:
             break;
    }
}

void ThreadHeap::push_pending_free(void* /*ptr*/) {
}



void ThreadHeap::process_pending_frees() {
}

void* ThreadHeap::allocate_large_slab(uint16_t num_pages) {
    void* header_ptr = acquire_pages(num_pages);
    if (header_ptr == nullptr) {
        return nullptr;
    }

    internal::MappedSegment* segment = internal::MappedSegment::get_segment(header_ptr);
    for (uint16_t i = 0; i < num_pages; ++i) {
        internal::PageDescriptor* desc = segment->get_page_desc(
            reinterpret_cast<char*>(header_ptr) + i * internal::PAGE_SIZE
        );
        desc->status = internal::PageStatus::LARGE_SLAB;
        desc->slab_ptr = header_ptr;
    }
    
    auto* header = static_cast<internal::LargeSlabHeader*>(header_ptr);
    header->num_pages_ = num_pages;
    header->prev = nullptr;
    header->next_ = nullptr;

    void* user_ptr = static_cast<char*>(header_ptr) + sizeof(internal::LargeSlabHeader);
    
    return user_ptr;
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
    
    internal::MappedSegment* segment = internal::MappedSegment::get_segment(slab_ptr);
    internal::SmallSlabHeader* slab_header = new (slab_ptr) internal::SmallSlabHeader(class_id);

    for (uint16_t i = 0; i < num_pages; ++i) {
        internal::PageDescriptor* desc = segment->get_page_desc(
            static_cast<char*>(slab_ptr) + i * internal::PAGE_SIZE
        );
        desc->status = internal::PageStatus::SMALL_SLAB;
        desc->slab_ptr = slab_header;
    }

    return slab_header;
}

void* ThreadHeap::split_slab(internal::LargeSlabHeader* slab_to_split, uint16_t required_pages) {
    uint16_t total_pages = slab_to_split->num_pages_;
    
    uint16_t remaining_pages = total_pages - required_pages;

    if (remaining_pages > 0) {
        void* remaining_slab_ptr = reinterpret_cast<char*>(slab_to_split) + required_pages * internal::PAGE_SIZE;
        
        internal::LargeSlabHeader* large_slab = initialize_as_free_slab(remaining_slab_ptr, remaining_pages);
        prepend_to_freelist(large_slab);
    }

    return slab_to_split;
}

void* ThreadHeap::acquire_pages(uint16_t num_pages) {
    if (num_pages == 0 || num_pages > (internal::SEGMENT_SIZE / internal::PAGE_SIZE)) {
        return nullptr;
    }

    size_t list_idx = num_pages - 1;
    if (free_slabs_[list_idx] != nullptr) {
    
        internal::LargeSlabHeader* node_to_reuse = free_slabs_[list_idx];
        
        free_slabs_[list_idx] = node_to_reuse->next_;
        if (node_to_reuse->next_ != nullptr) {
            node_to_reuse->next_->prev = nullptr;
        }
        
        return node_to_reuse;
    }

       for (size_t i = num_pages; i < internal::SEGMENT_SIZE / internal::PAGE_SIZE; ++i) {
        if (free_slabs_[i] != nullptr) {
            internal::LargeSlabHeader* slab_to_split = free_slabs_[i];

            free_slabs_[i] = slab_to_split->next_;
            if (slab_to_split->next_ != nullptr) {
                slab_to_split->next_->prev = nullptr;
            }

            return split_slab(slab_to_split, num_pages);
        }
    }

    internal::MappedSegment* new_seg = internal::MappedSegment::create();
    if (new_seg == nullptr) {
        return nullptr;
    }
    
    new_seg->set_owner_heap(this);

    new_seg->list_node.next = active_segments_;
    new_seg->list_node.prev = nullptr;

    if (active_segments_ != nullptr) {
        active_segments_->list_node.prev = new_seg;
    }
    active_segments_ = new_seg;

    const size_t metadata_pages = (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    void* slab_start_ptr = reinterpret_cast<char*>(new_seg) + metadata_pages * internal::PAGE_SIZE;
    const uint16_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    
    internal::LargeSlabHeader* large_slab = initialize_as_free_slab(slab_start_ptr, available_pages);

    void* ret_slab = split_slab(large_slab, num_pages);
    
    if (ret_slab == nullptr) {
        active_segments_ = new_seg->list_node.next;
        internal::MappedSegment::destroy(new_seg);
        return nullptr;
    }
    
    return ret_slab;
}

void ThreadHeap::prepend_to_freelist(internal::LargeSlabHeader* node_to_add) {
    uint16_t num_pages = node_to_add->num_pages_;
    if (num_pages == 0) {
        return;
    }
    size_t list_idx = num_pages - 1;

    internal::LargeSlabHeader* current_head = free_slabs_[list_idx];

    node_to_add->next_ = current_head;
    node_to_add->prev = nullptr;

    if (current_head != nullptr) {
        current_head->prev = node_to_add;
    }

    free_slabs_[list_idx] = node_to_add;
}


internal::LargeSlabHeader* ThreadHeap::initialize_as_free_slab(void* slab_ptr, uint16_t num_pages) {
    internal::MappedSegment* segment = internal::MappedSegment::get_segment(slab_ptr);
    
    for (uint16_t i = 0; i < num_pages; ++i) {
        char* current_page_ptr = static_cast<char*>(slab_ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* desc = segment->get_page_desc(current_page_ptr);
 
        desc->status = internal::PageStatus::FREE;
        desc->slab_ptr = slab_ptr;
    }

    memset(slab_ptr, 0, sizeof(internal::LargeSlabHeader));
    internal::LargeSlabHeader* node = new (slab_ptr) internal::LargeSlabHeader();
    node->num_pages_ = num_pages;
    return node;
}

void ThreadHeap::release_slab(void* slab_ptr, uint16_t num_pages) {
    internal::MappedSegment* segment = internal::MappedSegment::get_segment(slab_ptr);
    const size_t segment_start_addr = reinterpret_cast<size_t>(segment);
    const size_t segment_end_addr = segment_start_addr + internal::SEGMENT_SIZE;
    const size_t metadata_end_addr = segment_start_addr + (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE * internal::PAGE_SIZE;

    void* next_page_ptr = static_cast<char*>(slab_ptr) + num_pages * internal::PAGE_SIZE;
    if (reinterpret_cast<size_t>(next_page_ptr) < segment_end_addr) {
        internal::PageDescriptor* next_desc = segment->get_page_desc(next_page_ptr);
        if (next_desc->status == internal::PageStatus::FREE) {
            auto* next_slab_header = static_cast<internal::LargeSlabHeader*>(next_desc->slab_ptr);
            remove_from_freelist(next_slab_header);
            num_pages += next_slab_header->num_pages_;
        }
    }

    if (reinterpret_cast<size_t>(slab_ptr) > metadata_end_addr) {
        void* prev_page_ptr = static_cast<char*>(slab_ptr) - internal::PAGE_SIZE;
        internal::PageDescriptor* prev_desc = segment->get_page_desc(prev_page_ptr);
        if (prev_desc->status == internal::PageStatus::FREE) {
            auto* prev_slab_header = static_cast<internal::LargeSlabHeader*>(prev_desc->slab_ptr);
            remove_from_freelist(prev_slab_header);
            num_pages += prev_slab_header->num_pages_;
            slab_ptr = prev_slab_header;
        }
    }

    internal::LargeSlabHeader* final_slab = initialize_as_free_slab(slab_ptr, num_pages);
    prepend_to_freelist(final_slab);
}

void ThreadHeap::remove_from_freelist(internal::LargeSlabHeader* node_to_remove) {
    if (!node_to_remove) return;

    uint16_t num_pages = node_to_remove->num_pages_;
    if (num_pages == 0) return;
    size_t list_idx = num_pages - 1;

    if (node_to_remove->prev) {
        // Not the head of the list
        node_to_remove->prev->next_ = node_to_remove->next_;
    } else {
        // It is the head of the list
        free_slabs_[list_idx] = node_to_remove->next_;
    }

    if (node_to_remove->next_) {
        node_to_remove->next_->prev = node_to_remove->prev;
    }
}

} // namespace my_malloc