// file: tests/test_allocate.cpp

#include <gtest/gtest.h>
#include <my_malloc/ThreadHeap.hpp>
#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/definitions.hpp>

namespace my_malloc {

class AllocateTest : public ::testing::Test {
protected:
    ThreadHeap* heap_ = nullptr;

    void SetUp() override {
        heap_ = new ThreadHeap();
    }

    void TearDown() override {
        delete heap_;
    }
};

// ===================================================================================
// 测试用例 1: 分配一个刚好一页大小的大对象
// ===================================================================================
TEST_F(AllocateTest, AllocateOnePageLargeObject) {
    // 操作：请求 PAGE_SIZE 大小的内存
    void* ptr = heap_->allocate(internal::PAGE_SIZE);
    
    // 验证：
    // 1. 分配成功
    ASSERT_NE(ptr, nullptr);

    // 2. 通过返回的指针，我们可以找到它所属的 Segment 和 PageDescriptor
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc = seg->page_descriptor_from_ptr(ptr);
    
    // 3. 验证 PageDescriptor 的状态是否被正确设置
    EXPECT_EQ(desc->status, internal::PageStatus::LARGE_SLAB_START);
    EXPECT_EQ(desc->num_pages, 1); // 应该只占用 1 页
    EXPECT_EQ(desc->slab_ptr, ptr);
}

// ===================================================================================
// 测试用例 2: 分配一个跨越多页的大对象
// ===================================================================================
TEST_F(AllocateTest, AllocateMultiPageLargeObject) {
    // 操作：请求 3.5 页大小的内存，这应该会分配一个 4 页的 slab
    size_t requested_size = internal::PAGE_SIZE * 3 + 100;
    void* ptr = heap_->allocate(requested_size);
    
    // 验证：
    ASSERT_NE(ptr, nullptr);

    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* start_desc = seg->page_descriptor_from_ptr(ptr);
    
    // 验证起始页的 PageDescriptor
    EXPECT_EQ(start_desc->status, internal::PageStatus::LARGE_SLAB_START);
    EXPECT_EQ(start_desc->num_pages, 4); // 应该向上取整为 4 页
    EXPECT_EQ(start_desc->slab_ptr, ptr);

    // 验证后续页的 PageDescriptor
    for (int i = 1; i < 4; ++i) {
        char* next_page_ptr = static_cast<char*>(ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* cont_desc = seg->page_descriptor_from_ptr(next_page_ptr);
        EXPECT_EQ(cont_desc->status, internal::PageStatus::LARGE_SLAB_CONT);
        EXPECT_EQ(cont_desc->slab_ptr, ptr); // 应该指向 slab 的起始地址
    }
}

// ===================================================================================
// 测试用例 3: 分配一个非常大的对象，触发创建新 Segment
// ===================================================================================
TEST_F(AllocateTest, AllocationSpansAcrossSegments) {
    // 操作1：分配一个大对象，几乎占满第一个 Segment
    const size_t metadata_pages = (sizeof(internal::MappedSegment) + internal::PAGE_SIZE - 1) / internal::PAGE_SIZE;
    const size_t available_pages = (internal::SEGMENT_SIZE / internal::PAGE_SIZE) - metadata_pages;
    const size_t size1 = available_pages * internal::PAGE_SIZE;
    
    void* ptr1 = heap_->allocate(size1);
    ASSERT_NE(ptr1, nullptr);
    internal::MappedSegment* seg1 = internal::MappedSegment::from_ptr(ptr1);

    // 操作2：再分配一个小对象，这应该会迫使 ThreadHeap 创建一个新的 Segment
    void* ptr2 = heap_->allocate(internal::PAGE_SIZE);
    ASSERT_NE(ptr2, nullptr);
    internal::MappedSegment* seg2 = internal::MappedSegment::from_ptr(ptr2);

    // 验证：ptr1 和 ptr2 来自不同的 Segment
    EXPECT_NE(seg1, seg2);
}

// ===================================================================================
// 测试用例 4: 处理无效或边界请求
// ===================================================================================
TEST_F(AllocateTest, HandlesInvalidOrEdgeCaseSizes) {
    // 请求 0 字节，应该返回 nullptr
    EXPECT_EQ(heap_->allocate(0), nullptr);

    // 请求一个过大的尺寸，应该失败并返回 nullptr
    size_t too_large_size = internal::SEGMENT_SIZE + 1;
    EXPECT_EQ(heap_->allocate(too_large_size), nullptr);
}

} // namespace my_malloc