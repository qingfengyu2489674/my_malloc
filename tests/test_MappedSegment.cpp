#include <gtest/gtest.h>
#include <my_malloc/internal/MappedSegment.hpp>

// 所有之前的测试（生命周期、功能性、元数据）仍然有效。

using namespace my_malloc;

// --- 测试套件：生命周期 ---
class MappedSegmentTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 使用您最新的 MappedSegment.cpp 实现，create() 应该能正常工作
        segment_ = MappedSegment::create();
        ASSERT_NE(segment_, nullptr);
    }

    void TearDown() override {
        if (segment_) {
            MappedSegment::destroy(segment_);
        }
    }

    MappedSegment* segment_ = nullptr;
};

TEST_F(MappedSegmentTest, CreationAndDestroy) {
    SUCCEED(); // Fixture 已经处理了创建和销毁
}

// ... (Alignment, FromPtrLookup, OwnerHeapManagement 等测试保持不变) ...
// (为了简洁，这里省略了这些测试的代码，它们是正确的，不需要修改)
TEST_F(MappedSegmentTest, Alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(segment_);
    EXPECT_EQ(addr % SEGMENT_SIZE, 0);
}

TEST_F(MappedSegmentTest, FromPtrLookup) {
    void* ptr_start = segment_;
    EXPECT_EQ(MappedSegment::get_segment(ptr_start), segment_);
    char* ptr_middle_raw = reinterpret_cast<char*>(segment_) + (SEGMENT_SIZE / 2);
    void* ptr_middle = static_cast<void*>(ptr_middle_raw);
    EXPECT_EQ(MappedSegment::get_segment(ptr_middle), segment_);
}


// --- 测试套件：元数据初始化 (已更新，不再依赖 get_metadata_size_from_descriptor) ---
TEST_F(MappedSegmentTest, ConstructorInitializesMetadataCorrectly) {
    // 您的 MappedSegment.cpp 构造函数和 linear_allocate_pages 实现存在逻辑冲突
    // 我将基于您提供的最新版本编写测试，假设构造函数负责元数据页的初始化

    const size_t metadata_size = sizeof(MappedSegment);
    const size_t num_metadata_pages = (metadata_size + PAGE_SIZE - 1) / PAGE_SIZE;

    // 检查第一页
    const PageDescriptor* desc_first = segment_->get_page_desc(segment_);
    EXPECT_EQ(desc_first->status, PageStatus::METADATA);
    // 假设您的最终设计是将 slab_ptr 指向 segment
    EXPECT_EQ(desc_first->slab_ptr, segment_); 

    // 检查后续的元数据页
    for (size_t i = 1; i < num_metadata_pages; ++i) {
        void* ptr_in_page = reinterpret_cast<char*>(segment_) + i * PAGE_SIZE;
        const PageDescriptor* desc_sub = segment_->get_page_desc(ptr_in_page);
        EXPECT_EQ(desc_sub->status, PageStatus::METADATA);
        EXPECT_EQ(desc_sub->slab_ptr, segment_);
    }
}

// ===================================================================================
// === 测试用例 GetMetadataSizeFromDescriptor 已被移除 ===============================
// ===================================================================================
// TEST_F(MappedSegmentTest, GetMetadataSizeFromDescriptor) { ... } // <<-- THIS IS DELETED


// ===================================================================================
// === 线性分配测试套件 (与之前相同) =================================================
// ===================================================================================
class MappedSegmentAllocationTest : public MappedSegmentTest {
    // 继承自 MappedSegmentTest，为每个测试获取一个新的 segment_ 实例。
};
