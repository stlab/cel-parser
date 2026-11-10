// SPDX-License-Identifier: BSL-1.0
#include <cel/parser/compiler.hpp>
#include <cstdlib>
#include <new>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

namespace {
/// Injects allocation failure only during a single-threaded public comparison boundary.
bool fail_allocation = false;
/// Counts successful replacement allocations remaining before the injected failure.
std::size_t allowed_allocations = 0;
/// Marks one registration whose failure must not reserve the marker identity.
struct recoverable_marker {};
/// Restores ordinary allocation on every exit from an injected boundary.
struct allocation_failure {
    /// Allows count replacement allocations, then injects failure after fixture construction.
    explicit allocation_failure(std::size_t count = 0) {
        allowed_allocations = count;
        fail_allocation = true;
    }
    /// Restores allocation before assertions or doctest diagnostics.
    ~allocation_failure() { fail_allocation = false; }
};
} // namespace
/// Supplies ordinary test allocation or throws at the injected public boundary.
void* operator new(std::size_t size) {
    if (fail_allocation) {
        if (allowed_allocations == 0) throw std::bad_alloc();
        --allowed_allocations;
    }
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
/// Releases memory acquired by the test's ordinary replacement allocator.
void operator delete(void* memory) noexcept { std::free(memory); }

TEST_CASE("allocating signature comparisons propagate instead of becoming no match") {
    cel::op_lookup lookup;
    const auto expected =
        cel::value_type::array(cel::value_type::array(cel::value_type::leaf<double>()));
    lookup.register_operation("observe", std::array{expected}, cel::value_type::leaf<bool>(),
                              [](cel::dyn_segment& builder, cel::source_span) {
                                  builder.op<bool, std::vector<std::vector<double>>>(
                                      [](std::vector<std::vector<double>>) { return true; });
                              });
    auto child =
        cel::value::array(cel::value_type::leaf<double>(), std::array{cel::value::from(3.0)});
    auto actual = cel::value::array(child.type(), std::array{child});
    cel::dyn_segment builder;
    builder.just(actual);
    bool propagated = false;
    try {
        const allocation_failure fault;
        lookup.apply("observe", builder, 1, {});
    } catch (const std::bad_alloc&) {
        propagated = true;
    }
    CHECK(propagated);
    lookup.apply("observe", builder, 1, {});
    cel::raw_stack stack;
    CHECK(std::move(builder).finish().prepare<bool>().call(stack));

    bool duplicate_comparison_propagated = false;
    try {
        const allocation_failure fault;
        lookup.register_operation("observe", std::array{actual.type()},
                                  cel::value_type::leaf<bool>(),
                                  [](cel::dyn_segment&, cel::source_span) {});
    } catch (const std::bad_alloc&) {
        duplicate_comparison_propagated = true;
    }
    CHECK(duplicate_comparison_propagated);
}

TEST_CASE("failed function registration never reserves an unbound marker") {
    const auto warm_metadata = cel::value::from(recoverable_marker{});
    (void)warm_metadata;
    for (std::size_t count = 0;; ++count) {
        cel::op_lookup lookup;
        bool completed = true;
        try {
            const allocation_failure fault(count);
            lookup.register_function("candidate_function_name_owned_beyond_inline_string_storage",
                                     recoverable_marker{});
        } catch (const std::bad_alloc&) {
            completed = false;
        }
        if (completed) break;
        CHECK_NOTHROW(lookup.register_function("recovered", recoverable_marker{}));
    }
}
