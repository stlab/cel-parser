// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_MEASUREMENT_ALLOCATION_PROBE_HPP
#define CEL_PARSER_MEASUREMENT_ALLOCATION_PROBE_HPP
#include <cassert>
#include <cstdlib>
#include <limits>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif
/// Counts ordinary and over-aligned allocations during one exclusive measurement.
inline bool measuring_allocations = false;
/// Retains the number of allocation calls, not bytes or live allocations.
inline std::size_t measured_allocations = 0;
/// Records one allocation inside the active measurement.
inline void record_allocation() {
    if (measuring_allocations) ++measured_allocations;
}
/// Allocates ordinary storage and counts only the measured interval.
void* operator new(std::size_t size) {
    record_allocation();
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
/// Releases ordinarily allocated storage.
void operator delete(void* memory) noexcept { std::free(memory); }
/// Allocates an aligned extent using its matching platform allocator.
void* operator new(std::size_t size, std::align_val_t alignment) {
    record_allocation();
    const auto align = static_cast<std::size_t>(alignment);
    const auto extent = size == 0 ? std::size_t{1} : size;
    if (extent > std::numeric_limits<std::size_t>::max() - (align - 1)) throw std::bad_alloc();
    const auto rounded = ((extent + align - 1) / align) * align;
#if defined(_WIN32)
    auto* memory = _aligned_malloc(rounded, align);
#else
    auto* memory = std::aligned_alloc(align, rounded);
#endif
    if (!memory) throw std::bad_alloc();
    return memory;
}
/// Releases aligned storage using its matching platform facility.
void operator delete(void* memory, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
/// Releases sized ordinary storage.
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }
/// Releases sized aligned storage.
void operator delete(void* memory, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(memory, alignment);
}
/// Counts array allocations through the same ordinary allocation boundary.
void* operator new[](std::size_t size) { return ::operator new(size); }
/// Releases ordinary arrays.
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
/// Counts over-aligned array allocations.
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
/// Releases aligned arrays.
void operator delete[](void* memory, std::align_val_t alignment) noexcept {
    ::operator delete(memory, alignment);
}
/// Releases sized ordinary arrays.
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }
/// Releases sized aligned arrays.
void operator delete[](void* memory, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(memory, alignment);
}
/// Bounds allocation counting, restoring ordinary operation even after an exception.
struct allocation_measurement {
    /// Starts a new exclusive allocation count.
    /// Precondition: no allocation measurement is active.
    allocation_measurement() {
        assert(!measuring_allocations);
        measured_allocations = 0;
        measuring_allocations = true;
    }
    /// Stops counting before reporting and diagnostics.
    ~allocation_measurement() { measuring_allocations = false; }
    /// Prevents duplicated interval ownership.
    allocation_measurement(const allocation_measurement&) = delete;
    /// Prevents duplicated interval assignment.
    allocation_measurement& operator=(const allocation_measurement&) = delete;
};
#endif
