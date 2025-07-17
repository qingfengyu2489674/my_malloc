#include <my_malloc/internal/AllocSlab.hpp>
#include <my_malloc/internal/SlabConfig.hpp>
#include <cassert>
#include <cstring> // For memset

#if defined(__GNUC__) || defined(__clang__)
#include <strings.h> // For ffsll on some systems
#endif

namespace my_malloc {
namespace internal {

void SmallSlabHeader::init(uint16_t slab_class_id) {
    // 1. 记录自己的“身份”
    this->slab_class_id = slab_class_id;

    // 2. 查询全局配置，获取所有规格信息
    const SlabConfig& config = SlabConfig::get_instance();
    const SlabConfigInfo& info = config.get_info(slab_class_id);

    // 3. 根据规格，初始化自身状态
    this->free_count = info.slab_capacity;

    // 4. 初始化位图 (bitmap)，将所有位设为 1，表示所有块都空闲
    size_t bitmap_uint64_count = (info.slab_capacity + 63) / 64;
    memset(this->bitmap, 0xFF, bitmap_uint64_count * sizeof(uint64_t));

    // 5. (关键) 清除最后一个 uint64_t 中多余的、无效的位
    size_t remainder = info.slab_capacity % 64;
    if (remainder > 0) {
        // 创建一个掩码，只有低 `remainder` 位是 1
        uint64_t mask = (1ULL << remainder) - 1;
        this->bitmap[bitmap_uint64_count - 1] &= mask;
    }

    // 6. 初始化链表指针，表示暂不属于任何链表
    this->prev = nullptr;
    this->next = nullptr;
}

void* SmallSlabHeader::allocate_block() {
    if (is_full()) {
        return nullptr;
    }

    const SlabConfig& config = SlabConfig::get_instance();
    const SlabConfigInfo& info = config.get_info(this->slab_class_id);

    size_t bitmap_uint64_count = (info.slab_capacity + 63) / 64;
    size_t block_index = 0;

    // 遍历位图，查找第一个为 1 (空闲) 的位
    for (size_t i = 0; i < bitmap_uint64_count; ++i) {
        if (this->bitmap[i] == 0) {
            continue; // 这个 uint64_t 中没有空闲块，跳过
        }

        // --- 使用 CPU 内建指令高效查找第一个为 1 的位 ---
#if defined(__GNUC__) || defined(__clang__)
        // `ffsll` (find first set long long) 返回 1-based 的索引
        int first_set_bit = ffsll(this->bitmap[i]);
        assert(first_set_bit > 0);
        size_t bit_index = first_set_bit - 1; // 转换为 0-based
#else
        // 为其他编译器（如 MSVC）提供一个可移植但较慢的回退方案
        size_t bit_index = 0;
        uint64_t word = this->bitmap[i];
        while (((word >> bit_index) & 1) == 0) {
            bit_index++;
        }
#endif

        block_index = i * 64 + bit_index;
        
        // 确保找到的索引在容量范围内
        if (block_index >= info.slab_capacity) {
            continue;
        }

        // 标记该位为 0 (已使用)
        this->bitmap[i] &= ~(1ULL << bit_index);
        this->free_count--;

        // 计算并返回用户块的指针
        char* start_of_blocks = reinterpret_cast<char*>(this) + info.slab_metadata_size;
        return start_of_blocks + block_index * info.block_size;
    }

    // 理论上不应该执行到这里，因为 is_full() 已经检查过了
    assert(false && "Slab is not full, but no free block was found.");
    return nullptr;
}

void SmallSlabHeader::free_block(void* ptr) {
    const SlabConfig& config = SlabConfig::get_instance();
    const SlabConfigInfo& info = config.get_info(this->slab_class_id);

    // 1. 计算 ptr 相对于数据区的偏移，反推出 block_index
    char* start_of_blocks = reinterpret_cast<char*>(this) + info.slab_metadata_size;
    ptrdiff_t offset = static_cast<char*>(ptr) - start_of_blocks;

    // 安全检查
    assert(offset >= 0 && "Pointer is before the start of the slab's data area.");
    assert(offset % info.block_size == 0 && "Pointer is not aligned to a block boundary.");

    size_t block_index = offset / info.block_size;
    assert(block_index < info.slab_capacity && "Pointer maps to an out-of-bounds block index.");

    // 2. 计算在位图中的位置
    size_t word_index = block_index / 64;
    size_t bit_index = block_index % 64;

    // 3. 安全检查 (防范 double-free)
    assert(((this->bitmap[word_index] >> bit_index) & 1) == 0 && "Attempting to double-free a block.");

    // 4. 标记该位为 1 (空闲)
    this->bitmap[word_index] |= (1ULL << bit_index);
    this->free_count++;
}

bool SmallSlabHeader::is_empty() const {
    const SlabConfig& config = SlabConfig::get_instance();
    const SlabConfigInfo& info = config.get_info(this->slab_class_id);

    return this->free_count == info.slab_capacity;
}

} // namespace internal
} // namespace my_malloc