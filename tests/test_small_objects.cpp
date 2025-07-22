#include <gtest/gtest.h>
#include <my_malloc/ThreadHeap.hpp>
#include <my_malloc/internal/SlabConfig.hpp> // 需要包含以获取 Slab 容量
#include <vector>

// 使用命名空间在测试文件中是安全的
using namespace my_malloc;
using namespace my_malloc::internal;

/**
 * @brief Small Object 分配与释放的测试固件 (Test Fixture)
 *
 * 在每个测试用例开始前，都会创建一个全新的 ThreadHeap 实例，
 * 并在结束后销毁它，确保测试之间的隔离性。
 */
class SmallObjectTest : public ::testing::Test {
protected:
    void SetUp() override {
        heap = new ThreadHeap();
    }

    void TearDown() override {
        delete heap;
    }

    ThreadHeap* heap;
};


// -----------------------------------------------------------------------------
// 测试用例
// -----------------------------------------------------------------------------

/**
 * @test 首次分配小对象
 * @brief 验证当请求一个小尺寸对象时，系统能否正确创建第一个 Slab 并从中分配。
 */
TEST_F(SmallObjectTest, AllocateFirstSmallObject) {
    // 1. 分配一个 32 字节的小对象
    void* ptr = heap->allocate(32);
    ASSERT_NE(ptr, nullptr) << "首次分配不应失败";

    // 2. 验证 PageDescriptor 的状态
    MappedSegment* segment = MappedSegment::from_ptr(ptr);
    PageDescriptor* desc = segment->page_descriptor_from_ptr(ptr);
    
    // 页面状态应为 SMALL_SLAB 或 SMALL_SLAB
    ASSERT_TRUE(desc->status == PageStatus::SMALL_SLAB || desc->status == PageStatus::SMALL_SLAB)
        << "页面状态未被正确设置为 Small Slab";

    // 3. 验证 Slab Header
    ASSERT_NE(desc->slab_ptr, nullptr) << "PageDescriptor 未能指向 Slab Header";
    auto* slab = reinterpret_cast<SmallSlabHeader*>(desc->slab_ptr);
    
    // 验证 Slab 的 class_id 是否正确
    const auto& config = SlabConfig::get_instance();
    size_t expected_class_id = config.get_size_class_index(32);
    EXPECT_EQ(slab->slab_class_id_, expected_class_id) << "Slab 的尺寸类别 ID 不正确";

    // 验证 free_count 是否被正确更新
    const auto& info = config.get_info(expected_class_id);
    EXPECT_EQ(slab->free_count_, info.slab_capacity - 1) << "分配后，Slab 的 free_count 未减少";
}

/**
 * @test 快车道分配
 * @brief 验证当一个 Slab 未满时，后续相同尺寸的分配请求会复用同一个 Slab。
 */
TEST_F(SmallObjectTest, AllocateOnFastPathReusesSlab) {
    // 1. 连续分配两个相同尺寸的小对象
    void* ptr1 = heap->allocate(64);
    void* ptr2 = heap->allocate(64);
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr1, ptr2) << "连续分配应返回不同地址";

    // 2. 验证它们是否来自同一个 Slab
    PageDescriptor* desc1 = MappedSegment::from_ptr(ptr1)->page_descriptor_from_ptr(ptr1);
    PageDescriptor* desc2 = MappedSegment::from_ptr(ptr2)->page_descriptor_from_ptr(ptr2);

    EXPECT_EQ(desc1->slab_ptr, desc2->slab_ptr) << "快车道分配未能复用同一个 Slab";
}

/**
 * @test Slab 变满
 * @brief 验证当一个 Slab 的所有块被分配完后，它会从可用链表中移除，
 *        且后续请求会触发新 Slab 的创建。
 */
TEST_F(SmallObjectTest, SlabIsRemovedWhenFullAndNewOneIsCreated) {
    const size_t alloc_size = 16;
    const auto& config = SlabConfig::get_instance();
    size_t class_id = config.get_size_class_index(alloc_size);
    const auto& info = config.get_info(class_id);
    size_t capacity = info.slab_capacity;
    
    // 确保这个尺寸的 Slab 容量大于 1，否则测试无意义
    ASSERT_GT(capacity, 1);

    // 1. 分配满一个 Slab
    std::vector<void*> pointers;
    for (size_t i = 0; i < capacity; ++i) {
        pointers.push_back(heap->allocate(alloc_size));
    }

    // 2. 验证 Slab 状态
    void* first_ptr = pointers.front();
    PageDescriptor* desc = MappedSegment::from_ptr(first_ptr)->page_descriptor_from_ptr(first_ptr);
    auto* slab = reinterpret_cast<SmallSlabHeader*>(desc->slab_ptr);
    ASSERT_TRUE(slab->is_full()) << "Slab 分配满后，is_full() 应返回 true";

    // 3. 分配第 (capacity + 1) 个对象，这必须触发新 Slab 的创建
    void* ptr_new_slab = heap->allocate(alloc_size);
    ASSERT_NE(ptr_new_slab, nullptr);

    // 4. 验证新对象来自一个全新的 Slab
    PageDescriptor* desc_new = MappedSegment::from_ptr(ptr_new_slab)->page_descriptor_from_ptr(ptr_new_slab);
    EXPECT_NE(desc_new->slab_ptr, desc->slab_ptr) << "Slab 变满后，新的分配请求未能创建新的 Slab";

    // 清理
    for (void* p : pointers) {
        heap->free(p);
    }
    heap->free(ptr_new_slab);
}

/**
 * @test 从满的 Slab 中释放，使其重新可用
 * @brief 验证当一个已满的 Slab 被释放一个块后，它会重新回到可用链表中。
 */
TEST_F(SmallObjectTest, FreeingFromFullSlabMakesItAvailableAgain) {
    const size_t alloc_size = 128;
    const auto& config = SlabConfig::get_instance();
    size_t class_id = config.get_size_class_index(alloc_size);
    const auto& info = config.get_info(class_id);
    size_t capacity = info.slab_capacity;
    ASSERT_GT(capacity, 1);

    // 1. 分配满一个 Slab
    std::vector<void*> pointers;
    for (size_t i = 0; i < capacity; ++i) {
        pointers.push_back(heap->allocate(alloc_size));
    }
    
    // 2. 获取原始 Slab 的指针
    void* original_slab_ptr = MappedSegment::from_ptr(pointers.front())->page_descriptor_from_ptr(pointers.front())->slab_ptr;

    // 3. 释放其中一个块，使 Slab 从 "Full" -> "Partial"
    heap->free(pointers.back());
    pointers.pop_back();

    // 4. 再次请求分配
    void* ptr_reused = heap->allocate(alloc_size);
    ASSERT_NE(ptr_reused, nullptr);
    
    // 5. 验证这次分配复用了之前的 Slab，而不是创建新 Slab
    void* reused_slab_ptr = MappedSegment::from_ptr(ptr_reused)->page_descriptor_from_ptr(ptr_reused)->slab_ptr;
    EXPECT_EQ(reused_slab_ptr, original_slab_ptr) << "从已满 Slab 释放后，未能复用该 Slab";
    
    // 清理
    for (void* p : pointers) {
        heap->free(p);
    }
    heap->free(ptr_reused);
}

/**
 * @test 释放最后一个对象，回收 Slab 内存
 * @brief 验证当一个 Slab 的最后一个对象被释放时，Slab 自身会被回收，
 *        其占用的页面状态会变回 FREE。
 */
TEST_F(SmallObjectTest, FreeingLastObjectRecyclesSlabMemory) {
    // 1. 分配一个对象，这将创建一个新 Slab
    const size_t alloc_size = 256;
    void* ptr = heap->allocate(alloc_size);
    ASSERT_NE(ptr, nullptr);

    // 2. 记录 Slab 的信息
    MappedSegment* segment = MappedSegment::from_ptr(ptr);
    PageDescriptor* desc_before_free = segment->page_descriptor_from_ptr(ptr);
    void* slab_address = desc_before_free->slab_ptr;
    
    const auto& config = SlabConfig::get_instance();
    size_t class_id = config.get_size_class_index(alloc_size);
    const auto& info = config.get_info(class_id);
    uint16_t num_pages = info.slab_pages;

    // 3. 释放这唯一一个对象
    heap->free(ptr);

    // 4. 验证 Slab 占用的所有页面的状态都已变回 FREE
    for (uint16_t i = 0; i < num_pages; ++i) {
        char* current_page_ptr = static_cast<char*>(slab_address) + i * PAGE_SIZE;
        PageDescriptor* desc_after_free = segment->page_descriptor_from_ptr(current_page_ptr);
        
        EXPECT_EQ(desc_after_free->status, PageStatus::FREE) 
            << "释放最后一个对象后，第 " << i << " 页的状态未重置为 FREE";
    }
}

// ... (接在之前的测试用例之后) ...

/**
 * @test 混合尺寸类别分配
 * @brief 验证不同尺寸的分配请求会正确地使用不同的 Slab 缓存。
 */
TEST_F(SmallObjectTest, MixedSizeClassAllocationsAreIsolated) {
    // 1. 分配两种不同尺寸类别的对象
    void* ptr_16_byte_1 = heap->allocate(16);
    void* ptr_64_byte_1 = heap->allocate(64);
    ASSERT_NE(ptr_16_byte_1, nullptr);
    ASSERT_NE(ptr_64_byte_1, nullptr);

    // 2. 再次分配一个 16 字节的对象
    void* ptr_16_byte_2 = heap->allocate(16);
    ASSERT_NE(ptr_16_byte_2, nullptr);

    // 3. 验证 ptr_16_byte_1 和 ptr_16_byte_2 来自同一个 Slab
    auto* desc1 = MappedSegment::from_ptr(ptr_16_byte_1)->page_descriptor_from_ptr(ptr_16_byte_1);
    auto* desc3 = MappedSegment::from_ptr(ptr_16_byte_2)->page_descriptor_from_ptr(ptr_16_byte_2);
    EXPECT_EQ(desc1->slab_ptr, desc3->slab_ptr) << "相同尺寸的第二次分配未能复用 Slab";

    // 4. 验证 ptr_64_byte_1 来自一个完全不同的 Slab
    auto* desc2 = MappedSegment::from_ptr(ptr_64_byte_1)->page_descriptor_from_ptr(ptr_64_byte_1);
    EXPECT_NE(desc1->slab_ptr, desc2->slab_ptr) << "不同尺寸的分配错误地使用了同一个 Slab";
}


/**
 * @test 交错的分配与释放 (Checkerboard Pattern)
 * @brief 模拟碎片化的使用模式，验证 Slab 的位图逻辑和复用能力。
 */
TEST_F(SmallObjectTest, InterleavedAllocationAndFree) {
    const size_t alloc_size = 8;
    const auto& config = SlabConfig::get_instance();
    size_t class_id = config.get_size_class_index(alloc_size);
    const auto& info = config.get_info(class_id);
    size_t capacity = info.slab_capacity;
    ASSERT_GE(capacity, 4) << "此测试需要 Slab 容量至少为 4";

    // 1. 分配多个对象
    std::vector<void*> pointers;
    for (size_t i = 0; i < capacity; ++i) {
        pointers.push_back(heap->allocate(alloc_size));
    }
    
    auto* slab_ptr = MappedSegment::from_ptr(pointers[0])->page_descriptor_from_ptr(pointers[0])->slab_ptr;

    // 2. 交错释放 (释放所有偶数索引的指针)
    for (size_t i = 0; i < capacity; i += 2) {
        heap->free(pointers[i]);
    }
    
    // 3. 重新分配，验证是否复用了被释放的空洞
    for (size_t i = 0; i < capacity / 2; ++i) {
        void* reused_ptr = heap->allocate(alloc_size);
        auto* reused_slab_ptr = MappedSegment::from_ptr(reused_ptr)->page_descriptor_from_ptr(reused_ptr)->slab_ptr;
        // 验证新的分配仍然发生在这个部分空闲的 Slab 中
        EXPECT_EQ(reused_slab_ptr, slab_ptr) << "交错释放后，未能复用部分空闲的 Slab";
    }
}


/**
 * @test 边界尺寸分配
 * @brief 验证在 MAX_SMALL_OBJECT_SIZE 边界上的分配行为是否正确。
 */
TEST_F(SmallObjectTest, BoundarySizeAllocations) {
    // 1. 测试恰好是最大小对象的尺寸
    void* ptr_small_max = heap->allocate(MAX_SMALL_OBJECT_SIZE);
    ASSERT_NE(ptr_small_max, nullptr);
    PageDescriptor* desc_small = MappedSegment::from_ptr(ptr_small_max)->page_descriptor_from_ptr(ptr_small_max);
    EXPECT_TRUE(desc_small->status == PageStatus::SMALL_SLAB || desc_small->status == PageStatus::SMALL_SLAB)
        << "分配 MAX_SMALL_OBJECT_SIZE 时未走小对象路径";
    heap->free(ptr_small_max);

    // 2. 测试刚好超过最大小对象的尺寸
    void* ptr_large_min = heap->allocate(MAX_SMALL_OBJECT_SIZE + 1);
    ASSERT_NE(ptr_large_min, nullptr);
    PageDescriptor* desc_large = MappedSegment::from_ptr(ptr_large_min)->page_descriptor_from_ptr(ptr_large_min);
    EXPECT_EQ(desc_large->status, PageStatus::LARGE_SLAB)
        << "分配 MAX_SMALL_OBJECT_SIZE + 1 时未走大对象路径";
    heap->free(ptr_large_min);
}


/**
 * @test 重复释放的安全性
 * @brief 明确验证对同一个指针进行重复释放是安全的，不会导致崩溃。
 */
TEST_F(SmallObjectTest, DoubleFreeIsSafe) {
    void* ptr = heap->allocate(42);
    ASSERT_NE(ptr, nullptr);

    heap->free(ptr);

    // 重复释放不应抛出异常或导致程序中止。
    // EXPECT_NO_THROW 是 gtest 用于此目的的宏。
    EXPECT_NO_THROW(heap->free(ptr)); 
}