#include "ticker_fast/shm_segment.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace ticker_fast {

namespace {

// Builds the exception for a failed syscall. `err` is passed in rather than read from
// errno here, because the cleanup calls made before throwing may overwrite errno.
[[noreturn]] void throw_errno(const std::string& what, const std::string& name, int err) {
    throw std::runtime_error(what + "(" + name + ") failed: " + std::strerror(err));
}

// Rejects sizes that mmap cannot map (0) or that do not fit in off_t, the signed
// type ftruncate and fstat use for sizes.
void check_size(const std::string& name, std::size_t size) {
    if (size == 0 || size > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        throw std::runtime_error("ShmSegment(" + name + "): invalid size " + std::to_string(size));
    }
}

}  // namespace

ShmSegment ShmSegment::create(const std::string& name, std::size_t size) {
    check_size(name, size);
    // Copy the name before any syscall: if this allocation threw after mmap, the
    // mapping and the name would leak. Moving it into the object later cannot throw.
    std::string owned_name = name;

    // O_EXCL: fail if the name already exists instead of silently reusing an old segment.
    // 0600: only this user can open it.
    const int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        throw_errno("shm_open", name, errno);
    }

    // A new segment has size 0; ftruncate sets its size and the kernel zero-fills it.
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        const int err = errno;
        close(fd);
        shm_unlink(name.c_str());
        throw_errno("ftruncate", name, err);
    }

    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    const int map_err = errno;
    // The mapping holds its own reference to the segment, so the fd is not needed
    // any more, whether mmap worked or not.
    close(fd);
    if (addr == MAP_FAILED) {
        shm_unlink(name.c_str());
        throw_errno("mmap", name, map_err);
    }

    return ShmSegment(addr, size, std::move(owned_name), true);
}

ShmSegment ShmSegment::attach(const std::string& name, std::size_t size) {
    check_size(name, size);
    std::string owned_name = name;  // same reason as in create()

    // No O_CREAT: attaching must never create a segment by accident.
    const int fd = shm_open(name.c_str(), O_RDWR, 0);
    if (fd < 0) {
        throw_errno("shm_open", name, errno);
    }

    // The owner already sized the segment. Check it matches what we expect before
    // mapping it, instead of trusting the caller and reading past the end later.
    struct stat info{};
    if (fstat(fd, &info) != 0) {
        const int err = errno;
        close(fd);
        throw_errno("fstat", name, err);
    }
    if (info.st_size != static_cast<off_t>(size)) {
        close(fd);
        throw std::runtime_error("ShmSegment::attach(" + name + "): size is " +
                                 std::to_string(info.st_size) + ", expected " +
                                 std::to_string(size));
    }

    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    const int map_err = errno;
    close(fd);
    if (addr == MAP_FAILED) {
        throw_errno("mmap", name, map_err);
    }

    return ShmSegment(addr, size, std::move(owned_name), false);
}

ShmSegment::ShmSegment(void* addr, std::size_t size, std::string name, bool owner) noexcept
    : addr_(addr), size_(size), name_(std::move(name)), owner_(owner) {}

ShmSegment::~ShmSegment() { release(); }

// std::exchange(x, v) sets x to v and returns the old value: take it from `other`
// and leave the empty value behind in one step.
ShmSegment::ShmSegment(ShmSegment&& other) noexcept
    : addr_(std::exchange(other.addr_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      name_(std::move(other.name_)),
      owner_(std::exchange(other.owner_, false)) {
    // A moved-from string is valid but its contents are unspecified; make it empty.
    other.name_.clear();
}

ShmSegment& ShmSegment::operator=(ShmSegment&& other) noexcept {
    if (this != &other) {
        // Let go of the segment this object holds now, or it would leak.
        release();
        addr_ = std::exchange(other.addr_, nullptr);
        size_ = std::exchange(other.size_, 0);
        name_ = std::move(other.name_);
        other.name_.clear();
        owner_ = std::exchange(other.owner_, false);
    }
    return *this;
}

void* ShmSegment::data() const noexcept { return addr_; }

std::size_t ShmSegment::size() const noexcept { return size_; }

const std::string& ShmSegment::name() const noexcept { return name_; }

bool ShmSegment::is_owner() const noexcept { return owner_; }

void ShmSegment::release() noexcept {
    // Called from the destructor, so failures are reported, never thrown.
    if (addr_ != nullptr && munmap(addr_, size_) != 0) {
        std::fprintf(stderr, "ticker_fast: munmap(%s) failed: %s\n", name_.c_str(),
                     std::strerror(errno));
    }
    // Unlink removes the name only. Processes that still have it mapped keep working,
    // and the memory is freed once the last of them unmaps.
    if (owner_ && shm_unlink(name_.c_str()) != 0) {
        std::fprintf(stderr, "ticker_fast: shm_unlink(%s) failed: %s\n", name_.c_str(),
                     std::strerror(errno));
    }
    addr_ = nullptr;
    size_ = 0;
    name_.clear();
    owner_ = false;
}

}  // namespace ticker_fast
