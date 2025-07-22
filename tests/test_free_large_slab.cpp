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
// 测试用例 1: 验证释放单页大对象
// ===================================================================================
TEST_F(FreeTest, FreeSinglePageLargeObjectResetsStatus) {
    void* ptr = heap_->allocate(MAX_SMALL_OBJECT_SIZE + 1);
    ASSERT_NE(ptr, nullptr);

    MappedSegment* seg = MappedSegment::get_segment(ptr);
    PageDescriptor* desc_before = seg->get_page_desc(ptr);
    ASSERT_EQ(desc_before->status, PageStatus::LARGE_SLAB);

    heap_->free(ptr);

    PageDescriptor* desc_after = seg->get_page_desc(ptr);
    EXPECT_EQ(desc_after->status, PageStatus::FREE);
}

// ===================================================================================
// 测试用例 2: 验证释放多页大对象后，所有相关 PageDescriptor 的状态都被正确重置
// ===================================================================================
TEST_F(FreeTest, FreeMultiPageLargeObjectResetsAllStatuses) {
    const size_t num_pages = 4;
    const size_t large_size = MAX_SMALL_OBJECT_SIZE + num_pages * PAGE_SIZE;
    void* ptr = heap_->allocate(large_size);
    ASSERT_NE(ptr, nullptr);

    heap_->free(ptr);

    // 验证: 遍历这 4 页，检查每一页的 PageDescriptor 状态
    MappedSegment* seg = MappedSegment::get_segment(ptr);
    for (size_t i = 0; i < num_pages; ++i) {
        char* current_page_ptr = static_cast<char*>(ptr) + i * PAGE_SIZE;
        PageDescriptor* desc = seg->get_page_desc(current_page_ptr);
        
        // 使用 SCOPED_TRACE 可以在测试失败时打印出循环变量 i 的值，方便调试
        SCOPED_TRACE("Checking page index " + std::to_string(i));
        EXPECT_EQ(desc->status, PageStatus::FREE);
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
// 测试用例 4: 尝试释放一个无效指针（slab 的中间部分）
// ===================================================================================
TEST_F(FreeTest, FreeInvalidPointerDoesNothing) {
    void* ptr = heap_->allocate(MAX_SMALL_OBJECT_SIZE + 2 * PAGE_SIZE);
    ASSERT_NE(ptr, nullptr);

    void* invalid_ptr = static_cast<char*>(ptr) + PAGE_SIZE;
    
    MappedSegment* seg = MappedSegment::get_segment(ptr);
    PageDescriptor* desc_before = seg->get_page_desc(invalid_ptr);
    ASSERT_EQ(desc_before->status, PageStatus::LARGE_SLAB);

    heap_->free(invalid_ptr);

    PageDescriptor* desc_after = seg->get_page_desc(invalid_ptr);
    EXPECT_EQ(desc_after->status, PageStatus::FREE);
}

} // namespace my_malloc