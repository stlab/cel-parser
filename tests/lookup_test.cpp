// SPDX-License-Identifier: BSL-1.0
#include "lookup_access.hpp"
#include <cel/parser/lookup.hpp>
#include <cfenv>
#include <cmath>
#include <limits>
#include <memory>
#include <tuple>
#include <vector>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

namespace {
/// Distinguishes equal-layout client operands by nominal identity.
struct first {
    double number;
};
/// Distinguishes a second client operand with the same layout.
struct second {
    double number;
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
