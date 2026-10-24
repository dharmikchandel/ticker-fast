#pragma once

#include <cstddef>
#include <string>

namespace ticker_fast {

// RAII wrapper around a POSIX shared memory segment (shm_open + ftruncate + mmap).
// One process creates the segment and becomes its owner; other processes attach to
// the existing segment by name. The mapping is released when the object is destroyed,
// and the owner also removes the name from the system.
// Move-only: a copy would mean two objects releasing the same mapping.
class ShmSegment {
   public:
    // Creates a NEW segment of `size` bytes (O_CREAT | O_EXCL | O_RDWR), zero-filled.
    // `name` must look like "/something" (one leading slash, no others).
    // Throws std::runtime_error with the errno text if the name already exists or any
    // step fails; a failed create leaves no name behind.
    // This object becomes the owner: its destructor calls shm_unlink.
    static ShmSegment create(const std::string& name, std::size_t size);

    // Attaches to an EXISTING segment by name, read-write. Read-write because in
    // Phase 6 the subscriber writes its own read counter into the shared memory.
    // Throws std::runtime_error if the name does not exist, if its size is not
    // `size`, or if any step fails.
    // This object is NOT the owner: its destructor leaves the name in place.
    static ShmSegment attach(const std::string& name, std::size_t size);

    // Unmaps the memory; the owner also unlinks the name. Never throws.
    ~ShmSegment();

    ShmSegment(const ShmSegment&) = delete;
    ShmSegment& operator=(const ShmSegment&) = delete;

    // Moves take over the mapping and leave `other` empty, so its destructor does nothing.
    ShmSegment(ShmSegment&& other) noexcept;
    ShmSegment& operator=(ShmSegment&& other) noexcept;

    // Start of the mapped memory (nullptr after being moved from).
    void* data() const noexcept;
    // Size of the mapping in bytes.
    std::size_t size() const noexcept;
    // The name the segment was created or attached with.
    const std::string& name() const noexcept;
    // True if this object created the segment and will unlink it.
    bool is_owner() const noexcept;

   private:
    ShmSegment(void* addr, std::size_t size, std::string name, bool owner) noexcept;

    // Unmaps, unlinks if owner, and resets every member to the empty state.
    void release() noexcept;

    void* addr_ = nullptr;
    std::size_t size_ = 0;
    std::string name_;
    bool owner_ = false;
};

}  // namespace ticker_fast
