// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_LOOKUP_ACCESS_HPP
#define CEL_PARSER_LOOKUP_ACCESS_HPP
#include <cel/parser/lookup.hpp>

namespace cel::parser_detail {
/// Selects ordinary calls or receiver-only dispatch.
enum class lookup_selector { ordinary, member };
/// Selects whether transient compiler scopes participate.
enum class scope_selection { all, library_only };

/// Gives compilation access to transient scopes and checked operation dispatch.
struct lookup_access {
    /// Installs a transient scope under the registry's scope contract.
    static void push_scope(op_lookup& lookup, scope_callback callback) {
        lookup.push_scope(std::move(callback));
    }
    /// Retires the newest transient scope; requires an active transient scope.
    static void pop_scope(op_lookup& lookup) { lookup.pop_scope(); }
    /// Transfers transient scopes out while compiling an independent body.
    static std::vector<scope_callback> isolate_scopes(op_lookup& lookup) {
        return lookup.isolate_scopes();
    }
    /// Restores the scopes previously transferred from this registry.
    /// Complexity: O(retired captures).
    static void restore_scopes(op_lookup& lookup, std::vector<scope_callback> saved) {
        lookup.restore_scopes(std::move(saved));
    }
    /// Emits a checked operation under the registry's dispatch and error contract.
    /// Complexity: O(scopes + overload matching + emitted code).
    static void apply(const op_lookup& lookup,
                      std::string_view name,
                      dyn_segment& builder,
                      std::size_t arity,
                      source_span span,
                      lookup_selector selector = lookup_selector::ordinary,
                      scope_selection selection = scope_selection::all) {
        lookup.apply(name, builder, arity, span, selector, selection);
    }
    /// Emits a retained identity cast or throws a located type diagnostic.
    static void apply_cast(const op_lookup& lookup,
                           std::string_view target,
                           dyn_segment& builder,
                           source_span span) {
        lookup.apply_cast(target, builder, span);
    }
};
} // namespace cel::parser_detail
#endif
