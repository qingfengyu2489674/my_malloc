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
        const size_t total_size = size + sizeof(internal::LargeSlabHeader);
        const size_t num_pages = (total_size + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
        void* ptr = acquire_large_slab(static_cast<uint16_t>(num_pages));
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

    // 1. 【修复点 1】: 对于 Huge Object，ptr 可能不在一个标准的 2MB Segment 中
    //    我们需要先检查它是否是一个 Huge Object。
    //    一个简单的方法是，Huge Object 的 ptr 减去 MappedSegment header size
    //    应该等于一个 MappedSegment 的基地址。
    //    更通用的方法是，我们需要一种方式来识别 Huge Object。
    //    最好的方式就是通过 PageDescriptor。
    
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc_at_ptr = segment->page_descriptor_from_ptr(ptr);

    // --- 【修复点 1 - 开始】: 专门处理 Huge Slab ---
    // Huge Slab 的 page descriptor 只有第一页被设置，且没有 slab_ptr。
    // 我们需要通过计算来找到 Segment 头部，然后检查第一页的状态。
    if (segment->page_descriptors_[0].status == internal::PageStatus::HUGE_SLAB) {
        // 确认我们找到的是正确的 Huge Segment
        // 我们不能依赖 slab_ptr，因为 Huge Object 没有设置它
        {
            std::lock_guard<std::mutex> guard(huge_segments_lock_);
            // 从 huge_segments_ 链表中移除
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
        return; // 处理完毕，直接返回
    }
    // --- 【修复点 1 - 结束】 ---


    // --- 对于 Small 和 Large Slab 的统一处理逻辑 ---

    // 通过 desc->slab_ptr 总是能找到真正的头部地址
    void* header_ptr = desc_at_ptr->slab_ptr;
    if (header_ptr == nullptr) {
        // 可能是无效指针或重复释放 (例如 ptr 指向一个 FREE 页)
        return;
    }

    // 获取头部页的 PageDescriptor 来确认最终状态
    internal::PageDescriptor* desc_at_header = segment->page_descriptor_from_ptr(header_ptr);

    // 加锁以保护 Small/Large Slab 的共享数据结构
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
            header->free_block(ptr); // free_block 接收的是用户指针 ptr

            if (header->is_empty()) {
                // 从 SlabCache 链表中移除
                // 【修复点 2 - 开始】：确保 prev_ 和 next_ 指针有效
                if (header->prev_ != nullptr && header->next_ != nullptr) {
                    header->prev_->next_ = header->next_;
                    header->next_->prev_ = header->prev_;
                }
                // 【修复点 2 - 结束】
                
                // 释放整个 slab
                const auto& config = internal::SlabConfig::get_instance();
                const auto& info = config.get_info(header->slab_class_id_);
                release_slab(header, info.slab_pages);
            } 
            else if (was_full) {
                // 从 full 状态变回 partial，重新加入 SlabCache 链表
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
             // 可能是无效指针、已释放内存 (status=FREE)
             // 安全地忽略
             break;
    }
}

void ThreadHeap::push_pending_free(void* /*ptr*/) {
}

//==============================================================================
// Private Implementation: Helper Functions
//==============================================================================



void ThreadHeap::process_pending_frees() {
}

void* ThreadHeap::acquire_large_slab(uint16_t num_pages) {
    // 1. 获取内存块的【头部地址】
    void* header_ptr = acquire_pages(num_pages);
    if (header_ptr == nullptr) {
        return nullptr;
    }

    // 2. 格式化 Page Descriptors
    //    所有 PageDescriptor 的 slab_ptr 都应该指向【头部地址】
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(header_ptr);
    for (uint16_t i = 0; i < num_pages; ++i) {
        internal::PageDescriptor* desc = segment->page_descriptor_from_ptr(
            reinterpret_cast<char*>(header_ptr) + i * internal::PAGE_SIZE
        );
        desc->status = internal::PageStatus::LARGE_SLAB;
        desc->slab_ptr = header_ptr;
    }
    
    // 3. 在【头部地址】写入元数据
    auto* header = static_cast<internal::LargeSlabHeader*>(header_ptr);
    header->num_pages_ = num_pages;
    header->prev = nullptr; // 初始化指针
    header->next_ = nullptr;

    // 4. 【关键】：计算并返回【用户地址】，它在头部的后面
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
    
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(slab_ptr);
    internal::SmallSlabHeader* slab_header = new (slab_ptr) internal::SmallSlabHeader(class_id);

    for (uint16_t i = 0; i < num_pages; ++i) {
        internal::PageDescriptor* desc = segment->page_descriptor_from_ptr(
            static_cast<char*>(slab_ptr) + i * internal::PAGE_SIZE
        );
        desc->status = internal::PageStatus::SMALL_SLAB;
        desc->slab_ptr = slab_header;
    }

    return slab_header;
}

void* ThreadHeap::split_slab(internal::LargeSlabHeader* slab_to_split, uint16_t required_pages) {
    uint16_t total_pages = slab_to_split->num_pages_;
    
    // 计算剩余的页数
    uint16_t remaining_pages = total_pages - required_pages;

    if (remaining_pages > 0) {
        // 如果有剩余，计算剩余部分的起始地址
        void* remaining_slab_ptr = reinterpret_cast<char*>(slab_to_split) + required_pages * internal::PAGE_SIZE;
        
        // 将剩余部分格式化为一个新的空闲块并加入 freelist
        // initialize_as_free_slab 会处理所有元数据更新和链表插入
        internal::LargeSlabHeader* large_slab = initialize_as_free_slab(remaining_slab_ptr, remaining_pages);
        prepend_to_freelist(large_slab);
    }

    // 更新被切分出去的这部分内存的 PageDescriptor
    // internal::MappedSegment* segment = internal::MappedSegment::from_ptr(slab_to_split);
    // for (uint16_t i = 0; i < required_pages; ++i) {
    //     void* page_ptr = reinterpret_cast<char*>(slab_to_split) + i * internal::PAGE_SIZE;
    //     internal::PageDescriptor* desc = segment->page_descriptor_from_ptr(page_ptr);
    //     // 这里可以先简单标记为 LARGE_SLAB_CONT，由调用者 acquire_large_slab 负责更新为 START
    //     if(i == 0){
    //         desc->status = internal::PageStatus::LARGE_SLAB; 
    //     }
    //     else{
    //         desc->status = internal::PageStatus::LARGE_SLAB; 
    //     }

    // }
    
    // 返回切分出的、满足请求的内存块的指针
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

       for (size_t i = num_pages; i < internal::SEGMENT_SIZE / internal::PAGE_SIZE; ++i) { // 从 num_pages+1 (索引 i=num_pages) 开始
        if (free_slabs_[i] != nullptr) {
            // 找到了一个足够大的块
            internal::LargeSlabHeader* slab_to_split = free_slabs_[i];

            // 从其链表中移除
            free_slabs_[i] = slab_to_split->next_;
            if (slab_to_split->next_ != nullptr) {
                slab_to_split->next_->prev = nullptr;
            }

            // 分裂这个块，并返回所需的部分
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
    // 1. 从节点自身获取其大小，以确定要插入到哪个链表
    uint16_t num_pages = node_to_add->num_pages_;
    if (num_pages == 0) {
        // 防止插入一个无效的节点
        return;
    }
    size_t list_idx = num_pages - 1;

    // 2. 获取当前链表的头节点
    internal::LargeSlabHeader* current_head = free_slabs_[list_idx];

    // 3. 设置新节点的指针
    node_to_add->next_ = current_head;
    node_to_add->prev = nullptr; // 新的头节点没有前驱

    // 4. 更新旧的头节点（如果存在）
    if (current_head != nullptr) {
        current_head->prev = node_to_add;
    }

    // 5. 更新数组中的头指针，使其指向新的节点
    free_slabs_[list_idx] = node_to_add;
}


internal::LargeSlabHeader* ThreadHeap::initialize_as_free_slab(void* slab_ptr, uint16_t num_pages) {
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(slab_ptr);
    
    // 遍历这块内存覆盖的所有页面，并按照新规范更新它们的 PageDescriptor
    for (uint16_t i = 0; i < num_pages; ++i) {
        char* current_page_ptr = static_cast<char*>(slab_ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* desc = segment->page_descriptor_from_ptr(current_page_ptr);
        
        // *** MODIFICATION: 按照新要求填充元数据 ***
        desc->status = internal::PageStatus::FREE;
        desc->slab_ptr = slab_ptr;   // 指向整个空闲块的头部
    }

    // 在内存块的起始位置构造 LargeSlabHeader
    memset(slab_ptr, 0, sizeof(internal::LargeSlabHeader));
    internal::LargeSlabHeader* node = new (slab_ptr) internal::LargeSlabHeader();
    node->num_pages_ = num_pages;
    return node;
}

// in ThreadHeap.cpp (替换您现有的 release_slab)

void ThreadHeap::release_slab(void* slab_ptr, uint16_t num_pages) {
    internal::MappedSegment* segment = internal::MappedSegment::from_ptr(slab_ptr);
    const size_t segment_start_addr = reinterpret_cast<size_t>(segment);
    const size_t segment_end_addr = segment_start_addr + internal::SEGMENT_SIZE;
    const size_t metadata_end_addr = segment_start_addr + (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE * internal::PAGE_SIZE;

    // === 向后合并 ===
    void* next_page_ptr = static_cast<char*>(slab_ptr) + num_pages * internal::PAGE_SIZE;
    if (reinterpret_cast<size_t>(next_page_ptr) < segment_end_addr) {
        internal::PageDescriptor* next_desc = segment->page_descriptor_from_ptr(next_page_ptr);
        if (next_desc->status == internal::PageStatus::FREE) {
            auto* next_slab_header = static_cast<internal::LargeSlabHeader*>(next_desc->slab_ptr);
            remove_from_freelist(next_slab_header);
            num_pages += next_slab_header->num_pages_;
        }
    }

    // === 向前合并 ===
    if (reinterpret_cast<size_t>(slab_ptr) > metadata_end_addr) {
        void* prev_page_ptr = static_cast<char*>(slab_ptr) - internal::PAGE_SIZE;
        internal::PageDescriptor* prev_desc = segment->page_descriptor_from_ptr(prev_page_ptr);
        if (prev_desc->status == internal::PageStatus::FREE) {
            auto* prev_slab_header = static_cast<internal::LargeSlabHeader*>(prev_desc->slab_ptr);
            remove_from_freelist(prev_slab_header);
            num_pages += prev_slab_header->num_pages_;
            slab_ptr = prev_slab_header;
        }
    }

    // === 最终格式化与添加 ===
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