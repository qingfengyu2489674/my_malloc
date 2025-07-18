// file: tests/test_free.cpp

#include <gtest/gtest.h>
#include <my_malloc/ThreadHeap.hpp>
#include <my_malloc/internal/MappedSegment.hpp>
#include <my_malloc/internal/definitions.hpp>

namespace my_malloc {

class FreeTest : public ::testing::Test {
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
// 测试用例 1: 验证释放单页大对象后，PageDescriptor 的状态被正确重置
// ===================================================================================
TEST_F(FreeTest, FreeSinglePageLargeObjectResetsStatus) {
    // 操作1: 分配一个 1 页的 slab
    void* ptr = heap_->allocate(internal::PAGE_SIZE);
    ASSERT_NE(ptr, nullptr);

    // 检查释放前的状态
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc_before = seg->page_descriptor_from_ptr(ptr);
    ASSERT_EQ(desc_before->status, internal::PageStatus::LARGE_SLAB_START);

    // 操作2: 释放它
    heap_->free(ptr);

    // 验证: 释放后，PageDescriptor 的状态应该变回 FREE
    internal::PageDescriptor* desc_after = seg->page_descriptor_from_ptr(ptr);
    EXPECT_EQ(desc_after->status, internal::PageStatus::FREE);
}

// ===================================================================================
// 测试用例 2: 验证释放多页大对象后，所有相关 PageDescriptor 的状态都被正确重置
// ===================================================================================
TEST_F(FreeTest, FreeMultiPageLargeObjectResetsAllStatuses) {
    // 操作1: 分配一个 4 页的 slab
    const size_t num_pages = 4;
    void* ptr = heap_->allocate(num_pages * internal::PAGE_SIZE);
    ASSERT_NE(ptr, nullptr);

    // 操作2: 释放它
    heap_->free(ptr);

    // 验证: 遍历这 4 页，检查每一页的 PageDescriptor 状态
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    for (size_t i = 0; i < num_pages; ++i) {
        char* current_page_ptr = static_cast<char*>(ptr) + i * internal::PAGE_SIZE;
        internal::PageDescriptor* desc = seg->page_descriptor_from_ptr(current_page_ptr);
        
        // 使用 SCOPED_TRACE 可以在测试失败时打印出循环变量 i 的值，方便调试
        SCOPED_TRACE("Checking page index " + std::to_string(i));
        EXPECT_EQ(desc->status, internal::PageStatus::FREE);
    }
}

// ===================================================================================
// 测试用例 3: 验证 free(nullptr) 是安全的，不会导致程序崩溃
// ===================================================================================
TEST_F(FreeTest, FreeNullptrIsSafe) {
    // 我们的 free(nullptr) 实现是直接返回。
    // EXPECT_NO_THROW 会检查这行代码是否抛出异常 (或导致崩溃)。
    EXPECT_NO_THROW(heap_->free(nullptr));
}

// ===================================================================================
// 测试用-例 4: 尝试释放一个无效指针（slab 的中间部分），验证状态不变
// ===================================================================================
TEST_F(FreeTest, FreeInvalidPointerDoesNothing) {
    // 操作1: 分配一个 2 页的 slab
    void* ptr = heap_->allocate(2 * internal::PAGE_SIZE);
    ASSERT_NE(ptr, nullptr);

    // 构造一个指向 slab 中间（第二页起始位置）的无效指针
    void* invalid_ptr = static_cast<char*>(ptr) + internal::PAGE_SIZE;
    
    internal::MappedSegment* seg = internal::MappedSegment::from_ptr(ptr);
    internal::PageDescriptor* desc_before = seg->page_descriptor_from_ptr(invalid_ptr);
    // 释放前，第二页的状态应该是 LARGE_SLAB_CONT
    ASSERT_EQ(desc_before->status, internal::PageStatus::LARGE_SLAB_CONT);

    // 操作2: 尝试释放这个无效指针
    heap_->free(invalid_ptr);

    // 验证: 因为我们的 internal_free 会检查 desc->status，
    // 发现不是 LARGE_SLAB_START 就会直接返回。所以状态应该保持不变。
    internal::PageDescriptor* desc_after = seg->page_descriptor_from_ptr(invalid_ptr);
    EXPECT_EQ(desc_after->status, internal::PageStatus::LARGE_SLAB_CONT);
}

} // namespace my_malloc