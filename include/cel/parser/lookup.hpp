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

/// \file
/// Provides owned operation registries and type parsing for CEL library clients.
/// Include `<cel/parser/compiler.hpp>` to compile expressions with a registry.

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
    all,         ///< Includes transient scopes before persistent libraries.
    library_only ///< Excludes transient scopes; retains persistent libraries and other tiers.
};
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
/// Register a receiver operation and compile it through the public builder:
/// \code{.cpp}
/// cel::op_lookup lookup;
/// lookup.register_op<double, double>("twice", [](double x) { return x * 2; });
/// cel::dyn_segment builder;
/// builder.just(cel::value::from(3.0));
/// lookup.apply("twice", builder, 1, {0, 9}, cel::lookup_selector::member);
/// auto code = std::move(builder).finish();
/// cel::raw_stack stack;
/// double result = code.prepare<double>().call(stack); // 6.0
/// \endcode
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

public:
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
    /// Returns a retained descriptor for a registered or core scalar name, or nullopt.
    /// Complexity: O(name bytes), expected hash lookup.
    /// Example: lookup.find_type("f64")->kind() == type_kind::leaf.
    std::optional<value_type> find_type(std::string_view name) const;
    /// Registers one exact complete operation; a return type never distinguishes overloads.
    /// Copies the name and input descriptors and owns result and emit.
    /// emit follows operation_emitter's ownership and emission contract.
    /// Throws std::invalid_argument for empty names, absent emitters, or duplicate inputs.
    /// apply() checks the emitted stack depth and result compatibility after selection.
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
    /// Owns a persistent LIFO library scope visible in independent closure bodies.
    /// Precondition: no transient scopes are active.
    /// Precondition: callback follows scope_callback's emission and capture-lifetime contract.
    /// Throws invalid_argument for an empty callback.
    /// Complexity: amortized O(1), plus callback ownership.
    /// Example: lookup.push_library_scope(library_callback).
    void push_library_scope(scope_callback callback);
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
               lookup_selector selector = lookup_selector::ordinary,
               scope_selection selection = scope_selection::all) const;
    /// Emits only retained identity casts: bool to bool and f64 to f64.
    /// Only the target spellings "bool" and "f64" are recognized; registered aliases are not.
    /// Throws parse_error(type_error) with span for an unsupported target/source or underflow.
    /// Complexity: O(target bytes + compared metadata).
    /// Example: lookup.apply_cast("f64", builder, source_span{0, 8}).
    void apply_cast(std::string_view target, dyn_segment& builder, source_span span) const;
};

/// type_expression = identifier [ "(" [ type_expression { "," type_expression } ] ")" ]
/// | "[" type_expression "]" | "(" [ type_expression [ "," [ type_expression
/// { "," type_expression } ] ] ] ")".
/// Parses one current-token type and leaves the following host token unconsumed.
/// Each choice uses only the current token. A consumed comma requires another item except
/// for the required singleton-tuple comma; optional trailing commas are invalid.
/// Fn requires a parameter tuple and a result. Ordinary aggregates admit registered leaves.
/// Callable parameters admit recursively eligible bool/f64 composites, not client leaves
/// or nested callables; results admit any complete type. A callable type does not own code.
/// Returns an owned descriptor that survives the cursor, token source, and lookup.
/// Throws source-located parse_error(malformed_input) for an expected type production,
/// delimiter_error for a missing/mismatched close, type_error for unsupported names or
/// ineligible callable types, and resource_limit beyond 128 active type productions.
/// Producer errors retain their category, message, and span. Allocation/accounting failures
/// propagate. Failure may advance the cursor; parsing never rewinds.
/// Complexity: O(tokens + name bytes + complete descriptor construction), O(nodes + 128) storage.
/// Parse a prefix with an owned type and leave its host separator:
/// \code{.cpp}
/// cel::op_lookup lookup;
/// cel::lex_lexer cursor("(f64,), host");
/// cel::source_span parsed;
/// auto type = cel::parse_type(cursor, lookup, parsed);
/// auto count = type.fields().size(); // 1; parsed is {0, 6}.
/// auto comma = std::get<cel::punctuation>(cursor.current().value).spelling(); // ","
/// \endcode
value_type parse_type(token_cursor& cursor, const op_lookup& lookup);
/// Parses one type directly from the owned text lexer, following the inline grammar contract.
/// Leaves the following host token unconsumed without materializing a separate token array.
/// Throws the source diagnostics and resource errors documented by the token-cursor overload.
/// Complexity: O(lexer traversal + name bytes + complete descriptor construction) time,
/// O(nodes + 128) additional storage, excluding the lexer's owned source and payload.
/// Example: lex_lexer cursor("[f64], next"); auto type = parse_type(cursor, lookup).
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup);
/// Parses one token type prefix and returns its exact consumed byte interval in parsed_span.
/// Follows the two-argument prefix overload's grammar, errors, ownership, and complexity.
/// Postcondition: parsed_span runs from the first consumed token's begin to the last one's end,
/// excluding surrounding whitespace and the following host token.
/// External tokens supply these offsets; parsing does not re-lex their source.
/// Leaves parsed_span unchanged if parsing fails; the cursor may have advanced.
/// Example: source_span span; auto type = parse_type(cursor, lookup, span).
value_type parse_type(token_cursor& cursor, const op_lookup& lookup, source_span& parsed_span);
/// Parses one text type prefix and returns its exact consumed interval without a token copy.
/// Follows the token span-output overload's grammar, errors, ownership, and complexity.
/// Example: lex_lexer c("[f64] , next"); source_span s; auto t = parse_type(c, lookup, s).
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup, source_span& parsed_span);
/// Parses one complete type and rejects trailing input; follows the inline grammar contract.
/// Throws parse_error(malformed_input) for input after the type and before terminal end.
/// Explicit end/error tokens follow token_cursor's contract; later tokens are ignored.
/// Returns an owned descriptor; tokens and lookup need not survive the call.
/// Complexity: O(tokens + name bytes + complete descriptor construction), O(nodes + 128) storage.
/// Example: auto type = parse_type(std::span<const token>(tokens), lookup).
value_type parse_type(std::span<const token> tokens, const op_lookup& lookup);
} // namespace cel
#endif
