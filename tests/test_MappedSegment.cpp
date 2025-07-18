#include <gtest/gtest.h>

// 包含被测试的头文件
#include <my_malloc/internal/MappedSegment.hpp>

using my_malloc::ThreadHeap;

// 使用我们定义的核心常量和类型
using namespace my_malloc::internal;

// =====================================================================================
// 注意：此测试文件需要链接 MappedSegment.cpp 中提供的实现才能成功编译运行。
// =====================================================================================


// --- 测试套件 1: 生命周期 (Lifecycle) ---
// 这个套件不使用测试固件，用于测试最基本的创建和销毁边缘情况。

TEST(MappedSegmentLifecycle, CreationReturnsValidPointer) {
    MappedSegment* segment = MappedSegment::create();
    // 最基本的测试：create() 应该返回一个非空指针。
    // 如果这个测试失败，说明 mmap 调用可能失败了。
    ASSERT_NE(segment, nullptr) << "MappedSegment::create() should return a valid pointer.";
    
    // 清理资源
    MappedSegment::destroy(segment);
}

TEST(MappedSegmentLifecycle, DestroyNullDoesNotCrash) {
    // 验证销毁一个空指针不会导致程序崩溃。
    // 这是一个重要的健壮性测试。
    EXPECT_NO_THROW(MappedSegment::destroy(nullptr));
}


// --- 测试套件 2: 功能性测试 (Functionality) ---
// 使用测试固件 (Test Fixture) 来管理 MappedSegment 的生命周期，
// 以便在多个测试用例中复用一个已经创建好的、有效的 segment_ 对象。

class MappedSegmentTest : public ::testing::Test {
protected:
    // 在每个测试开始前，GTest 会调用 SetUp()
    void SetUp() override {
        // 创建一个新的 MappedSegment 实例供测试使用
        segment_ = MappedSegment::create();
        // 确保创建成功，否则后续测试没有意义
        ASSERT_NE(segment_, nullptr) << "Test setup failed: Could not create MappedSegment.";
    }

    // 在每个测试结束后，GTest 会调用 TearDown()
    void TearDown() override {
        // 清理在 SetUp() 中创建的资源，防止内存泄漏
        if (segment_) {
            MappedSegment::destroy(segment_);
        }
    }

    // 指向为每个测试准备的 MappedSegment 实例的指针
    MappedSegment* segment_ = nullptr;
};

// 测试用例 1: 验证 Fixture 本身能正常工作
TEST_F(MappedSegmentTest, FixtureSetupAndTeardown) {
    // 如果这个测试能够无错运行，就证明了 SetUp 和 TearDown 流程是正确的。
    // segment_ 指针应该在 SetUp 中被成功初始化。
    ASSERT_NE(segment_, nullptr);
    SUCCEED();
}

// 测试用例 2: 验证内存对齐
// 这是 MappedSegment::from_ptr() 能工作的关键前提
TEST_F(MappedSegmentTest, Alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(segment_);
    // 验证 segment_ 的地址是否是 SEGMENT_SIZE 的整数倍
    EXPECT_EQ(addr % SEGMENT_SIZE, 0)
        << "Segment address " << std::hex << addr
        << " is not aligned to SEGMENT_SIZE (" << SEGMENT_SIZE << ")";
}


// 测试用例 3: 验证核心的 from_ptr() 查找功能
TEST_F(MappedSegmentTest, FromPtrLookup) {
    // 指针指向 Segment 的起始位置
    void* ptr_start = segment_;
    EXPECT_EQ(MappedSegment::from_ptr(ptr_start), segment_);

    // 指针指向 Segment 的中间某处
    char* ptr_middle_raw = reinterpret_cast<char*>(segment_) + (SEGMENT_SIZE / 2);
    void* ptr_middle = static_cast<void*>(ptr_middle_raw);
    EXPECT_EQ(MappedSegment::from_ptr(ptr_middle), segment_);

    // 指针指向 Segment 的最后一个字节
    char* ptr_end_raw = reinterpret_cast<char*>(segment_) + SEGMENT_SIZE - 1;
    void* ptr_end = static_cast<void*>(ptr_end_raw);
    EXPECT_EQ(MappedSegment::from_ptr(ptr_end), segment_);
}

// 测试用例 4: 验证所有者 ThreadHeap 的设置和获取
TEST_F(MappedSegmentTest, OwnerHeapManagement) {
    // 假设 MappedSegment 的构造函数会将 owner_heap_ 初始化为 nullptr
    EXPECT_EQ(segment_->get_owner_heap(), nullptr) << "Newly created segment should have no owner.";

    // 创建一个假的 ThreadHeap 地址用于测试
    ThreadHeap* dummy_heap = reinterpret_cast<ThreadHeap*>(0xDEADBEEF);
    segment_->set_owner_heap(dummy_heap);

    EXPECT_EQ(segment_->get_owner_heap(), dummy_heap);
}

// 测试用例 5: 验证 PageDescriptor 的查找和修改功能
TEST_F(MappedSegmentTest, PageDescriptorLookupAndModification) {
    uintptr_t base_addr = reinterpret_cast<uintptr_t>(segment_);

    // 选择一个非边界的 page (例如第10个) 来进行测试
    const size_t test_page_index = 100;
    void* ptr_in_page = reinterpret_cast<void*>(base_addr + test_page_index * PAGE_SIZE + 123);

    // 1. 查找并验证初始状态
    PageDescriptor* desc = segment_->page_descriptor_from_ptr(ptr_in_page);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->status, PageStatus::FREE) << "Page should initially be FREE.";
    EXPECT_EQ(desc->slab_ptr, nullptr);

    // 2. 修改该 descriptor 的内容
    desc->status = PageStatus::SLAB_START;
    AllocSlab* dummy_slab = reinterpret_cast<AllocSlab*>(0xCAFEF00D);
    desc->slab_ptr = dummy_slab;

    // 3. 再次通过同一页内的不同地址查找，验证修改是否生效
    void* another_ptr_in_same_page = reinterpret_cast<void*>(base_addr + test_page_index * PAGE_SIZE);
    PageDescriptor* desc_again = segment_->page_descriptor_from_ptr(another_ptr_in_same_page);

    EXPECT_EQ(desc_again, desc) << "Pointers within the same page should return the same descriptor.";
    EXPECT_EQ(desc_again->status, PageStatus::SLAB_START);
    EXPECT_EQ(desc_again->slab_ptr, dummy_slab);

    // 4. 检查相邻的 page 是否未受影响，确保修改没有越界
    void* ptr_in_next_page = reinterpret_cast<void*>(base_addr + (test_page_index + 1) * PAGE_SIZE);
    PageDescriptor* desc_next = segment_->page_descriptor_from_ptr(ptr_in_next_page);
    ASSERT_NE(desc_next, nullptr);
    EXPECT_NE(desc_next, desc);
    EXPECT_EQ(desc_next->status, PageStatus::FREE) << "Adjacent page should not be affected.";
}

// --- 新增的、更严格的 Alignment 循环测试 ---
TEST_F(MappedSegmentTest, AlignmentIsConsistentlyCorrect) {
    // 定义一个合理的迭代次数，例如 100 次，以确保结果不是偶然的。
    const int num_iterations = 100;

    for (int i = 0; i < num_iterations; ++i) {
        // SCOPED_TRACE 是一个非常有用的 gtest 工具。
        // 如果这个作用域内的任何 EXPECT/ASSERT 失败，它会打印出当前是第几次迭代，
        // 极大地帮助我们定位问题。
        SCOPED_TRACE("Iteration " + std::to_string(i));

        // 1. 在循环内部创建 Segment
        MappedSegment* segment = MappedSegment::create();

        // 使用 ASSERT，因为如果创建失败，后续测试没有意义，应立即停止。
        ASSERT_NE(segment, nullptr) << "Failed to create segment on this iteration.";

        // 2. 对新创建的 Segment 进行对齐检查
        uintptr_t addr = reinterpret_cast<uintptr_t>(segment);
        
        // 使用 EXPECT，如果某一次对齐失败，测试会记录失败但会继续循环，
        // 这有助于我们发现问题是否是偶发的。
        EXPECT_EQ(addr % SEGMENT_SIZE, 0)
            << "Segment address " << std::hex << addr
            << " is not aligned to SEGMENT_SIZE (" << SEGMENT_SIZE << ")";

        // 3. 立即销毁，为下一次迭代释放资源
        MappedSegment::destroy(segment);
    }
}


// --- 新增测试: 元数据初始化 (Metadata Initialization) ---
// 这些测试验证 MappedSegment 构造函数是否正确地标记了元数据页，
// 以及相关的辅助函数是否能正确读取这些信息。

// 测试用例 6: 验证构造函数是否正确地初始化了元数据页描述符 (增强版)
TEST_F(MappedSegmentTest, ConstructorInitializesMetadataPagesCorrectly) {
    // 1. 计算理论上元数据应该占据的页面数
    const size_t metadata_size = sizeof(MappedSegment);
    const size_t num_metadata_pages = (metadata_size + PAGE_SIZE - 1) / PAGE_SIZE;
    const size_t total_pages = SEGMENT_SIZE / PAGE_SIZE;
    
    // 确保这个计算有意义
    ASSERT_GT(num_metadata_pages, 0);
    ASSERT_LT(num_metadata_pages, total_pages) << "Metadata should not consume the entire segment.";

    // 2. 验证第一个元数据页 (METADATA_START)
    SCOPED_TRACE("Verifying the first metadata page (index 0)");
    const PageDescriptor* desc_first = segment_->page_descriptor_from_ptr(segment_);
    
    EXPECT_EQ(desc_first->status, PageStatus::METADATA_START);
    
    size_t stored_metadata_size = reinterpret_cast<size_t>(desc_first->slab_ptr);
    EXPECT_EQ(stored_metadata_size, metadata_size);

    // 3. 验证后续的元数据页 (METADATA_SUBPAGE)
    for (size_t i = 1; i < num_metadata_pages; ++i) {
        SCOPED_TRACE("Verifying metadata sub-page at index " + std::to_string(i));
        void* ptr_in_page = reinterpret_cast<char*>(segment_) + i * PAGE_SIZE;
        const PageDescriptor* desc_sub = segment_->page_descriptor_from_ptr(ptr_in_page);
        
        EXPECT_EQ(desc_sub->status, PageStatus::METADATA_SUBPAGE);
    }

    // --- 增强部分 ---
    // 4. 验证元数据区域之后的所有页面都为 FREE 状态
    // 这是一个更强的检查，确保构造函数中对空闲页的初始化是完全且正确的。
    for (size_t i = num_metadata_pages; i < total_pages; ++i) {
        // 使用 SCOPED_TRACE 可以在失败时准确地指出是哪一页出了问题
        SCOPED_TRACE("Verifying free page at index " + std::to_string(i));
        
        void* ptr_in_free_page = reinterpret_cast<char*>(segment_) + i * PAGE_SIZE;
        const PageDescriptor* desc_free = segment_->page_descriptor_from_ptr(ptr_in_free_page);
        
        // 验证状态和指针都符合期望的默认值
        EXPECT_EQ(desc_free->status, PageStatus::FREE);
        EXPECT_EQ(desc_free->slab_ptr, nullptr);
    }
}

// 测试用例 7: 验证 get_metadata_size_from_descriptor() 的功能
TEST_F(MappedSegmentTest, GetMetadataSizeFromDescriptor) {
    // 1. 正常情况：刚创建的 segment 应该能返回正确的元数据大小
    size_t expected_size = sizeof(MappedSegment);
    EXPECT_EQ(segment_->get_metadata_size_from_descriptor(), expected_size);

    // 2. 异常情况：如果我们手动修改第一个页的状态，函数应该返回 0
    //    这是为了测试函数内部的 if 条件。
    PageDescriptor* desc_first = segment_->page_descriptor_from_ptr(segment_);
    
    // 保存原始状态以便恢复
    PageStatus original_status = desc_first->status;
    
    // 修改状态为非 METADATA_START
    desc_first->status = PageStatus::FREE;
    
    // 现在函数应该返回 0
    EXPECT_EQ(segment_->get_metadata_size_from_descriptor(), 0) 
        << "Should return 0 when the first page status is not METADATA_START";
        
    // 恢复原始状态，以免影响其他测试（虽然 TearDown 会处理，但这是好习惯）
    desc_first->status = original_status;
}