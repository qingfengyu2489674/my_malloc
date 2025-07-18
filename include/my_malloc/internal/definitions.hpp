// include/my_malloc/alloc_internals/definitions.hpp
#ifndef MY_MALLOC_ALLOC_INTERNALS_DEFINITIONS_HPP
#define MY_MALLOC_ALLOC_INTERNALS_DEFINITIONS_HPP

#include <cstddef>
#include <cstdint>

namespace my_malloc {
    class ThreadHeap;
}

namespace my_malloc {
namespace internal {

constexpr size_t PAGE_SIZE = 4 * 1024;
constexpr size_t SEGMENT_SIZE = 2 * 1024 * 1024;

enum class PageStatus : uint8_t {
    FREE,
    METADATA_START,
    METADATA_CONT,
    LARGE_SLAB_START,
    LARGE_SLAB_CONT, 
    SMALL_SLAB_START,
    SMALL_SLAB_CONT
};

class AllocSlab;

struct PageDescriptor {
    PageStatus status = PageStatus::FREE;
    uint16_t num_pages;
    AllocSlab* slab_ptr = nullptr;
};

} // namespace internal
} // namespace my_malloc

#endif // MY_MALLOC_ALLOC_INTERNALS_DEFINITIONS_HPP