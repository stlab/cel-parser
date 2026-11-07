// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_LOOKUP_HPP
#define CEL_PARSER_LOOKUP_HPP

#include <array>
#include <cel/parser.hpp>
#include <cel/runtime.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cel {
/// Selects ordinary value-name/call dispatch or operation-only receiver dispatch.
/// Example: lookup_selector::member never resolves a bare function binding.
enum class lookup_selector {
    ordinary, ///< Resolves scopes, callable names, registrations, and built-ins.
    member    ///< Resolves receiver-plus-explicit operands without value-name lookup.
};
/// Selects all active scopes or only persistent library scopes.
/// Example: scope_selection::library_only hides enclosing host bindings.
enum class scope_selection {
    all,         ///< Searches transient scopes before persistent libraries.
    library_only ///< Searches persistent libraries and registrations only.
};
/// Owns a copyable scope callback; false leaves the builder completely unchanged.
/// Captured references remain the client's lifetime and synchronization responsibility.
/// Example: [](std::string_view, dyn_segment&, std::size_t, source_span) { return false; }.
using scope_callback =
    std::function<bool(std::string_view, dyn_segment&, std::size_t, source_span)>;
/// Owns a checked emitter whose successful effect consumes its declared inputs and adds one result.
/// Callback exceptions propagate; an error need only leave the builder safely discardable.
/// Example: [](dyn_segment& b, source_span) { b.project_tuple(0); }.
using operation_emitter = std::function<void(dyn_segment&, source_span)>;

/// Builds public structural witnesses without exposing native composite layout.
namespace parser_detail {
/// Maps a trusted concrete leaf to its public runtime descriptor.
template <class T>
struct native_descriptor {
    /// Returns exact concrete identity; opaque nontrivial leaves fail compilation.
    static value_type get() { return value_type::leaf<T>(); }
};
/// Maps an owning standard vector through the runtime's public structural descriptor.
template <class T, class A>
struct native_descriptor<std::vector<T, A>> {
    /// Retains the recursively complete element; cached metadata shares immutable state.
    /// Complexity: O(distinct concrete descriptor nodes and fields) on first use, O(1) thereafter.
    static value_type get() {
        static const auto type = value_type::array(native_descriptor<T>::get());
        return type;
    }
};
/// Maps an owning standard tuple without assuming its native object layout.
template <class... T>
struct native_descriptor<std::tuple<T...>> {
    /// Retains source-ordered complete fields.
    /// Complexity: O(distinct concrete descriptor nodes and fields) on first use, O(1) thereafter.
    static value_type get() {
        static const auto type = [] {
            const std::array fields{native_descriptor<T>::get()...};
            return value_type::tuple(fields);
        }();
        return type;
    }
};
/// Maps source unit without relying on empty class-template argument deduction.
template <>
struct native_descriptor<std::tuple<>> {
    /// Returns the public unit descriptor.
    static value_type get() { return {}; }
};
/// Hashes owned names and borrowed query strings with the same hash.
struct name_hash {
    using is_transparent = void; ///< Enables allocation-free borrowed lookup.

    /// Hashes bytes; complexity is O(name.size()).
    std::size_t operator()(std::string_view name) const noexcept {
        return std::hash<std::string_view>{}(name);
    }
};
} // namespace parser_detail

/// Returns the canonical origin below count local operands without mutating the builder.
/// Throws invalid_argument on local underflow; accounting failures propagate.
/// Complexity: O(local prefix depth), with no type copies or allocations.
/// Example: builder.make_tuple(2, operand_origin(builder, 2)).
stack_origin operand_origin(const dyn_segment& builder, std::size_t count);

/// Owns exact registrations, nominal schemas, and LIFO scope callbacks.
/// Names, descriptors, and callbacks never borrow registration arguments.
/// Freezing emitted runtime code retains callback captures after this lookup dies.
/// Client-captured references must outlive every execution that uses them.
/// Mutable callbacks are not made thread-safe by registry ownership.
/// Example: op_lookup l; l.register_op<double, double>("twice", [](double x) { return x * 2; }).
class op_lookup {
    /// Owns one complete signature and its checked emission callback.
    struct operation {
        std::vector<value_type> inputs; ///< Owns exact older-first operands.
        value_type result;              ///< Owns the complete result witness.
        operation_emitter emit;         ///< Owns required compiler-side state.
    };
    /// Owns the first marker emitter for one ordinary function name.
    struct function_binding {
        value_type marker;      ///< Retains exact marker identity.
        operation_emitter emit; ///< Owns marker construction state.
    };
    /// Stores owned names with allocation-free string-view lookup.
    template <class T>
    using names = std::unordered_map<std::string, T, parser_detail::name_hash, std::equal_to<>>;
    names<value_type> types_;
    names<std::vector<operation>> operations_;
    names<std::vector<operation>> structural_;
    names<std::vector<operation>> builtins_;
    names<function_binding> functions_;
    std::unordered_map<std::type_index, std::string> marker_names_;
    std::vector<scope_callback> libraries_;
    std::vector<scope_callback> scopes_;

    /// Inserts a unique complete operand signature; result-only overloads are rejected.
    /// Throws invalid_argument for empty names, absent emitters, or duplicate input identities.
    /// Complexity: O(overloads * arity * distinct compared plans + name bytes).
    static void insert(names<std::vector<operation>>& destination,
                       std::string_view name,
                       std::span<const value_type> inputs,
                       value_type result,
                       operation_emitter emit);
    /// Selects exact inputs before invoking only the matching emitter.
    /// False leaves the builder untouched; comparison/emitter failures propagate.
    /// Complexity: O(overloads * arity * distinct compared plans).
    static bool emit_match(const names<std::vector<operation>>& registrations,
                           std::string_view name,
                           dyn_segment& builder,
                           std::size_t arity,
                           source_span span);
    /// Searches eligible LIFO scopes; declined callbacks must leave the builder unchanged.
    /// Complexity: O(scopes + selected callback work).
    bool scope_match(std::string_view name,
                     dyn_segment& builder,
                     std::size_t arity,
                     source_span span,
                     scope_selection selection) const;
    /// Resolves direct operation signatures; zero arity skips scopes.
    /// Complexity: O(scopes + overload matching).
    bool direct_match(std::string_view name,
                      dyn_segment& builder,
                      std::size_t arity,
                      source_span span,
                      scope_selection selection) const;
    /// Installs retained scalar signatures without marking them as client registrations.
    void install_builtins();
    /// Inserts one typed built-in using the same exact runtime thunk boundary.
    template <class R, class... A, class F>
    void builtin(std::string_view name, F callback) {
        const std::array<value_type, sizeof...(A)> inputs{
            parser_detail::native_descriptor<A>::get()...};
        insert(builtins_, name, inputs, parser_detail::native_descriptor<R>::get(),
               [callback = std::move(callback)](dyn_segment& builder, source_span) {
                   builder.template op<R, A...>(callback);
               });
    }
    /// Inserts an owned leaf or record name; duplicate/reserved names are rejected.
    /// Complexity: O(name bytes), expected map insertion plus metadata release on failure.
    void insert_type(std::string_view name, value_type type);
    /// Keeps the first function emitter while admitting new marker types for that same name.
    /// Throws invalid_argument if a marker identity already names a distinct function.
    /// Failure leaves existing bindings and marker reservations unchanged.
    /// Complexity: O(name bytes), expected hash insertion.
    void insert_function(std::string_view name, value_type marker, operation_emitter emit);

public:
    /// Creates only core operators, casts, and the persistent full-range constructor.
    /// Standard numeric function names require install_standard_library().
    /// Complexity: O(retained signature count) metadata and callback storage.
    op_lookup();
    /// Prevents shallow copies of callback state being advertised as independent registries.
    op_lookup(const op_lookup&) = delete;
    /// Prevents copied assignment.
    op_lookup& operator=(const op_lookup&) = delete;
    /// Transfers owned registration and scope state.
    op_lookup(op_lookup&&) noexcept = default;
    /// Transfers state and releases old registrations.
    /// Complexity: O(old owned state).
    op_lookup& operator=(op_lookup&&) noexcept = default;
    /// Registers an opaque trivially-copyable leaf under an owned nonempty name.
    /// Throws invalid_argument for duplicate/reserved names or a structural native adapter.
    /// Complexity: O(name bytes), expected map insertion.
    /// Example: lookup.register_type<my_trivial_type>("Point").
    template <class T>
    void register_type(std::string_view name) {
        auto type = value_type::leaf<T>();
        if (!type.compatible_with(parser_detail::native_descriptor<std::remove_cv_t<T>>::get()))
            throw std::invalid_argument("named leaf registration requires an opaque leaf");
        insert_type(name, std::move(type));
    }
    /// Registers an owned complete nominal record schema, never a guessed native layout.
    /// Throws invalid_argument for empty/duplicate/reserved names or invalid field names.
    /// Complexity: O(fields + field-name bytes + name bytes), expected map insertion.
    /// Example: lookup.register_record("Flag", std::array{record_field{"on",
    /// value_type::leaf<bool>()}}).
    void register_record(std::string_view name, std::span<const record_field> fields);
    /// Returns a retained descriptor for a registered or core scalar name, or nullopt.
    /// Complexity: O(name bytes), expected hash lookup.
    /// Example: lookup.find_type("f64")->kind() == type_kind::leaf.
    std::optional<value_type> find_type(std::string_view name) const;
    /// Registers one exact complete operation; a return type never distinguishes overloads.
    /// Emitters must consume inputs and produce one result with compatible ownership.
    /// Throws invalid_argument for empty names, absent emitters, or duplicate inputs.
    /// Complexity: O(overloads * arity * distinct compared plans + name bytes).
    /// Example: lookup.register_operation("first", inputs, result, [](dyn_segment& b, source_span)
    /// { b.project_tuple(0); }).
    void register_operation(std::string_view name,
                            std::span<const value_type> inputs,
                            value_type result,
                            operation_emitter emit);
    /// Registers a lower-priority structural signature with complete recursive identities.
    /// Persistent operations precede this tier; core built-ins follow it.
    /// Follows register_operation's error, emitter, and complexity contract.
    /// Example: lookup.register_structural_op("first", inputs, result, project).
    void register_structural_op(std::string_view name,
                                std::span<const value_type> inputs,
                                value_type result,
                                operation_emitter emit);
    /// Owns a typed const callback accepted by the runtime's exact thunk constraints.
    /// Concrete vectors/tuples use trusted public adapters; opaque leaves are trivially copyable.
    /// Use std::tuple<> for source unit; void is not a source value.
    /// Duplicate operands are rejected even if R differs. Callback references stay client-owned.
    /// Complexity: O(overloads * arity * distinct compared plans + name/capture storage +
    /// first-use complete structural descriptor construction).
    /// Example: lookup.register_op<double, double>("negate", [](double x) { return -x; }).
    template <class R, class... A, class F>
    requires(!std::is_void_v<R> && requires(dyn_segment & b, F callback) {
        b.template op<R, A...>(std::move(callback));
    }) void register_op(std::string_view name, F callback) {
        const std::array<value_type, sizeof...(A)> inputs{
            parser_detail::native_descriptor<A>::get()...};
        auto owned = std::make_shared<const F>(std::move(callback));
        register_operation(name, inputs, parser_detail::native_descriptor<R>::get(),
                           [owned = std::move(owned)](dyn_segment& builder, source_span) {
                               builder.template op<R, A...>([owned](A... arguments) -> R {
                                   return std::invoke(*owned, std::move(arguments)...);
                               });
                           });
    }
    /// Binds an ordinary name to its first owned trivially-copyable marker.
    /// Repeated installers may use another marker for the same name; the first remains selected.
    /// Throws invalid_argument for a structural marker or if its type names a distinct function.
    /// Failure leaves existing bindings and marker reservations unchanged.
    /// Complexity: O(name bytes + marker size), expected hash insertion.
    /// Example: lookup.register_function("convert", my_marker{}).
    template <class M>
    void register_function(std::string_view name, M marker) {
        const auto type = value_type::leaf<M>();
        auto owned = value::from(marker);
        if (!type.compatible_with(owned.type()))
            throw std::invalid_argument("function markers require an opaque leaf");
        insert_function(name, type, [owned = std::move(owned)](dyn_segment& builder, source_span) {
            builder.just(owned);
        });
    }
    /// Owns a transient scope searched before persistent registrations and libraries.
    /// Throws invalid_argument for an empty callback.
    /// Complexity: amortized O(1), plus callback ownership.
    /// Example: lookup.push_scope([](auto, auto&, auto, auto) { return false; }).
    void push_scope(scope_callback callback);
    /// Retires the newest transient scope.
    /// Precondition: at least one transient scope is active.
    /// Complexity: O(retired capture destruction).
    /// Example: lookup.pop_scope().
    void pop_scope();
    /// Owns a persistent LIFO library scope visible in independent closure bodies.
    /// Precondition: no transient scopes are active.
    /// Throws invalid_argument for an empty callback.
    /// Complexity: amortized O(1), plus callback ownership.
    /// Example: lookup.push_library_scope(library_callback).
    void push_library_scope(scope_callback callback);
    /// Transfers transient scopes out, retaining persistent libraries and all registrations.
    /// Example: auto saved = lookup.isolate_scopes().
    std::vector<scope_callback> isolate_scopes();
    /// Retires current transient scopes and restores the previously owned set in order.
    /// Precondition: saved came from this lookup's isolate_scopes().
    /// Complexity: O(retired captures).
    /// Example: lookup.restore_scopes(std::move(saved)).
    void restore_scopes(std::vector<scope_callback> saved);
    /// Resolves exact older-first operands and emits without executing the operation.
    /// Ordinary calls count the marker/callee plus explicit arguments; members count the receiver.
    /// Precondition: callbacks do not modify this lookup's registrations or scope sets.
    /// Throws parse_error with span on no match; comparison and selected-emitter failures
    /// propagate. Complexity: O(scopes + overloads * arity * distinct compared plans + local prefix
    /// depth for marker retirement); captures and metadata allocate during compilation, not scalar
    /// execution. Example: lookup.apply("+", builder, 2, source_span{0, 3}).
    void apply(std::string_view name,
               dyn_segment& builder,
               std::size_t arity,
               source_span span,
               lookup_selector selector = lookup_selector::ordinary,
               scope_selection selection = scope_selection::all) const;
    /// Emits only retained identity casts: bool to bool and f64 to f64.
    /// Only the target spellings "bool" and "f64" are recognized; registered aliases are not.
    /// Throws parse_error with span for an unsupported target/source or underflow.
    /// Complexity: O(target bytes + compared metadata).
    /// Example: lookup.apply_cast("f64", builder, source_span{0, 8}).
    void apply_cast(std::string_view target, dyn_segment& builder, source_span span) const;
};

/// Installs the eleven retained standard numeric names and f64 signatures explicitly.
/// Independent disjoint client overloads compose; reinstalling a signature throws invalid_argument.
/// Failure leaves a safely discardable, possibly partially installed lookup.
/// Complexity: O(installed signatures * existing overloads * compared metadata).
/// Example: op_lookup lookup; install_standard_library(lookup).
void install_standard_library(op_lookup& lookup);

/// type_expression = identifier [ "(" [ type_expression { "," type_expression } ] ")" ]
/// | "[" type_expression "]" | "(" [ type_expression [ "," [ type_expression
/// { "," type_expression } ] ] ] ")".
/// Parses one current-token type and leaves the following host token unconsumed.
/// Fn requires a parameter tuple and a result; singleton tuples require their comma.
/// Ordinary aggregates admit registered leaves; callable parameters obey runtime eligibility.
/// Throws source-located parse_error for invalid syntax/types, producer errors, or nesting
/// exceeding 128 productions. Allocation/accounting failures propagate.
/// Complexity: O(tokens + name bytes + complete descriptor construction), O(nodes + 128) storage.
/// Example: token_cursor cursor(tokens); auto type = parse_type(cursor, lookup).
value_type parse_type(token_cursor& cursor, const op_lookup& lookup);
/// Parses one type directly from the owned text lexer, following the inline grammar contract.
/// Leaves the following host token unconsumed without materializing a separate token array.
/// Throws the source diagnostics and resource errors documented by the token-cursor overload.
/// Complexity: O(lexed source bytes + complete descriptor construction), O(nodes + 128) storage.
/// Example: lex_lexer cursor("[f64], next"); auto type = parse_type(cursor, lookup).
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup);
/// Parses one token type prefix and returns its exact consumed byte interval in parsed_span.
/// Follows the two-argument prefix overload's grammar, errors, ownership, and complexity.
/// - Postcondition: parsed_span excludes surrounding whitespace and the following host token.
/// Leaves parsed_span unchanged if parsing fails; the cursor may have advanced.
/// Example: source_span span; auto type = parse_type(cursor, lookup, span).
value_type parse_type(token_cursor& cursor, const op_lookup& lookup, source_span& parsed_span);
/// Parses one text type prefix and returns its exact consumed interval without a token copy.
/// Follows the token span-output overload's grammar, errors, ownership, and complexity.
/// Example: lex_lexer c("[f64] , next"); source_span s; auto t = parse_type(c, lookup, s).
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup, source_span& parsed_span);
/// Parses one complete type and rejects trailing input; follows the inline grammar contract.
/// Complexity: O(tokens + name bytes + complete descriptor construction), O(nodes + 128) storage.
/// Example: auto type = parse_type(std::span<const token>(tokens), lookup).
value_type parse_type(std::span<const token> tokens, const op_lookup& lookup);
} // namespace cel
// [knowledge] Include the compiler after op_lookup is complete; this header also uses the token
// umbrella.
#include <cel/parser/compiler.hpp>
#endif
