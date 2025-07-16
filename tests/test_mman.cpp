#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <system_error>

#include <my_malloc/sys/mman.hpp>

class MmanWrapperTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MmanWrapperTest, SuccessfulMapAndReadWrite) {
    const size_t TEST_SIZE = 4096;

    void* ptr = mmap(nullptr, TEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    ASSERT_NE(ptr, nullptr) << "mmap returned a null pointer.";
    ASSERT_NE(ptr, MAP_FAILED) << "mmap returned MAP_FAILED. Errno: " << errno << " (" << strerror(errno) << ")";

    unsigned char write_buffer[TEST_SIZE];
    memset(write_buffer, 0xAB, TEST_SIZE);
    
    ASSERT_NO_THROW({
        memcpy(ptr, write_buffer, TEST_SIZE);
    }) << "Writing to the mapped memory caused an exception.";

    unsigned char read_buffer[TEST_SIZE];
    ASSERT_NO_THROW({
        memcpy(read_buffer, ptr, TEST_SIZE);
    }) << "Reading from the mapped memory caused an exception.";

    EXPECT_EQ(memcmp(write_buffer, read_buffer, TEST_SIZE), 0) << "Memory content verification failed. Data read back is not what was written.";

    int unmap_result = munmap(ptr, TEST_SIZE);
    
    EXPECT_EQ(unmap_result, 0) << "munmap failed to release memory. Errno: " << errno << " (" << strerror(errno) << ")";
}

TEST_F(MmanWrapperTest, FailsWithInvalidArguments) {
    void* ptr = mmap(nullptr, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    EXPECT_EQ(ptr, MAP_FAILED) << "mmap with zero length should have failed, but it did not.";
    EXPECT_EQ(errno, EINVAL) << "mmap with zero length should set errno to EINVAL, but it was set to " << errno;
}