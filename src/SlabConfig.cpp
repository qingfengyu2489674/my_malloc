#include <my_malloc/internal/SlabConfig.hpp>
#include <my_malloc/internal/AllocSlab.hpp>
#include <cassert>
#include <algorithm> // for std::min/max
#include <stddef.h> // For offsetof

namespace my_malloc {

const SlabConfig& SlabConfig::get_instance() {
    // C++11 保证了局部静态变量的初始化是线程安全的，且只执行一次。
    static SlabConfig instance;
    return instance;
}

size_t SlabConfig::get_size_class_index(size_t size) const {
    if (size > MAX_SMALL_OBJECT_SIZE) {
        // 对于超出 small object 范围的请求，返回一个无效索引。
        // 使用 SIZE_MAX 是一个清晰的信号。
        return static_cast<size_t>(-1);
    }

    return size_to_class_map_[size];
}

const SlabConfigInfo& SlabConfig::get_info(size_t index) const {
    assert(index < num_classes_ && "Size class index out of bounds.");
    return slab_class_infos_[index];
}

SlabConfig::SlabConfig() : num_classes_(0) {
    initialize_size_classes();
    calculate_derived_parameters();
    build_lookup_table();
}


void SlabConfig::initialize_size_classes() {
    auto add_class = [&](size_t block_size, uint16_t base_slab_pages) {
        if (num_classes_ >= MAX_NUM_SIZE_CLASSES) return;
        
        SlabConfigInfo& info = slab_class_infos_[num_classes_];
        info.block_size = block_size;
        
        uint16_t pages = base_slab_pages;

        size_t min_pages = (block_size * 8 + PAGE_SIZE -1) / PAGE_SIZE;
        pages = std::max(pages, static_cast<uint16_t>(min_pages));

        const uint16_t max_allowed_pages = (SEGMENT_SIZE / PAGE_SIZE) / 2;
        pages = std::min(pages, max_allowed_pages);
        
        info.slab_pages = pages;
        num_classes_++;
    };
    
    // 我们的混合增长策略 (88 个类别)
    for (size_t block_size = 8; block_size <= MAX_SMALL_OBJECT_SIZE; ) {
        
        uint16_t suggested_pages;

        if (block_size <= 1024) {
            suggested_pages = 16;
        } 
        else if (block_size <= 64 * 1024) {
            suggested_pages = (block_size * 8 + PAGE_SIZE - 1) / PAGE_SIZE;
        }
        else {
            suggested_pages = (block_size * 2 + PAGE_SIZE - 1) / PAGE_SIZE;
        }
        
        add_class(block_size, suggested_pages);

        if (block_size < 128) {
            block_size += 8;
        } else if (block_size < 256) {
            block_size += 16;
        } else if (block_size < 512) {
            block_size += 32;
        } else if (block_size < 1024) {
            block_size += 64;
        } else if (block_size < 4096) {
            block_size += 256;
        } else if (block_size < 16384) {
            block_size += 1024;
        } else if (block_size < 65536) {
            block_size += 4096;
        } else {
            block_size += 16384;
        }
    }

    assert(num_classes_ < MAX_NUM_SIZE_CLASSES && "Exceeded max size classes");
}


void SlabConfig::calculate_derived_parameters() {
    for (size_t i = 0; i < num_classes_; ++i) {
        SlabConfigInfo& info = slab_class_infos_[i];
        
        const size_t slab_total_size = info.slab_pages * PAGE_SIZE;
        const size_t header_base_size = offsetof(SmallSlabHeader, bitmap);
        
        // 这是一个迭代计算，以找到最佳的 capacity。
        // 我们尝试从最大可能的容量开始递减，找到第一个满足空间要求的容量。
        size_t best_capacity = 0;
        for (size_t cap = slab_total_size / info.block_size; cap > 0; --cap) {
            size_t bitmap_uint64_count = (cap + 63) / 64;
            size_t metadata_size = header_base_size + bitmap_uint64_count * 8;
            
            // 向上对齐元数据大小，以确保第一个块是对齐的
            // 这对性能很重要，尤其是当 block_size 是 8, 16 等对齐值时
            metadata_size = (metadata_size + 7) & ~7; // Align to 8 bytes

            if (metadata_size + cap * info.block_size <= slab_total_size) {
                best_capacity = cap;
                info.slab_metadata_size = metadata_size;
                break;
            }
        }
        info.slab_capacity = best_capacity;
        assert(info.slab_capacity > 0 && "Calculated capacity is zero, check logic.");
    }
}


void SlabConfig::build_lookup_table() {
    size_t current_class = 0;
    for (size_t size = 1; size <= MAX_SMALL_OBJECT_SIZE; ++size) {
        // 如果当前 size 超过了当前 class 的 block_size，就移动到下一个 class
        if (size > slab_class_infos_[current_class].block_size) {
            current_class++;
        }
        size_to_class_map_[size] = static_cast<uint8_t>(current_class);
    }
    // 特殊处理 size = 0 的情况
    size_to_class_map_[0] = 0;
}

} // namespace my_malloc