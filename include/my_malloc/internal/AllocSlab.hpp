#ifndef MY_MALLOC_ALLOC_INTERNALS_SLAB_HPP
#define MY_MALLOC_ALLOC_INTERNALS_SLAB_HPP

#include <cstdint>
#include <my_malloc/internal/definitions.hpp>

namespace my_malloc {
namespace internal {

struct FreeSlabNode {
    // FreeSlabNode* prev = nullptr; // 由于单项链表
    FreeSlabNode* next_ = nullptr;
    uint16_t num_pages_ = 0;
    uint16_t reserved_ = 0; 
};
static_assert(sizeof(FreeSlabNode) <= 32, "FreeSlabNode is too large!");


class SmallSlabHeader {
public:
    SmallSlabHeader* prev_ = nullptr;
    SmallSlabHeader* next_ = nullptr;
    
    uint16_t free_count_ = 0;
    uint16_t slab_class_id_ = 0;

    uint64_t bitmap[1];

    SmallSlabHeader() 
        : prev_(this), next_(this), free_count_(0), slab_class_id_(UINT16_MAX) 
    {}

    explicit SmallSlabHeader(uint16_t slab_class_id);

    SmallSlabHeader(const SmallSlabHeader&) = delete;
    SmallSlabHeader& operator=(const SmallSlabHeader&) = delete;

    void* allocate_block();

    void free_block(void* ptr);

    bool is_full() const { return free_count_ == 0; }

    bool is_empty() const;
};

} // namespace internal
} // namespace my_malloc

#endif // MY_MALLOC_ALLOC_INTERNALS_SLAB_HPP