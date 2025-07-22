#ifndef MY_MALLOC_ALLOC_INTERNALS_SLAB_CONFIG_HPP
#define MY_MALLOC_ALLOC_INTERNALS_SLAB_CONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <my_malloc/internal/definitions.hpp>

namespace my_malloc {
    class ThreadHeap;
}

namespace my_malloc {
constexpr size_t MAX_SMALL_OBJECT_SIZE = 256 * 1024;
constexpr size_t MAX_NUM_SIZE_CLASSES = 128;



struct SlabConfigInfo {
    size_t block_size = 0;
    uint16_t slab_pages = 0;
    size_t slab_capacity = 0;
    size_t slab_metadata_size = 0;
};



class SlabConfig {
public:
    static const SlabConfig& get_instance();

    size_t get_size_class_index(size_t size) const;

    const SlabConfigInfo& get_info(size_t index) const;

    size_t get_num_classes() const { return num_classes_; }

// private:
    SlabConfig(); 

    void initialize_size_classes();
    void calculate_derived_parameters();
    void build_lookup_table();
    
    SlabConfig(const SlabConfig&) = delete;
    SlabConfig& operator=(const SlabConfig&) = delete;


    SlabConfigInfo slab_class_infos_[MAX_NUM_SIZE_CLASSES];

    size_t num_classes_;

    uint8_t size_to_class_map_[MAX_SMALL_OBJECT_SIZE + 1];
};

} // namespace my_malloc

#endif // MY_MALLOC_ALLOC_INTERNALS_SLAB_CONFIG_HPP