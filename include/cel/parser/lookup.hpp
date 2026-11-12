// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_LOOKUP_HPP
#define CEL_PARSER_LOOKUP_HPP

#include <array>
#include <cel/parser/diagnostic.hpp>
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

/// \file
/// Provides owned type and operation registrations for extension libraries.
/// Include `<cel/parser/compiler.hpp>` to compile expressions with a registry.

namespace cel {
/// \addtogroup parser_extensions
/// @{

namespace parser_detail {
enum class lookup_selector;
enum class scope_selection;
struct lookup_access;
} // namespace parser_detail
/// Owns a copyable resolver called with a borrowed name, builder, operand count, and source span.
/// The name and builder are borrowed only for the call. Operands are older-first.
/// Returning false must leave instructions and operands unchanged. Returning true must
/// emit the resolved value or operation, consuming arity operands and adding one result.
/// Exceptions propagate; after an emission failure, the builder need only be safely discardable.
/// Captured references must outlive uses of the callback and any emitted code retaining them.
/// Clients synchronize shared mutable captures.
/// Example: [](std::string_view, dyn_segment&, std::size_t, source_span) { return false; }.
using scope_callback =
    std::function<bool(std::string_view, dyn_segment&, std::size_t, source_span)>;
/// Owns a checked emitter whose successful effect consumes its declared inputs and adds one result.
/// The builder is borrowed only for the call; span locates the source operation.
/// Emitted code must retain the values, descriptors, and captures it needs for execution.
/// Callback exceptions propagate; an error need only leave the builder safely discardable.
/// Example: [](dyn_segment& b, source_span) { b.project_tuple(0); }.
using operation_emitter = std::function<void(dyn_segment&, source_span)>;

/// Supplies allocation-free heterogeneous name lookup.
namespace parser_detail {
/// Hashes owned names and borrowed query strings with the same hash.
struct name_hash {
    using is_transparent = void; ///< Enables allocation-free borrowed lookup.

    /// Hashes bytes; complexity is O(name.size()).
    std::size_t operator()(std::string_view name) const noexcept {
        return std::hash<std::string_view>{}(name);
    }
};
} // namespace parser_detail

/// Owns exact registrations, nominal schemas, and LIFO scope callbacks.
/// Owns copied names, retained descriptors, and stored callbacks, not references inside captures.
/// register_op retains its runtime callback in emitted code after this lookup dies.
/// Custom scope callbacks and operation emitters must retain state in the code they emit.
/// Client-captured references must outlive every execution that uses them.
/// Mutable callbacks are not made thread-safe by registry ownership.
/// Registration matches complete compatible input descriptors, including nominal identity
/// and ownership representation; equal layouts or display names do not establish a match.
/// Copies are unavailable; moves transfer registry ownership.
/// Register a receiver operation before transferring the lookup into a parser:
/// \code{.cpp}
/// #include <cel/parser.hpp>
///
/// cel::op_lookup lookup;
/// lookup.register_op<double, double>("twice", [](double x) { return x * 2; });
/// cel::parser compiler(std::move(lookup));
/// auto code = compiler.parse_str("3.0.twice()");
/// \endcode
class op_lookup {
    friend struct parser_detail::lookup_access;
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
                     parser_detail::scope_selection selection) const;
    /// Resolves direct operation signatures; zero arity skips scopes.
    /// Complexity: O(scopes + overload matching).
    bool direct_match(std::string_view name,
                      dyn_segment& builder,
                      std::size_t arity,
                      source_span span,
                      parser_detail::scope_selection selection) const;
    /// Installs retained scalar signatures without marking them as client registrations.
    void install_builtins();
    /// Inserts one typed built-in using the same exact runtime thunk boundary.
    template <class R, class... A, class F>
    void builtin(std::string_view name, F&& callback) {
        const std::array<value_type, sizeof...(A)> inputs{native_type<A>()...};
        insert(builtins_, name, inputs, native_type<R>(),
               [callback = std::forward<F>(callback)](dyn_segment& builder, source_span) {
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

    /// Owns a transient scope searched newest-first before persistent libraries and registrations.
    /// Precondition: callback follows scope_callback's emission and capture-lifetime contract.
    /// Throws invalid_argument for an empty callback.
    /// Complexity: amortized O(1), plus callback ownership.
    /// Example: lookup.push_scope([](auto, auto&, auto, auto) { return false; }).
    void push_scope(scope_callback callback);
    /// Retires the newest transient scope.
    /// Precondition: at least one transient scope is active.
    /// Complexity: O(retired capture destruction).
    /// Example: lookup.pop_scope().
    void pop_scope();
    /// Transfers transient scopes out, retaining persistent libraries and all registrations.
    /// Postcondition: no transient scopes remain active; the returned vector owns them in order.
    /// Example: auto saved = lookup.isolate_scopes().
    std::vector<scope_callback> isolate_scopes();
    /// Retires current transient scopes and restores the previously owned set in order.
    /// Precondition: saved came from this lookup's isolate_scopes().
    /// Complexity: O(retired captures).
    /// Example: lookup.restore_scopes(std::move(saved)).
    void restore_scopes(std::vector<scope_callback> saved);
    /// Resolves exact older-first operands and emits code without executing runtime operations.
    /// Ordinary calls count the marker/callee plus explicit arguments; members count the receiver.
    /// Ordinary resolution searches scopes, function bindings, operations, structural operations,
    /// built-ins, then callable invocation. Member resolution omits bare function bindings and
    /// callable invocation; named record projection follows the operation tiers.
    /// Direct zero-explicit-argument marker calls skip scopes for the operation lookup.
    /// selection excludes transient scopes only when set to library_only.
    /// Precondition: callbacks do not modify this lookup's registrations or scope sets.
    /// Throws parse_error(lookup_error) with span on no match. Throws std::invalid_argument if
    /// a selected registered emitter violates its result/depth contract. Comparison and
    /// callback failures propagate. A selected-emitter failure need not leave builder unchanged.
    /// Complexity: O(name bytes + scopes + overloads * arity * distinct compared plans +
    /// selected callback work); emission allocates code, captures, and metadata as needed.
    /// Example: lookup.apply("+", builder, 2, source_span{0, 3}).
    void apply(std::string_view name,
               dyn_segment& builder,
               std::size_t arity,
               source_span span,
               parser_detail::lookup_selector selector,
               parser_detail::scope_selection selection) const;
    /// Emits only retained identity casts: bool to bool and f64 to f64.
    /// Only the target spellings "bool" and "f64" are recognized; registered aliases are not.
    /// Throws parse_error(type_error) with span for an unsupported target/source or underflow.
    /// Complexity: O(target bytes + compared metadata).
    /// Example: lookup.apply_cast("f64", builder, source_span{0, 8}).
    void apply_cast(std::string_view target, dyn_segment& builder, source_span span) const;

public:
    /// Returns a retained descriptor for extension signatures, or nullopt for an unknown name.
    /// Complexity: O(name bytes), expected hash lookup.
    /// Example: auto point = lookup.find_type("Point").
    std::optional<value_type> find_type(std::string_view name) const;
    /// Creates only core operators, casts, and the persistent full-range constructor.
    /// Standard-library functions are separate client registrations.
    /// Complexity: O(retained signature count) metadata and callback storage.
    op_lookup();
    /// Disallows copying this registry.
    op_lookup(const op_lookup&) = delete;
    /// Disallows copy assignment of this registry.
    op_lookup& operator=(const op_lookup&) = delete;
    /// Transfers owned registration and scope state.
    op_lookup(op_lookup&&) noexcept = default;
    /// Transfers state and releases old registrations.
    /// Complexity: O(old owned state).
    op_lookup& operator=(op_lookup&&) noexcept = default;
    // [knowledge] Keep registries noncopyable: shared mutable callback captures are not
    // independent value copies, even when their std::function wrappers can be copied.
    /// Registers an opaque trivially-copyable leaf under an owned nonempty name.
    /// Throws std::invalid_argument for empty/duplicate/reserved names or a structural adapter.
    /// Complexity: O(name bytes), expected map insertion.
    /// Example: lookup.register_type<my_trivial_type>("Point").
    template <class T>
    void register_type(std::string_view name) {
        if constexpr (!detail::native_leaf<T>) {
            throw std::invalid_argument("named leaf registration requires an opaque leaf");
        } else {
            auto type = value_type::leaf<T>();
            if (!type.compatible_with(native_type<T>()))
                throw std::invalid_argument("named leaf registration requires an opaque leaf");
            insert_type(name, std::move(type));
        }
    }
    /// Registers an owned complete nominal record schema, never a guessed native layout.
    /// Throws invalid_argument for empty/duplicate/reserved names or invalid field names.
    /// Complexity: O(fields + field-name bytes + name bytes), expected map insertion.
    /// Example: lookup.register_record("Flag", std::array{record_field{"on",
    /// value_type::leaf<bool>()}}).
    void register_record(std::string_view name, std::span<const record_field> fields);
    /// Registers one exact complete operation; a return type never distinguishes overloads.
    /// Copies the name and input descriptors and owns result and emit.
    /// emit follows operation_emitter's ownership and emission contract.
    /// Throws std::invalid_argument for empty names, absent emitters, or duplicate inputs.
    /// Compilation checks the emitted stack depth and result compatibility after selection.
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
    /// Registers an owned const-invocable callback with exactly the declared result type.
    /// Arguments must satisfy the runtime's declared A... invocation constraints.
    /// Concrete vectors/tuples use trusted public adapters; opaque leaves are trivially copyable.
    /// Use std::tuple<> for source unit; void is not a source value.
    /// Throws std::invalid_argument for an empty name or duplicate operands, even if R differs.
    /// Callback references stay client-owned; emitted code retains the stored callback.
    /// Lvalue callbacks are copied; rvalue callbacks are moved into owned decayed storage.
    /// Complexity: O(overloads * arity * distinct compared plans + name/capture storage +
    /// first-use complete structural descriptor construction).
    /// Example: lookup.register_op<double, double>("negate", [](double x) { return -x; }).
    template <class R, class... A, class F>
    requires(!std::is_void_v<R> && requires(dyn_segment & b, F&& callback) {
        b.template op<R, A...>(std::forward<F>(callback));
    }) void register_op(std::string_view name, F&& callback) {
        const std::array<value_type, sizeof...(A)> inputs{native_type<A>()...};
        auto owned = std::make_shared<const std::decay_t<F>>(std::forward<F>(callback));
        register_operation(name, inputs, native_type<R>(),
                           [owned = std::move(owned)](dyn_segment& builder, source_span) {
                               builder.template op<R, A...>([owned](A&&... arguments) -> R {
                                   return std::invoke(*owned, std::move(arguments)...);
                               });
                           });
    }
    /// Binds an ordinary name to its first owned trivially-copyable marker.
    /// Repeated installers may use another marker for the same name; the first remains selected.
    /// Throws std::invalid_argument for an empty name, a structural marker, or a marker type
    /// already bound to a distinct function.
    /// Register matching register_op overloads separately; a marker alone supplies no operation.
    /// Failure leaves existing bindings and marker reservations unchanged.
    /// Complexity: O(name bytes + marker size), expected hash insertion.
    /// Example: lookup.register_function("convert", my_marker{}).
    template <class M>
    void register_function(std::string_view name, M&& marker) {
        using marker_type = std::decay_t<M>;
        if constexpr (!detail::native_leaf<marker_type>) {
            throw std::invalid_argument("function markers require an opaque leaf");
        } else {
            const auto type = value_type::leaf<marker_type>();
            auto owned = value::from<marker_type>(std::forward<M>(marker));
            if (!type.compatible_with(owned.type()))
                throw std::invalid_argument("function markers require an opaque leaf");
            insert_function(name, type,
                            [owned = std::move(owned)](dyn_segment& builder, source_span) {
                                builder.just(owned);
                            });
        }
    }
    /// Owns a persistent LIFO library scope visible in independent closure bodies.
    /// Precondition: no transient scopes are active.
    /// Precondition: callback follows scope_callback's emission and capture-lifetime contract.
    /// Throws invalid_argument for an empty callback.
    /// Complexity: amortized O(1), plus callback ownership.
    /// Example: lookup.push_library_scope(library_callback).
    void push_library_scope(scope_callback callback);
};

/// @}
} // namespace cel
#endif
