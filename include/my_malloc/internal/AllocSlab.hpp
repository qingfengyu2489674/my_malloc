#ifndef MY_MALLOC_ALLOC_INTERNALS_SLAB_HPP
#define MY_MALLOC_ALLOC_INTERNALS_SLAB_HPP

#include <cstdint>
#include <my_malloc/internal/definitions.hpp>

namespace my_malloc {
namespace internal {

struct FreeSlabNode {
    FreeSlabNode* prev = nullptr;
    FreeSlabNode* next = nullptr;
    uint16_t num_pages = 0;
    uint16_t reserved = 0; 
};
static_assert(sizeof(FreeSlabNode) <= 32, "FreeSlabNode is too large!");


class SmallSlabHeader {
public:
    SmallSlabHeader* prev = nullptr;
    SmallSlabHeader* next = nullptr;
    
    uint16_t free_count = 0;
    uint16_t slab_class_id = 0;

    uint64_t bitmap[1];

    SmallSlabHeader(const SmallSlabHeader&) = delete;
    SmallSlabHeader& operator=(const SmallSlabHeader&) = delete;

    void init(uint16_t slab_class_id);

    void* allocate_block();

    void free_block(void* ptr);

    bool is_full() const { return free_count == 0; }

    bool is_empty() const;
};

} // namespace internal
} // namespace my_malloc

#endif // MY_MALLOC_ALLOC_INTERNALS_SLAB_HPP