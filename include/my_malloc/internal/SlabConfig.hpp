#ifndef MY_MALLOC_ALLOC_INTERNALS_SLAB_CONFIG_HPP
#define MY_MALLOC_ALLOC_INTERNALS_SLAB_CONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <my_malloc/internal/definitions.hpp>

namespace my_malloc {
namespace internal {

// --- 全局常量定义 ---

// 定义 Small Object 的大小上限。超过此大小的请求将由 Large Object 逻辑处理。
// 256KB 是一个常见且合理的选择。
constexpr size_t MAX_SMALL_OBJECT_SIZE = 256 * 1024;

// 预设的尺寸类别数量上限。这需要足够大以容纳从最小到最大的所有 small object 尺寸。
// 128 个类别通常是绰绰有余的。
constexpr size_t MAX_NUM_SIZE_CLASSES = 128;


/**
 * @struct SlabConfigInfo
 * @brief 存储单个尺寸类别（Size Class）的所有预计算配置信息。
 * 
 * 这是 ThreadHeap 创建和管理 SmallSlab 的核心数据依据。
 */
struct SlabConfigInfo {
    // --- 核心配置 ---
    
    // 该尺寸类别对应的 UserBlock 的精确大小。
    size_t block_size = 0;
    
    // 管理此类 UserBlock 的 Slab 需要占用多少个连续的页面。
    uint16_t slab_pages = 0;
    
    // --- 派生/计算出的配置 ---

    // 在一个 Slab 中，能容纳的 UserBlock 的总数量。
    size_t slab_capacity = 0;
    
    // 该 Slab 的元数据 (SmallSlabHeader，包含位图) 的实际大小。
    // 这个值对于计算第一个 UserBlock 的起始偏移至关重要。
    size_t slab_metadata_size = 0;
};


/**
 * @class SlabConfig
 * @brief 尺寸类别（Size Class）系统的管理器。
 * 
 * 这是一个单例类，在程序首次使用时进行一次性初始化，
 * 预先计算出所有 small object 尺寸类别的最佳配置。
 * 它为 ThreadHeap 提供了一个只读的、高效的查询接口。
 */
class SlabConfig {
public:
    /**
     * @brief 获取 SlabConfig 的全局单例实例。
     * @return 返回 SlabConfig 的常量引用。
     */
    static const SlabConfig& get_instance();

    /**
     * @brief 根据用户请求的大小，返回对应的尺寸类别索引 (size_class_idx)。
     * 
     * @param size 用户请求的内存大小。
     * @return 如果 size 在 small object 范围内，则返回对应的索引。
     *         如果超出范围，则返回一个特殊值（如 SIZE_MAX）。
     */
    size_t get_size_class_index(size_t size) const;

    /**
     * @brief 获取指定尺寸类别的所有配置信息。
     * 
     * @param index 尺寸类别索引。
     * @return 返回包含所有预计算参数的 SlabConfigInfo 结构体的常量引用。
     */
    const SlabConfigInfo& get_info(size_t index) const;

    /**
     * @brief 获取已初始化的尺寸类别的总数。
     */
    size_t get_num_classes() const { return num_classes_; }

private:
    // 私有构造函数，在首次调用 get_instance() 时执行初始化。
    SlabConfig(); 

    void initialize_size_classes();
    void calculate_derived_parameters();
    void build_lookup_table();
    
    // 禁止拷贝和移动，以维护单例模式。
    SlabConfig(const SlabConfig&) = delete;
    SlabConfig& operator=(const SlabConfig&) = delete;

    // --- 核心数据存储 ---

    // 存储所有尺寸类别配置信息的数组。
    SlabConfigInfo slab_class_infos_[MAX_NUM_SIZE_CLASSES];
    
    // 实际初始化的尺寸类别数量。
    size_t num_classes_;

    // --- 快速查找表 ---

    // 一个从请求大小到尺寸类别索引的映射表。
    // 数组索引代表请求大小，值为对应的 size_class_idx。
    // 这提供了 O(1) 的查找速度。
    uint8_t size_to_class_map_[MAX_SMALL_OBJECT_SIZE + 1];
};

} // namespace internal
} // namespace my_malloc

#endif // MY_MALLOC_ALLOC_INTERNALS_SLAB_CONFIG_HPP