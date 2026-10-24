#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "ticker_fast/shm_segment.hpp"

namespace {

using ticker_fast::ShmSegment;

constexpr std::size_t kSegmentSize = 4096;

// Returns 0 if a segment with this name exists, otherwise the errno from shm_open.
// Asks the OS directly instead of trusting anything ShmSegment reports about itself.
int shm_open_error(const std::string& name) {
    const int fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (fd < 0) {
        return errno;
    }
    close(fd);
    return 0;
}

// Gives every test its own segment names (test name + PID), so tests running in
// parallel or leftovers from an earlier crashed run cannot collide.
class ShmSegmentTest : public ::testing::Test {
   protected:
    void SetUp() override {
        const std::string suffix =
            std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "_" +
            std::to_string(getpid());
        name_ = "/ticker_fast_test_" + suffix;
        other_name_ = "/ticker_fast_test_other_" + suffix;
    }

    // Safety net: if an assertion failed before a segment's owner was destroyed,
    // remove the names here so nothing stays in /dev/shm.
    void TearDown() override {
        shm_unlink(name_.c_str());
        shm_unlink(other_name_.c_str());
    }

    std::string name_;
    std::string other_name_;
};

// 1. Create, write a byte pattern, read it back in the same process.
TEST_F(ShmSegmentTest, CreateWriteRead) {
    ShmSegment segment = ShmSegment::create(name_, kSegmentSize);
    ASSERT_NE(segment.data(), nullptr);
    EXPECT_EQ(segment.size(), kSegmentSize);
    EXPECT_EQ(segment.name(), name_);
    EXPECT_TRUE(segment.is_owner());

    auto* bytes = static_cast<unsigned char*>(segment.data());
    // ftruncate zero-fills a new segment.
    for (std::size_t i = 0; i < kSegmentSize; ++i) {
        ASSERT_EQ(bytes[i], 0U) << "byte " << i;
    }
    for (std::size_t i = 0; i < kSegmentSize; ++i) {
        bytes[i] = static_cast<unsigned char>(i % 251U);
    }
    for (std::size_t i = 0; i < kSegmentSize; ++i) {
        ASSERT_EQ(bytes[i], static_cast<unsigned char>(i % 251U)) << "byte " << i;
    }
}

// 2. The parent creates the segment, a forked child attaches by name and writes,
// and the parent reads the value through its own mapping.
TEST_F(ShmSegmentTest, ForkChildWritesParentReads) {
    constexpr std::uint64_t kChildValue = 0x0123456789ABCDEFULL;
    ShmSegment owner = ShmSegment::create(name_, kSegmentSize);

    const pid_t pid = fork();
    ASSERT_GE(pid, 0) << "fork failed: " << std::strerror(errno);

    if (pid == 0) {
        // Child. It attaches fresh by name, like an independently started process
        // would, instead of using the mapping it inherited from the parent.
        // No gtest assertions here: they would be reported by the child, not the
        // parent. The result goes back through the exit status instead.
        int status = 0;
        try {
            ShmSegment child = ShmSegment::attach(name_, kSegmentSize);
            std::memcpy(child.data(), &kChildValue, sizeof(kChildValue));
        } catch (const std::exception&) {
            status = 1;
        }
        // _exit, not return or exit: the child has its own copy of `owner`, and
        // returning would run its destructor and unlink the parent's segment. _exit
        // also skips gtest's end-of-run code, which belongs to the parent.
        _exit(status);
    }

    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0) << "child failed to attach";

    std::uint64_t seen = 0;
    std::memcpy(&seen, owner.data(), sizeof(seen));
    EXPECT_EQ(seen, kChildValue);
}

// 3. Destroying the owner removes the name from the system.
TEST_F(ShmSegmentTest, OwnerDestructionUnlinks) {
    {
        ShmSegment segment = ShmSegment::create(name_, kSegmentSize);
        ASSERT_EQ(shm_open_error(name_), 0);
    }
    EXPECT_EQ(shm_open_error(name_), ENOENT);
}

// 4. Destroying an attached (non-owner) object leaves the segment in place.
TEST_F(ShmSegmentTest, NonOwnerDestructionKeepsSegment) {
    ShmSegment owner = ShmSegment::create(name_, kSegmentSize);
    {
        ShmSegment attached = ShmSegment::attach(name_, kSegmentSize);
        EXPECT_FALSE(attached.is_owner());
        static_cast<unsigned char*>(attached.data())[0] = 42U;
    }
    EXPECT_EQ(shm_open_error(name_), 0);
    EXPECT_EQ(static_cast<unsigned char*>(owner.data())[0], 42U);
}

// 5. Creating a name that already exists throws, and does not remove the existing one.
TEST_F(ShmSegmentTest, DoubleCreateThrows) {
    ShmSegment first = ShmSegment::create(name_, kSegmentSize);
    EXPECT_THROW(ShmSegment::create(name_, kSegmentSize), std::runtime_error);
    EXPECT_EQ(shm_open_error(name_), 0);
}

// 6. Attaching to a name that was never created throws.
TEST_F(ShmSegmentTest, AttachMissingThrows) {
    ASSERT_EQ(shm_open_error(name_), ENOENT);
    EXPECT_THROW(ShmSegment::attach(name_, kSegmentSize), std::runtime_error);
}

// 7. Attaching with a different size than the segment has throws.
TEST_F(ShmSegmentTest, AttachSizeMismatchThrows) {
    ShmSegment owner = ShmSegment::create(name_, kSegmentSize);
    EXPECT_THROW(ShmSegment::attach(name_, kSegmentSize * 2), std::runtime_error);
    EXPECT_THROW(ShmSegment::attach(name_, kSegmentSize / 2), std::runtime_error);
}

// 8a. Move construction hands the segment over; the moved-from object is empty and
// its destructor does nothing. Exactly one unlink happens, when the new owner dies.
TEST_F(ShmSegmentTest, MoveConstructTransfersOwnership) {
    {
        ShmSegment source = ShmSegment::create(name_, kSegmentSize);
        void* const addr = source.data();

        ShmSegment target(std::move(source));
        EXPECT_EQ(source.data(), nullptr);
        EXPECT_EQ(source.size(), 0U);
        EXPECT_TRUE(source.name().empty());
        EXPECT_FALSE(source.is_owner());

        EXPECT_EQ(target.data(), addr);
        EXPECT_EQ(target.size(), kSegmentSize);
        EXPECT_EQ(target.name(), name_);
        EXPECT_TRUE(target.is_owner());
    }
    EXPECT_EQ(shm_open_error(name_), ENOENT);
}

// 8b. Move assignment releases the segment the target held before taking the new one.
TEST_F(ShmSegmentTest, MoveAssignReleasesOldSegment) {
    {
        ShmSegment source = ShmSegment::create(name_, kSegmentSize);
        ShmSegment target = ShmSegment::create(other_name_, kSegmentSize);

        target = std::move(source);
        EXPECT_EQ(shm_open_error(other_name_), ENOENT);  // old segment released now
        EXPECT_EQ(shm_open_error(name_), 0);
        EXPECT_EQ(target.name(), name_);
        EXPECT_EQ(source.data(), nullptr);
        EXPECT_FALSE(source.is_owner());
    }
    EXPECT_EQ(shm_open_error(name_), ENOENT);
}

// 9. A zero size is rejected before any syscall.
TEST_F(ShmSegmentTest, ZeroSizeThrows) {
    EXPECT_THROW(ShmSegment::create(name_, 0), std::runtime_error);
    EXPECT_EQ(shm_open_error(name_), ENOENT);
}

}  // namespace
