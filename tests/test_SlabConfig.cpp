#include <gtest/gtest.h>
#include <my_malloc/internal/SlabConfig.hpp>
#include <my_malloc/internal/AllocSlab.hpp> // For offsetof
#include <stddef.h> // For offsetof
#include <climits> // For SIZE_MAX

using namespace my_malloc::internal;

// --- 测试固件 (Test Fixture) ---
// 使用固件可以方便地在多个测试用例中复用一个已经获取到的 SlabConfig 实例。
class SlabConfigTest : public ::testing::Test {
protected:
    // 在每个测试用例开始前，GTest会调用 SetUp()
    void SetUp() override {
        // 通过调用 get_instance() 来获取（并可能触发初始化）SlabConfig 单例。
        // config_ 是一个常量引用，指向全局唯一的实例。
        config_ = &SlabConfig::get_instance();
    }

    // 指向 SlabConfig 实例的指针。用指针是为了能在 SetUp 中赋值。
    const SlabConfig* config_ = nullptr;
};


// --- 测试用例集 ---

// 测试1: 单例模式的基本功能
// 验证 get_instance() 是否总是返回同一个实例。
TEST_F(SlabConfigTest, SingletonBehavesCorrectly) {
    // 再次调用 get_instance()
    const SlabConfig& another_instance = SlabConfig::get_instance();
    
    // 验证两次调用返回的实例地址是相同的。
    // 这证明了单例模式是正常工作的。
    ASSERT_EQ(config_, &another_instance) << "get_instance() should always return the same instance.";
}

// 测试2: 验证尺寸类别的基本属性
// 检查初始化后的类别数量是否在预期范围内，并且每个类别的基本属性是否合理。
TEST_F(SlabConfigTest, SizeClassesAreInitializedPlausibly) {
    size_t num_classes = config_->get_num_classes();
    
    // 我们预期有 88 个类别，但只要它在一个合理范围内即可。
    ASSERT_GT(num_classes, 0) << "Number of size classes should be greater than zero.";
    ASSERT_LE(num_classes, MAX_NUM_SIZE_CLASSES) << "Number of size classes should not exceed the maximum limit.";

    // 遍历所有已初始化的类别，进行健全性检查。
    for (size_t i = 0; i < num_classes; ++i) {
        // 使用 SCOPED_TRACE 可以在失败时打印出当前是哪个类别出了问题。
        SCOPED_TRACE("Testing size class index: " + std::to_string(i));
        
        const SlabConfigInfo& info = config_->get_info(i);
        
        // 1. block_size 应该是递增的
        if (i > 0) {
            const SlabConfigInfo& prev_info = config_->get_info(i - 1);
            EXPECT_GT(info.block_size, prev_info.block_size) << "block_size should be monotonically increasing.";
        }
        
        // 2. 所有计算出的参数都应该是正数
        EXPECT_GT(info.block_size, 0);
        EXPECT_GT(info.slab_pages, 0);
        EXPECT_GT(info.slab_capacity, 0);
        EXPECT_GT(info.slab_metadata_size, 0);

        // 3. 验证空间约束：元数据 + 数据 <= 总空间
        size_t total_space = static_cast<size_t>(info.slab_pages) * PAGE_SIZE;
        size_t used_space = info.slab_metadata_size + info.slab_capacity * info.block_size;
        EXPECT_LE(used_space, total_space) << "Total used space must not exceed the slab's total size.";

        // 4. 验证空间利用率不会太离谱 (防止计算错误)
        // 我们可以断言，再多放一个块就会超出空间，这证明了 capacity 是最优的。
        EXPECT_GT(used_space + info.block_size, total_space) 
            << "There seems to be enough space for at least one more block, capacity might be calculated incorrectly.";
    }
}

// 测试3: 验证 size_to_class_map_ 的映射正确性
// 这是对 Phase 3 逻辑的直接测试。
TEST_F(SlabConfigTest, SizeToClassMapIsCorrect) {
    // 1. 测试边界情况
    EXPECT_EQ(config_->get_size_class_index(0), 0); // malloc(0) 应该映射到最小的类别
    EXPECT_EQ(config_->get_size_class_index(1), 0); // 最小的有效请求
    
    // 2. 测试第一个类别的边界
    const SlabConfigInfo& first_class_info = config_->get_info(0);
    EXPECT_EQ(config_->get_size_class_index(first_class_info.block_size), 0);
    // 请求大小比第一个类别大1，应该映射到第二个类别 (索引为1)
    EXPECT_EQ(config_->get_size_class_index(first_class_info.block_size + 1), 1);

    // 3. 测试一些随机的中间值
    size_t size1 = 100;
    size_t idx1 = config_->get_size_class_index(size1);
    const SlabConfigInfo& info1 = config_->get_info(idx1);
    EXPECT_GE(info1.block_size, size1) << "Mapped block_size should be large enough for the request.";
    if (idx1 > 0) {
        const SlabConfigInfo& prev_info1 = config_->get_info(idx1 - 1);
        EXPECT_LT(prev_info1.block_size, size1) << "A smaller size class should have been chosen if available.";
    }

    // 4. 测试最大 small object 的情况
    size_t last_class_idx = config_->get_num_classes() - 1;
    EXPECT_EQ(config_->get_size_class_index(MAX_SMALL_OBJECT_SIZE), last_class_idx);
    
    // 5. 测试超出范围的情况
    EXPECT_EQ(config_->get_size_class_index(MAX_SMALL_OBJECT_SIZE + 1), static_cast<size_t>(-1));
}

// 测试4: 对 get_info 的边界检查
// 测试传入无效索引时，在 Debug 模式下是否会触发 assert。
// 注意：这个测试只在 Debug 模式下有意义，因为 Release 模式会移除 assert。
TEST_F(SlabConfigTest, GetInfoBoundsCheck) {
    size_t num_classes = config_->get_num_classes();
    // 使用 EXPECT_DEATH 来检查断言失败。断言失败会终止程序。
    // 这是一种高级的 GTest 用法，用于测试程序的“死亡”行为。
#if !defined(NDEBUG)
    EXPECT_DEATH(config_->get_info(num_classes), "index < num_classes_");
#else
    // 在 Release 模式下，我们只是打印一条信息，表示此测试被跳过。
    GTEST_LOG_(INFO) << "Skipping death test for get_info in release mode.";
#endif
}