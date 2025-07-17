#include <my_malloc/internal/SlabConfig.hpp>
#include <my_malloc/internal/AllocSlab.hpp>
#include <cassert>
#include <algorithm> // for std::min/max
#include <stddef.h> // For offsetof

namespace my_malloc {
namespace internal {

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
    // O(1) 查找
    return size_to_class_map_[size];
}

const SlabConfigInfo& SlabConfig::get_info(size_t index) const {
    assert(index < num_classes_ && "Size class index out of bounds.");
    return slab_class_infos_[index];
}

SlabConfig::SlabConfig() : num_classes_(0) {
    // --- Phase 1: 初始化尺寸类别和它们的 slab_pages ---

    size_t current_block_size = 0;

    auto add_class = [&](size_t block_size, uint16_t base_slab_pages) {
        if (num_classes_ >= MAX_NUM_SIZE_CLASSES) return;
        
        SlabConfigInfo& info = slab_class_infos_[num_classes_];
        info.block_size = block_size;
        
        // 动态决定 slab_pages，这里采用一个简单的策略
        uint16_t pages = base_slab_pages;
        // 确保 Slab 大小至少是 block_size 的8倍，且不超过 64 页 (256KB)
        size_t min_pages = (block_size * 8 + PAGE_SIZE -1) / PAGE_SIZE;
        pages = std::max(pages, static_cast<uint16_t>(min_pages));
        // pages = std::min(pages, static_cast<uint16_t>(64));
        pages = std::min(pages, static_cast<uint16_t>(128));
        info.slab_pages = pages;
        
        num_classes_++;
    };
    
    // 我们的混合增长策略 (88 个类别)
    // [1, 128B]: 步长 8B
    for (current_block_size = 8; current_block_size <= 128; current_block_size += 8) {
        add_class(current_block_size, 4); // 16KB Slabs
    }
    // (128B, 256B]: 步长 16B
    for (current_block_size = 128 + 16; current_block_size <= 256; current_block_size += 16) {
        add_class(current_block_size, 4); // 16KB Slabs
    }
    // (256B, 512B]: 步长 32B
    for (current_block_size = 256 + 32; current_block_size <= 512; current_block_size += 32) {
        add_class(current_block_size, 8); // 32KB Slabs
    }
    // (512B, 1KB]: 步长 64B
    for (current_block_size = 512 + 64; current_block_size <= 1024; current_block_size += 64) {
        add_class(current_block_size, 8); // 32KB Slabs
    }

    // (1KB, 4KB]: 步长 256B
    for (current_block_size = 1024 + 256; current_block_size <= 4096; current_block_size += 256) {
        add_class(current_block_size, 16); // 64KB Slabs
    }
    // (4KB, 16KB]: 步长 1KB
    for (current_block_size = 4096 + 1024; current_block_size <= 16384; current_block_size += 1024) {
        add_class(current_block_size, 16); // 64KB Slabs
    }
    // (16KB, 64KB]: 步长 4KB
    for (current_block_size = 16384 + 4096; current_block_size <= 65536; current_block_size += 4096) {
        add_class(current_block_size, 32); // 128KB Slabs
    }
    // (64KB, 256KB]: 步长 16KB
    for (current_block_size = 65536 + 16384; current_block_size <= 262144; current_block_size += 16384) {
        add_class(current_block_size, 64); // 256KB Slabs; 
    }
    
    assert(num_classes_ < MAX_NUM_SIZE_CLASSES && "Exceeded max size classes");

    // --- Phase 2: 计算每个类别的派生参数 (capacity, metadata_size) ---

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
    
    // --- Phase 3: 初始化 size_to_class_map_ 快速查找表 ---
    
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

} // namespace internal
} // namespace my_malloc