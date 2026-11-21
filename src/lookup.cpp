// SPDX-License-Identifier: BSL-1.0
#include "lexer.hpp"
#include "lookup_access.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cpp-library/testing.hpp>
#include <unordered_set>

namespace cel {
using parser_detail::lookup_selector;
using parser_detail::scope_selection;
namespace {
/// Reports whether a name belongs to the reserved language type inventory.
/// Complexity: O(name bytes) for the fixed language name inventory.
bool reserved_type(std::string_view name) {
    constexpr std::array names{"bool",
                               "f64",
                               "f32",
                               "u8",
                               "u16",
                               "u32",
                               "u64",
                               "u128",
                               "usize",
                               "i8",
                               "i16",
                               "i32",
                               "i64",
                               "i128",
                               "isize",
                               "String",
                               "Fn",
                               "Range",
                               "RangeInclusive",
                               "RangeFrom",
                               "RangeTo",
                               "RangeToInclusive",
                               "RangeFull"};
    return std::ranges::find(names, name) != names.end();
}

/// Checks whether the complete spelling is a CEL identifier.
/// Complexity: O(name bytes) time and auxiliary space.
bool source_identifier(std::string_view name) {
    parser_detail::lex_lexer lexer(name);
    const auto* identifier = std::get_if<parser_detail::identifier>(&lexer.current().value);
    return identifier && identifier->spelling == name;
}

/// Rejects field names that cannot be written in CEL throughout a descriptor.
/// Complexity: expected O(V + E + S) time and O(V + E + L) auxiliary space, where V
/// and E are visited descriptor references and edges, S is field-name bytes, and L
/// is the longest field name.
void validate_record_fields(const value_type& type) {
    std::vector<const value_type*> pending{&type};
    std::unordered_set<const value_type*> visited;
    while (!pending.empty()) {
        const auto* current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) continue;
        switch (current->kind()) {
            case type_kind::leaf:
                break;
            case type_kind::array:
            case type_kind::range:
                pending.push_back(&current->element());
                break;
            case type_kind::record:
                for (const auto& name : current->field_names())
                    if (!source_identifier(name))
                        throw std::invalid_argument("record field is not a CEL identifier: " +
                                                    name);
                [[fallthrough]];
            case type_kind::tuple:
                for (const auto& field : current->fields())
                    pending.push_back(&field);
                break;
            case type_kind::callable:
                for (const auto& parameter : current->parameters())
                    pending.push_back(&parameter);
                pending.push_back(&current->result());
                break;
        }
    }
}
} // namespace

void op_lookup::insert(names<std::vector<operation>>& destination,
                       std::string_view name,
                       std::span<const value_type> inputs,
                       value_type result,
                       operation_emitter emit) {
    if (name.empty() || !emit) throw std::invalid_argument("operation needs a name and emitter");
    const auto existing = destination.find(name);
    if (existing != destination.end()) {
        // Return types do not create a disjoint input overload.
        for (const auto& candidate : existing->second) {
            if (candidate.inputs.size() == inputs.size() &&
                std::ranges::equal(
                    candidate.inputs, inputs,
                    [](const value_type& a, const value_type& b) { return a.compatible_with(b); }))
                throw std::invalid_argument("duplicate operation signature");
        }
    }
    operation entry{{inputs.begin(), inputs.end()}, std::move(result), std::move(emit)};
    if (existing == destination.end()) {
        std::vector<operation> overloads;
        overloads.push_back(std::move(entry));
        destination.emplace(std::string(name), std::move(overloads));
    } else {
        existing->second.push_back(std::move(entry));
    }
}

bool op_lookup::emit_match(const names<std::vector<operation>>& registrations,
                           std::string_view name,
                           dyn_segment& builder,
                           std::size_t arity,
                           source_span span) {
    const auto named = registrations.find(name);
    if (named == registrations.end()) return false;
    const auto operands = builder.operand_types(arity);
    if (operands.size() != arity) return false;
    // Inspect every witness before invoking the selected emitter; no speculative emission occurs.
    for (const auto& candidate : named->second) {
        if (candidate.inputs.size() != arity ||
            !std::ranges::equal(
                candidate.inputs, operands,
                [](const value_type& a, const value_type& b) { return a.compatible_with(b); }))
            continue;
        const auto depth = builder.snapshot().depth;
        candidate.emit(builder, span);
        const auto output = builder.operand_types(1);
        if (builder.snapshot().depth != depth - arity + 1 || output.size() != 1 ||
            !candidate.result.compatible_with(output[0]))
            throw std::invalid_argument("registered emitter violates its result contract");
        return true;
    }
    return false;
}

void op_lookup::insert_type(std::string_view name, value_type type) {
    if (name.empty() || reserved_type(name) || types_.contains(name))
        throw std::invalid_argument("duplicate or reserved type name");
    types_.emplace(std::string(name), std::move(type));
}

void op_lookup::insert_record_type(std::string_view name, value_type type) {
    if (!source_identifier(name) || name == "if" || name == "else" || name == "as")
        throw std::invalid_argument("record source name is not a CEL constructor identifier: " +
                                    std::string(name));
    validate_record_fields(type);
    insert_type(name, std::move(type));
}

op_lookup::op_lookup() {
    types_.emplace("bool", value_type::leaf<bool>());
    types_.emplace("f64", value_type::leaf<double>());
    push_library_scope(
        [](std::string_view name, dyn_segment& builder, std::size_t arity, source_span) {
            if (name != "range_full" || arity != 0) return false;
            builder.make_range(range_kind::range_full);
            return true;
        });
    install_builtins();
}

void op_lookup::register_record(std::string_view name, std::span<const record_field> fields) {
    insert_record_type(name, value_type::record(name, fields));
}

std::optional<value_type> op_lookup::find_type(std::string_view name) const {
    const auto found = types_.find(name);
    if (found == types_.end()) return std::nullopt;
    return found->second;
}

void op_lookup::register_operation(std::string_view name,
                                   std::span<const value_type> inputs,
                                   value_type result,
                                   operation_emitter emit) {
    insert(operations_, name, inputs, std::move(result), std::move(emit));
}

void op_lookup::register_structural_op(std::string_view name,
                                       std::span<const value_type> inputs,
                                       value_type result,
                                       operation_emitter emit) {
    insert(structural_, name, inputs, std::move(result), std::move(emit));
}

void op_lookup::insert_function(std::string_view name, value_type marker, operation_emitter emit) {
    if (name.empty()) throw std::invalid_argument("empty function name");
    const auto previous = marker_names_.find(marker.leaf_identity());
    if (previous != marker_names_.end() && previous->second != name)
        throw std::invalid_argument("function marker already names another function");
    const auto [reservation, inserted] = marker_names_.try_emplace(marker.leaf_identity(), name);
    try {
        if (!functions_.contains(name))
            functions_.emplace(std::string(name),
                               function_binding{std::move(marker), std::move(emit)});
    } catch (...) {
        if (inserted) marker_names_.erase(reservation);
        throw;
    }
}

void op_lookup::push_scope(scope_callback callback) {
    if (!callback) throw std::invalid_argument("empty scope callback");
    scopes_.push_back(std::move(callback));
}
void op_lookup::pop_scope() {
    assert(!scopes_.empty());
    scopes_.pop_back();
}
void op_lookup::push_library_scope(scope_callback callback) {
    assert(scopes_.empty());
    if (!callback) throw std::invalid_argument("empty scope callback");
    libraries_.push_back(std::move(callback));
}
std::vector<scope_callback> op_lookup::isolate_scopes() {
    auto saved = std::move(scopes_);
    scopes_.clear();
    return saved;
}
void op_lookup::restore_scopes(std::vector<scope_callback> saved) { scopes_ = std::move(saved); }

bool op_lookup::scope_match(std::string_view name,
                            dyn_segment& builder,
                            std::size_t arity,
                            source_span span,
                            scope_selection selection) const {
    if (selection == scope_selection::all) {
        // Transient scopes are newer than every persistent library scope.
        for (auto i = scopes_.rbegin(); i != scopes_.rend(); ++i)
            if ((*i)(name, builder, arity, span)) return true;
    }
    // Persistent scopes retain their own LIFO order under isolation.
    for (auto i = libraries_.rbegin(); i != libraries_.rend(); ++i)
        if ((*i)(name, builder, arity, span)) return true;
    return false;
}
bool op_lookup::direct_match(std::string_view name,
                             dyn_segment& builder,
                             std::size_t arity,
                             source_span span,
                             scope_selection selection) const {
    return (arity > 0 && scope_match(name, builder, arity, span, selection)) ||
           emit_match(operations_, name, builder, arity, span);
}

void op_lookup::apply(std::string_view name,
                      dyn_segment& builder,
                      std::size_t arity,
                      source_span span,
                      lookup_selector selector,
                      scope_selection selection) const {
    if (selector == lookup_selector::ordinary) {
        if (scope_match(name, builder, arity, span, selection)) return;
        const auto function = functions_.find(name);
        if (arity == 0 && function != functions_.end()) {
            function->second.emit(builder, span);
            return;
        }
        if (name == "()" && arity > 0) {
            const auto operands = builder.operand_types(arity);
            if (operands.size() == arity && operands[0].kind() == type_kind::leaf) {
                const auto marker = marker_names_.find(operands[0].leaf_identity());
                if (marker != marker_names_.end() &&
                    functions_.at(marker->second).marker.compatible_with(operands[0])) {
                    const auto ambient = builder.origin_below(arity);
                    if (direct_match(marker->second, builder, arity - 1, span, selection)) {
                        builder.make_tuple(2, ambient);
                        builder.project_tuple(1);
                        return;
                    }
                }
            }
        }
        if (emit_match(operations_, name, builder, arity, span)) return;
    } else if (direct_match(name, builder, arity, span, selection)) {
        return;
    }
    if (emit_match(structural_, name, builder, arity, span) ||
        emit_match(builtins_, name, builder, arity, span))
        return;
    if (selector == lookup_selector::member && arity == 1) {
        const auto operands = builder.operand_types(1);
        if (operands.size() == 1 && operands[0].kind() == type_kind::record &&
            std::ranges::find(operands[0].field_names(), name) != operands[0].field_names().end()) {
            builder.project_record(name);
            return;
        }
    }
    if (selector == lookup_selector::ordinary && name == "()" && arity > 0) {
        const auto operands = builder.operand_types(arity);
        if (operands.size() == arity && operands[0].kind() == type_kind::callable) {
            const auto& parameters = operands[0].parameters();
            if (parameters.size() == arity - 1 &&
                std::ranges::equal(parameters, operands.subspan(1),
                                   [](const value_type& parameter, const value_type& argument) {
                                       return parameter.compatible_with(argument);
                                   })) {
                builder.invoke_closure(arity - 1);
                return;
            }
        }
    }
    throw parse_error(error_category::lookup_error, span,
                      arity == 0 ? "undefined identifier `" + std::string(name) + "`" :
                                   "no matching operation `" + std::string(name) + "`");
}

void op_lookup::apply_cast(std::string_view target, dyn_segment& builder, source_span span) const {
    const auto operands = builder.operand_types(1);
    if (operands.size() == 1) {
        if (target == "f64" && operands[0].compatible_with(value_type::leaf<double>())) {
            builder.op<double, double>([](double input) { return input; });
            return;
        }
        if (target == "bool" && operands[0].compatible_with(value_type::leaf<bool>())) {
            builder.op<bool, bool>([](bool input) { return input; });
            return;
        }
    }
    throw parse_error(error_category::type_error, span,
                      "unsupported cast to `" + std::string(target) + "`");
}

void op_lookup::install_builtins() {
    builtin<double, double, double>("+", [](double a, double b) { return a + b; });
    builtin<double, double, double>("-", [](double a, double b) { return a - b; });
    builtin<double, double>("-", [](double a) { return -a; });
    builtin<double, double, double>("*", [](double a, double b) { return a * b; });
    builtin<double, double, double>("/", [](double a, double b) { return a / b; });
    builtin<double, double, double>("%", [](double a, double b) { return std::fmod(a, b); });
    builtin<bool, bool>("!", [](bool a) { return !a; });
    builtin<bool, bool, bool>("==", [](bool a, bool b) { return a == b; });
    builtin<bool, bool, bool>("!=", [](bool a, bool b) { return a != b; });
    builtin<bool, double, double>("==", [](double a, double b) { return a == b; });
    builtin<bool, double, double>("!=", [](double a, double b) { return a != b; });
    builtin<bool, double, double>("<", [](double a, double b) { return a < b; });
    builtin<bool, double, double>("<=", [](double a, double b) { return a <= b; });
    builtin<bool, double, double>(">", [](double a, double b) { return a > b; });
    builtin<bool, double, double>(">=", [](double a, double b) { return a >= b; });
    constexpr std::array forms{std::pair{"range", range_kind::range},
                               std::pair{"range_inclusive", range_kind::range_inclusive},
                               std::pair{"range_from", range_kind::range_from},
                               std::pair{"range_to", range_kind::range_to},
                               std::pair{"range_to_inclusive", range_kind::range_to_inclusive}};
    // Every retained range constructor uses the general installed canonical range emitter.
    for (auto [name, kind] : forms) {
        const std::size_t arity =
            kind == range_kind::range || kind == range_kind::range_inclusive ? 2 : 1;
        const std::array inputs{value_type::leaf<double>(), value_type::leaf<double>()};
        insert(builtins_, name, std::span(inputs).first(arity),
               value_type::range(kind, value_type::leaf<double>()),
               [kind](dyn_segment& builder, source_span) { builder.make_range(kind); });
    }
}

} // namespace cel

#if CPP_LIBRARY_TESTING()
#include <doctest/doctest.h>
#include <ostream>

#include <cel/parser/compiler.hpp>
#include <cfenv>
#include <cmath>
#include <limits>
#include <memory>
#include <tuple>
#include <vector>

namespace {
/// Distinguishes equal-layout client operands by nominal identity.
struct first {
    double number;
};
/// Distinguishes a second client operand with the same layout.
struct second {
    double number;
};
/// Supplies one adapter-backed nominal record for parser registration tests.
struct adapted_point {
    double x;
    double y;
};
/// Supplies a same-layout second nominal record.
struct adapted_point_alias {
    double x;
    double y;
};
/// Marks an ordinary registered function independently of its operand overloads.
struct named_function {};
/// Marks an independent installer's binding of the same function name.
struct other_marker {};
/// Owns a copyable, nonmovable over-aligned registered callback.
struct alignas(256) copy_only_factor {
    double factor; ///< Owns the multiplication factor.
    /// Initializes the factor.
    explicit copy_only_factor(double input) : factor(input) {}
    /// Copies the factor independently.
    copy_only_factor(const copy_only_factor&) = default;
    /// Disallows move construction.
    copy_only_factor(copy_only_factor&&) = delete;
    /// Multiplies the operand by the owned factor.
    double operator()(double input) const { return factor * input; }
};
/// Marks a function with a valid over-aligned opaque client leaf.
struct alignas(256) aligned_marker {
    double number; ///< Owns observable marker state.
};
/// Evaluates a complete ordinary program through the public prepared boundary.
/// Complexity: O(compiled operations + native conversion + callback execution).
template <class R>
R evaluate(cel::dyn_segment builder) {
    auto program = std::move(builder).finish();
    auto prepared = program.template prepare<R>();
    cel::raw_stack stack;
    return prepared.call(stack);
}
/// Emits source-ordered native operands, applies a name, and evaluates its result.
/// Complexity: O(arity + matching + compiled operations + conversion + callback execution).
template <class R, class... A>
R operation(const cel::op_lookup& lookup, std::string_view name, A... arguments) {
    cel::dyn_segment builder;
    (builder.just(cel::value::from(arguments)), ...);
    cel::parser_detail::lookup_access::apply(lookup, name, builder, sizeof...(A), {10, 14});
    return evaluate<R>(std::move(builder));
}
} // namespace

namespace cel {
template <>
struct record_adapter<::adapted_point> {
    /// Returns the canonical nominal descriptor for adapted_point.
    static value_type type() {
        static const auto descriptor = value_type::record(
            "sample.point", std::array{record_field{"x", value_type::leaf<double>()},
                                       record_field{"y", value_type::leaf<double>()}});
        return descriptor;
    }
    /// Encodes one point value in canonical field order.
    static value to_value(const ::adapted_point& source) {
        return value::record(type(), std::array{value::from(source.x), value::from(source.y)});
    }
    /// Decodes one point value after descriptor validation.
    static ::adapted_point from_value(const value& source) {
        if (!source.type().same_shape(type()))
            throw std::invalid_argument("wrong adapted point type");
        return {.x = source.field("x").as<double>(), .y = source.field("y").as<double>()};
    }
};

template <>
struct record_adapter<::adapted_point_alias> {
    /// Returns the canonical nominal descriptor for adapted_point_alias.
    static value_type type() {
        static const auto descriptor = value_type::record(
            "AdapterPointAlias", std::array{record_field{"x", value_type::leaf<double>()},
                                            record_field{"y", value_type::leaf<double>()}});
        return descriptor;
    }
    /// Encodes one alias value in canonical field order.
    static value to_value(const ::adapted_point_alias& source) {
        return value::record(type(), std::array{value::from(source.x), value::from(source.y)});
    }
    /// Decodes one alias value after descriptor validation.
    static ::adapted_point_alias from_value(const value& source) {
        if (!source.type().same_shape(type()))
            throw std::invalid_argument("wrong adapted point alias type");
        return {.x = source.field("x").as<double>(), .y = source.field("y").as<double>()};
    }
};

} // namespace cel

TEST_CASE("structural registrations reject nontrivial adapters without reserving names") {
    cel::op_lookup lookup;
    CHECK_THROWS_AS(lookup.register_type<std::vector<double>>("array"), std::invalid_argument);
    CHECK_THROWS_AS(lookup.register_type<std::tuple<double>>("tuple"), std::invalid_argument);
    CHECK_THROWS_AS(lookup.register_function("marker", std::vector<double>{}),
                    std::invalid_argument);
    CHECK_THROWS_AS(lookup.register_function("marker", std::tuple{1.0}), std::invalid_argument);
    CHECK_FALSE(lookup.find_type("array"));
    CHECK_FALSE(lookup.find_type("tuple"));
    lookup.register_type<first>("array");
    lookup.register_type<second>("tuple");
    lookup.register_function("marker", named_function{});
    CHECK_NOTHROW(operation<named_function>(lookup, "marker"));
}

TEST_CASE("lookup owns independent copies of over-aligned lvalue callbacks and markers") {
    cel::op_lookup lookup;
    copy_only_factor callback(3.0);
    lookup.register_op<double, double>("scale", callback);
    callback.factor = 99.0;
    CHECK(operation<double>(lookup, "scale", 2.0) == 6.0);
    aligned_marker marker{7.0};
    lookup.register_function("aligned", marker);
    marker.number = 99.0;
    CHECK(operation<aligned_marker>(lookup, "aligned").number == 7.0);
}

TEST_CASE("native extension registrations preserve nested structural descriptors and origins") {
    using nested = std::tuple<std::tuple<>, std::vector<std::tuple<bool, first>>>;
    cel::op_lookup lookup;
    lookup.register_op<nested, nested>("copy_nested", [](nested input) { return input; });
    const nested input{{}, {{true, first{4.0}}}};
    const auto copied = operation<nested>(lookup, "copy_nested", input);
    CHECK(std::get<1>(std::get<1>(copied)[0]).number == 4.0);
    lookup.register_function("measure_named", named_function{});
    lookup.register_op<double, first>("measure_named", [](first input) { return input.number; });
    cel::dyn_segment builder;
    builder.just(cel::value::from(true));
    const auto prefix = builder.snapshot();
    cel::parser_detail::lookup_access::apply(lookup, "measure_named", builder, 0, {});
    builder.just(cel::value::from(first{9.0}));
    cel::parser_detail::lookup_access::apply(lookup, "()", builder, 2, {});
    CHECK(builder.origin_below(1) == prefix);
    builder.make_tuple(2, {});
    const auto result = evaluate<std::tuple<bool, double>>(std::move(builder));
    CHECK(std::get<0>(result));
    CHECK(std::get<1>(result) == 9.0);
}

TEST_CASE("exact registrations preserve overload identity and callback lifetime") {
    cel::dyn_segment compiled;
    std::weak_ptr<double> lifetime;
    {
        cel::op_lookup lookup;
        lookup.register_type<first>("First");
        lookup.register_type<second>("Second");
        auto state = std::make_shared<double>(3.0);
        lifetime = state;
        lookup.register_op<double, first>("measure",
                                          [state](first x) { return x.number + *state; });
        lookup.register_op<double, second>("measure", [](second x) { return x.number * 2.0; });
        CHECK(operation<double>(lookup, "measure", second{4.0}) == 8.0);
        compiled.just(cel::value::from(first{4.0}));
        cel::parser_detail::lookup_access::apply(lookup, "measure", compiled, 1, {1, 8});
        CHECK_THROWS_AS((lookup.register_op<bool, first>("measure", [](first) { return true; })),
                        std::invalid_argument);
        CHECK_THROWS_AS(lookup.register_type<double>("First"), std::invalid_argument);
    }
    CHECK_FALSE(lifetime.expired());
    CHECK(evaluate<double>(std::move(compiled)) == 7.0);
    CHECK(lifetime.expired());
}

TEST_CASE("scopes are LIFO and isolated bodies retain libraries and registrations") {
    cel::op_lookup lookup;
    lookup.register_op<double>("constant", [] { return 1.0; });
    lookup.push_library_scope(
        [](std::string_view n, cel::dyn_segment& b, std::size_t a, cel::source_span) {
            if (n != "library" || a != 0) return false;
            b.just(cel::value::from(2.0));
            return true;
        });
    cel::parser_detail::lookup_access::push_scope(
        lookup, [](std::string_view n, cel::dyn_segment& b, std::size_t a, cel::source_span) {
            if (n != "constant" || a != 0) return false;
            b.just(cel::value::from(3.0));
            return true;
        });
    cel::parser_detail::lookup_access::push_scope(
        lookup, [](std::string_view n, cel::dyn_segment& b, std::size_t a, cel::source_span) {
            if (n != "constant" || a != 0) return false;
            b.just(cel::value::from(4.0));
            return true;
        });
    CHECK(operation<double>(lookup, "constant") == 4.0);
    cel::parser_detail::lookup_access::pop_scope(lookup);
    CHECK(operation<double>(lookup, "constant") == 3.0);
    auto saved = cel::parser_detail::lookup_access::isolate_scopes(lookup);
    CHECK(operation<double>(lookup, "constant") == 1.0);
    CHECK(operation<double>(lookup, "library") == 2.0);
    cel::parser_detail::lookup_access::restore_scopes(lookup, std::move(saved));
    CHECK(operation<double>(lookup, "constant") == 3.0);
}

TEST_CASE("ordinary named markers retain first binding and direct member selectors") {
    cel::op_lookup lookup;
    CHECK_THROWS_AS(lookup.register_function("bad_marker", std::tuple{}), std::invalid_argument);
    CHECK_THROWS_AS(operation<std::tuple<>>(lookup, "bad_marker"), cel::parse_error);
    lookup.register_function("convert", named_function{});
    lookup.register_function("convert", other_marker{});
    CHECK_THROWS_AS(lookup.register_function("elsewhere", named_function{}), std::invalid_argument);
    lookup.register_op<double, double>("convert", [](double x) { return x + 1.0; });
    cel::dyn_segment secondary;
    secondary.just(cel::value::from(other_marker{}));
    secondary.just(cel::value::from(4.0));
    CHECK_THROWS_AS(cel::parser_detail::lookup_access::apply(lookup, "()", secondary, 2, {}),
                    cel::parse_error);
    cel::dyn_segment builder;
    builder.just(cel::value::from(true)); // Preserve a prefix beneath marker and arguments.
    cel::parser_detail::lookup_access::apply(lookup, "convert", builder, 0, {});
    CHECK(builder.operand_types(1)[0].same_shape(cel::value_type::leaf<named_function>()));
    builder.just(cel::value::from(4.0));
    cel::parser_detail::lookup_access::apply(lookup, "()", builder, 2, {});
    builder.make_tuple(2, {});
    CHECK(evaluate<std::tuple<bool, double>>(std::move(builder)) == std::tuple{true, 5.0});
    cel::dyn_segment member;
    member.just(cel::value::from(5.0));
    cel::parser_detail::lookup_access::apply(lookup, "convert", member, 1, {},
                                             cel::parser_detail::lookup_selector::member);
    CHECK(evaluate<double>(std::move(member)) == 6.0);
}

TEST_CASE("declined overloads never execute and selected emitter failures propagate") {
    cel::op_lookup lookup;
    std::size_t invoked = 0;
    lookup.register_op<double, first>("fail", [&invoked](first) {
        ++invoked;
        return 1.0;
    });
    cel::dyn_segment builder;
    builder.just(cel::value::from(second{2.0}));
    CHECK_THROWS_AS(cel::parser_detail::lookup_access::apply(lookup, "fail", builder, 1, {2, 6}),
                    cel::parse_error);
    CHECK(invoked == 0);
    CHECK(evaluate<second>(std::move(builder)).number == 2.0);
    lookup.register_operation(
        "setup_failure", {}, cel::value_type::leaf<double>(),
        [](cel::dyn_segment&, cel::source_span) { throw std::logic_error("selected failure"); });
    CHECK_THROWS_WITH(operation<double>(lookup, "setup_failure"), "selected failure");
    lookup.register_op<double>("throwing",
                               []() -> double { throw std::runtime_error("callback"); });
    CHECK_THROWS_WITH(operation<double>(lookup, "throwing"), "callback");
}

TEST_CASE("record adapter registration preserves nominal types and member calls") {
    cel::op_lookup lookup;
    lookup.register_record_type<adapted_point>("AdapterPoint");
    lookup.register_record_type<adapted_point_alias>("AdapterPointAlias");
    std::size_t adapted_point_calls = 0;
    lookup.register_op<double, adapted_point>("sum", [&adapted_point_calls](adapted_point input) {
        ++adapted_point_calls;
        return input.x + input.y;
    });
    cel::dyn_segment mismatch;
    mismatch.just(cel::value::from(adapted_point_alias{.x = 1.0, .y = 2.0}));
    CHECK_THROWS_AS(cel::parser_detail::lookup_access::apply(lookup, "sum", mismatch, 1, {}),
                    cel::parse_error);
    CHECK(adapted_point_calls == 0);
    CHECK(evaluate<adapted_point_alias>(std::move(mismatch)).x == 1.0);
    lookup.register_op<double, adapted_point_alias>(
        "sum", [](adapted_point_alias input) { return input.x - input.y; });
    cel::parser parser(std::move(lookup));
    cel::raw_stack stack;
    CHECK(
        parser.parse_str("AdapterPoint { y: 2.0, x: 1.0 }.sum()").execute(stack, {}).as<double>() ==
        3.0);
    CHECK(parser.parse_str("AdapterPointAlias { y: 2.0, x: 1.0 }.sum()")
              .execute(stack, {})
              .as<double>() == -1.0);
}

TEST_CASE("nominal records and arbitrary nested registration use public runtime emitters") {
    cel::op_lookup lookup;
    const auto field = cel::record_field{
        "payload", cel::value_type::array(cel::value_type::array(cel::value_type::tuple(std::array{
                       cel::value_type::leaf<bool>(), cel::value_type::leaf<double>()})))};
    lookup.register_record("Record", std::array{field});
    CHECK_THROWS_AS(lookup.register_record("Record", {}), std::invalid_argument);
    const auto found = lookup.find_type("Record");
    if (!found) throw std::logic_error("registered record missing");
    auto schema = *found;
    CHECK(schema.record_identity() == "Record");
    CHECK(schema.fields()[0].same_shape(field.type));
    using nested = std::vector<std::vector<std::tuple<bool, double>>>;
    lookup.register_op<nested, nested>("copy", [](nested input) { return input; });
    nested input{{{true, 3.0}}};
    CHECK(operation<nested>(lookup, "copy", input) == input);
    lookup.register_operation(
        "payload", std::array{schema}, field.type,
        [](cel::dyn_segment& b, cel::source_span) { b.project_record("payload"); });
    cel::dyn_segment builder;
    builder.just(cel::value::record(schema, std::array{cel::value::from(input)}));
    cel::parser_detail::lookup_access::apply(lookup, "payload", builder, 1, {},
                                             cel::parser_detail::lookup_selector::member);
    CHECK(evaluate<nested>(std::move(builder)) == input);
    const auto different_name = cel::value_type::record("OtherRecord", std::array{field});
    const auto alternate_payload = cel::value::from(std::vector<std::vector<bool>>{{true}});
    const auto different_fields = cel::value_type::record(
        "Record", std::array{cel::record_field{"payload", alternate_payload.type()}});
    for (const auto& record : {
             cel::value::record(different_name, std::array{cel::value::from(input)}),
             cel::value::record(different_fields, std::array{alternate_payload}),
         }) {
        cel::dyn_segment wrong;
        wrong.just(record);
        CHECK_THROWS_AS(cel::parser_detail::lookup_access::apply(lookup, "payload", wrong, 1, {}),
                        cel::parse_error);
        const auto operands = wrong.operand_types(1);
        const bool retained = operands.size() == 1 && operands[0].compatible_with(record.type());
        CHECK(retained);
    }
}

TEST_CASE("retained operators use IEEE arithmetic and partial comparisons") {
    cel::op_lookup lookup;
    CHECK(operation<double>(lookup, "+", 2.0, 3.0) == 5.0);
    CHECK(operation<double>(lookup, "-", 2.0, 3.0) == -1.0);
    CHECK(operation<double>(lookup, "*", 2.0, 3.0) == 6.0);
    CHECK(operation<double>(lookup, "/", 8.0, 2.0) == 4.0);
    CHECK(operation<double>(lookup, "%", -5.0, 2.0) == -1.0);
    CHECK(std::signbit(operation<double>(lookup, "-", 0.0)));
    CHECK_THROWS_AS(operation<double>(lookup, "+", 1.0), cel::parse_error);
    CHECK(std::isinf(operation<double>(lookup, "/", 1.0, 0.0)));
    CHECK(std::isnan(operation<double>(lookup, "/", 0.0, 0.0)));
    CHECK(std::isnan(operation<double>(lookup, "%", 1.0, 0.0)));
    CHECK(operation<bool>(lookup, "!", false));
    CHECK(operation<bool>(lookup, "==", true, true));
    CHECK(operation<bool>(lookup, "!=", true, false));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (auto name : {"==", "<", "<=", ">", ">="}) {
        CHECK_FALSE(operation<bool>(lookup, name, nan, 1.0));
    }
    CHECK(operation<bool>(lookup, "!=", nan, nan));
    CHECK(operation<bool>(lookup, "==", -0.0, 0.0));
    CHECK(operation<bool>(lookup, "<", 1.0, 2.0));
    CHECK_FALSE(operation<bool>(lookup, "<", 2.0, 1.0));
    CHECK(operation<bool>(lookup, "<=", 2.0, 2.0));
    CHECK(operation<bool>(lookup, ">", 2.0, 1.0));
    CHECK_FALSE(operation<bool>(lookup, ">", 1.0, 2.0));
    CHECK(operation<bool>(lookup, ">=", 2.0, 2.0));
    for (auto name : {"&", "|", "^", "<<", ">>", "round"}) {
        CHECK_THROWS_AS(operation<double>(lookup, name, 1.0, 2.0), cel::parse_error);
    }
    lookup.register_op<double, double, double>("+", [](double a, double b) { return a - b; });
    CHECK(operation<double>(lookup, "+", 2.0, 3.0) == -1.0);
}

TEST_CASE("standard numeric names are not parser-provided operations") {
    cel::op_lookup lookup;
    for (auto name : {"min", "max", "clamp", "abs", "signum", "sqrt", "floor", "ceil", "trunc",
                      "fract", "round_ties_even"}) {
        CAPTURE(name);
        CHECK_THROWS_AS(operation<double>(lookup, name, -3.0), cel::parse_error);
    }
}

TEST_CASE("owned names schemas and scope captures survive registration input destruction") {
    cel::dyn_segment builder;
    cel::dyn_segment move_only_program;
    std::optional<cel::value_type> schema;
    {
        cel::op_lookup lookup;
        std::string name = "owned";
        lookup.register_op<double>(name,
                                   [state = std::make_unique<double>(8.0)] { return *state; });
        cel::parser_detail::lookup_access::apply(lookup, name, move_only_program, 0, {});
        name.assign("changed");
        CHECK(operation<double>(lookup, "owned") == 8.0);
        std::array fields{cel::record_field{"field", cel::value_type::leaf<double>()}};
        lookup.register_record("Owner", fields);
        fields[0].name.assign("replaced");
        schema = lookup.find_type("Owner");
        auto state = std::make_unique<double>(9.0);
        auto captured = std::shared_ptr<double>(std::move(state));
        cel::parser_detail::lookup_access::push_scope(
            lookup,
            [captured](std::string_view n, cel::dyn_segment& b, std::size_t a, cel::source_span) {
                if (n != "scoped" || a != 0) return false;
                b.op<double>([captured] { return *captured; });
                return true;
            });
        cel::parser_detail::lookup_access::apply(lookup, "scoped", builder, 0, {});
    }
    if (!schema) throw std::logic_error("owned record missing");
    CHECK(schema->field_names()[0] == "field");
    CHECK(evaluate<double>(std::move(move_only_program)) == 8.0);
    CHECK(evaluate<double>(std::move(builder)) == 9.0);
    cel::op_lookup lookup;
    CHECK_THROWS_AS(lookup.register_type<double>("f64"), std::invalid_argument);
    CHECK_THROWS_AS(lookup.register_type<double>("i32"), std::invalid_argument);
    CHECK_THROWS_AS(lookup.register_type<double>(""), std::invalid_argument);
    CHECK_THROWS_AS(lookup.register_type<std::tuple<>>("NotLeaf"), std::invalid_argument);
    const auto field = cel::record_field{"x", cel::value_type::leaf<bool>()};
    CHECK_THROWS_AS(lookup.register_record("Bad", std::array{field, field}), std::invalid_argument);
    CHECK_THROWS_AS(cel::parser_detail::lookup_access::push_scope(lookup, {}),
                    std::invalid_argument);
}

TEST_CASE("structural signatures follow operations and records project complete schemas") {
    cel::op_lookup lookup;
    using input_type = std::tuple<bool, std::vector<std::tuple<double, bool>>>;
    const auto tuple_type = cel::value::from(input_type{true, {{3.0, false}}}).type();
    const auto vector_type = tuple_type.fields()[1];
    lookup.register_structural_op(
        "part", std::array{tuple_type}, vector_type,
        [](cel::dyn_segment& b, cel::source_span) { b.project_tuple(1); });
    CHECK(operation<std::vector<std::tuple<double, bool>>>(lookup, "part",
                                                           input_type{true, {{3.0, false}}}) ==
          std::vector<std::tuple<double, bool>>{{3.0, false}});
    lookup.register_op<std::vector<std::tuple<double, bool>>, input_type>(
        "part", [](input_type) { return std::vector<std::tuple<double, bool>>{{5.0, true}}; });
    CHECK(operation<std::vector<std::tuple<double, bool>>>(lookup, "part", input_type{true, {}}) ==
          std::vector<std::tuple<double, bool>>{{5.0, true}});
    lookup.register_record("Nominal", std::array{cel::record_field{"part", vector_type}});
    const auto found = lookup.find_type("Nominal");
    if (!found) throw std::logic_error("registered record missing");
    const auto schema = *found;
    const auto payload = cel::value::from(std::vector<std::tuple<double, bool>>{{2.0, true}});
    cel::dyn_segment builder;
    builder.just(cel::value::record(schema, std::array{payload}));
    cel::parser_detail::lookup_access::apply(lookup, "part", builder, 1, {},
                                             cel::parser_detail::lookup_selector::member);
    CHECK(evaluate<std::vector<std::tuple<double, bool>>>(std::move(builder)) ==
          std::vector<std::tuple<double, bool>>{{2.0, true}});
    lookup.register_operation(
        "part", std::array{schema}, vector_type, [](cel::dyn_segment& b, cel::source_span) {
            b.project_record("part");
            b.op<std::vector<std::tuple<double, bool>>, std::vector<std::tuple<double, bool>>>(
                [](std::vector<std::tuple<double, bool>>) {
                    return std::vector<std::tuple<double, bool>>{{7.0, false}};
                });
        });
    cel::dyn_segment shadowed;
    shadowed.just(cel::value::record(schema, std::array{payload}));
    cel::parser_detail::lookup_access::apply(lookup, "part", shadowed, 1, {},
                                             cel::parser_detail::lookup_selector::member);
    CHECK(evaluate<std::vector<std::tuple<double, bool>>>(std::move(shadowed)) ==
          std::vector<std::tuple<double, bool>>{{7.0, false}});
    CHECK_THROWS_AS(operation<bool>(lookup, "==", input_type{true, {}}, input_type{true, {}}),
                    cel::parse_error);
}

TEST_CASE("callable dispatch accepts complete nested parameters and client results") {
    using input_type = std::tuple<bool, std::vector<std::vector<double>>>;
    cel::dyn_segment body;
    body.push_argument(0, cel::value::from(input_type{false, {}}).type());
    body.op<first, input_type>([](input_type x) { return first{std::get<1>(x)[0][0]}; });
    cel::callable function(std::move(body).finish(),
                           std::array{cel::value::from(input_type{false, {}}).type()});
    cel::dyn_segment builder;
    {
        cel::op_lookup lookup;
        builder.just(function.to_value());
        builder.just(cel::value::from(input_type{true, {{6.0}}}));
        cel::parser_detail::lookup_access::apply(lookup, "()", builder, 2, {});
    }
    CHECK(evaluate<first>(std::move(builder)).number == 6.0);
    cel::dyn_segment member;
    member.just(function.to_value());
    member.just(cel::value::from(input_type{true, {{6.0}}}));
    cel::op_lookup lookup;
    CHECK_THROWS_AS(cel::parser_detail::lookup_access::apply(
                        lookup, "()", member, 2, {}, cel::parser_detail::lookup_selector::member),
                    cel::parse_error);
    cel::dyn_segment missing_argument;
    missing_argument.just(function.to_value());
    CHECK_THROWS_AS(cel::parser_detail::lookup_access::apply(lookup, "()", missing_argument, 1, {}),
                    cel::parse_error);
    CHECK(missing_argument.operand_types(1)[0].compatible_with(function.type()));
    cel::dyn_segment wrong_argument;
    wrong_argument.just(function.to_value());
    wrong_argument.just(cel::value::from(true));
    try {
        cel::parser_detail::lookup_access::apply(lookup, "()", wrong_argument, 2, {3, 9});
        FAIL("wrong callable argument admitted");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == cel::error_category::lookup_error);
        CHECK(error.span() == cel::source_span{3, 9});
    }
    CHECK(wrong_argument.operand_types(2)[1].compatible_with(cel::value_type::leaf<bool>()));
}

TEST_CASE("range emitters and casts preserve exact retained types and diagnostics") {
    cel::op_lookup lookup;
    cel::raw_stack stack;
    constexpr std::array forms{std::pair{"range", cel::range_kind::range},
                               std::pair{"range_inclusive", cel::range_kind::range_inclusive},
                               std::pair{"range_from", cel::range_kind::range_from},
                               std::pair{"range_to", cel::range_kind::range_to},
                               std::pair{"range_to_inclusive", cel::range_kind::range_to_inclusive},
                               std::pair{"range_full", cel::range_kind::range_full}};
    for (auto [name, kind] : forms) {
        cel::dyn_segment builder;
        const auto arity =
            kind == cel::range_kind::range_full                                        ? 0U :
            kind == cel::range_kind::range || kind == cel::range_kind::range_inclusive ? 2U :
                                                                                         1U;
        if (arity > 0) builder.just(cel::value::from(-0.0));
        if (arity > 1) builder.just(cel::value::from(std::numeric_limits<double>::infinity()));
        cel::parser_detail::lookup_access::apply(lookup, name, builder, arity, {});
        auto value = std::move(builder).finish().execute(stack, {});
        const bool same_form = value.type().form() == kind;
        const bool same_count = value.size() == arity;
        CHECK(same_form);
        CHECK(same_count);
        if (arity > 0) CHECK(std::signbit(value.at(0).as<double>()));
        if (arity > 1) CHECK(std::isinf(value.at(1).as<double>()));
    }
    cel::dyn_segment number;
    number.just(cel::value::from(-0.0));
    cel::parser_detail::lookup_access::apply_cast(lookup, "f64", number, {2, 8});
    CHECK(std::signbit(evaluate<double>(std::move(number))));
    cel::dyn_segment boolean;
    boolean.just(cel::value::from(true));
    cel::parser_detail::lookup_access::apply_cast(lookup, "bool", boolean, {});
    CHECK(evaluate<bool>(std::move(boolean)));
    lookup.register_type<double>("AliasD");
    for (auto target : {"bool", "f32", "i32", "String", "AliasD"}) {
        cel::dyn_segment invalid;
        invalid.just(cel::value::from(3.0));
        try {
            cel::parser_detail::lookup_access::apply_cast(lookup, target, invalid, {4, 11});
            FAIL("unsupported cast accepted");
        } catch (const cel::parse_error& error) {
            CHECK(error.span() == cel::source_span{4, 11});
        }
        CHECK(evaluate<double>(std::move(invalid)) == 3.0);
    }
    cel::dyn_segment empty;
    CHECK_THROWS_AS(cel::parser_detail::lookup_access::apply_cast(lookup, "f64", empty, {}),
                    cel::parse_error);
    CHECK_THROWS_AS(operation<double>(lookup, "range", true, false), cel::parse_error);
}

TEST_CASE("zero-argument functions skip value scopes during direct operation selection") {
    cel::op_lookup lookup;
    lookup.register_function("zero", named_function{});
    lookup.register_op<double>("zero", [] { return 2.0; });
    cel::parser_detail::lookup_access::push_scope(
        lookup,
        [](std::string_view name, cel::dyn_segment& builder, std::size_t arity, cel::source_span) {
            if (name != "zero" || arity != 0) return false;
            builder.just(cel::value::from(7.0));
            return true;
        });
    CHECK(operation<double>(lookup, "zero") == 7.0);
    cel::dyn_segment builder;
    cel::parser_detail::lookup_access::apply(lookup, "zero", builder, 0, {},
                                             cel::parser_detail::lookup_selector::ordinary,
                                             cel::parser_detail::scope_selection::library_only);
    cel::parser_detail::lookup_access::apply(lookup, "()", builder, 1, {});
    CHECK(evaluate<double>(std::move(builder)) == 2.0);
}

TEST_CASE("function marker retirement preserves zero-byte source unit results") {
    cel::op_lookup lookup;
    lookup.register_function("unit", named_function{});
    lookup.register_op<std::tuple<>>("unit", [] { return std::tuple{}; });
    cel::dyn_segment builder;
    cel::parser_detail::lookup_access::apply(lookup, "unit", builder, 0, {});
    cel::parser_detail::lookup_access::apply(lookup, "()", builder, 1, {});
    CHECK(evaluate<std::tuple<>>(std::move(builder)) == std::tuple{});
}

TEST_CASE("registered descriptors distinguish semantic equality from ownership compatibility") {
    cel::dyn_segment body;
    body.just(cel::value::from(3.0));
    cel::callable function(std::move(body).finish(), {});
    auto declared = cel::value_type::callable({}, cel::value_type::leaf<double>());
    CHECK(declared.same_shape(function.type()));
    REQUIRE_FALSE(declared.compatible_with(function.type()));
    cel::op_lookup lookup;
    lookup.register_operation(
        "test", std::array{declared}, cel::value_type::leaf<bool>(),
        [](cel::dyn_segment&, cel::source_span) { FAIL("incompatible ownership selected"); });
    cel::dyn_segment builder;
    builder.just(function.to_value());
    CHECK_THROWS_AS(cel::parser_detail::lookup_access::apply(lookup, "test", builder, 1, {}),
                    cel::parse_error);
}
#endif
