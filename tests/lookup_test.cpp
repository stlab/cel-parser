// SPDX-License-Identifier: BSL-1.0
#include <cel/parser/compiler.hpp>
#include <cel/parser/lookup.hpp>
#include <cfenv>
#include <cmath>
#include <limits>
#include <memory>
#include <tuple>
#include <vector>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <ostream>

namespace {
/// Supplies one adapter-backed nominal record for parser registration tests.
struct adapted_point {
    double x;
    double y;
};
/// Supplies a second adapter for the same nominal identity.
struct adapted_point_duplicate_name {
    double x;
    double y;
};
/// Supplies a record whose runtime field name is not a CEL identifier.
struct adapted_unspellable_field {
    double value;
};
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
struct record_adapter<::adapted_point_duplicate_name> {
    /// Returns the same nominal identity as adapted_point.
    static value_type type() {
        static const auto descriptor = value_type::record(
            "sample.point", std::array{record_field{"x", value_type::leaf<double>()},
                                       record_field{"y", value_type::leaf<double>()}});
        return descriptor;
    }
    /// Encodes one duplicate-name value in canonical field order.
    static value to_value(const ::adapted_point_duplicate_name& source) {
        return value::record(type(), std::array{value::from(source.x), value::from(source.y)});
    }
    /// Decodes one duplicate-name value after descriptor validation.
    static ::adapted_point_duplicate_name from_value(const value& source) {
        if (!source.type().same_shape(type()))
            throw std::invalid_argument("wrong adapted point duplicate type");
        return {.x = source.field("x").as<double>(), .y = source.field("y").as<double>()};
    }
};

template <>
struct record_adapter<::adapted_unspellable_field> {
    /// Returns a descriptor containing an unspellable field name.
    static value_type type() {
        static const auto descriptor = value_type::record(
            "Unspellable", std::array{record_field{"first-name", value_type::leaf<double>()}});
        return descriptor;
    }
    /// Encodes the value as a canonical record.
    static value to_value(const ::adapted_unspellable_field& source) {
        return value::record(type(), std::array{value::from(source.value)});
    }
    /// Decodes the value after checking its descriptor.
    static ::adapted_unspellable_field from_value(const value& source) {
        if (!source.type().same_shape(type()))
            throw std::invalid_argument("wrong unspellable record type");
        return {source.field("first-name").as<double>()};
    }
};
} // namespace cel

TEST_CASE("record adapter registration rejects duplicate CEL source names") {
    cel::op_lookup lookup;
    lookup.register_record_type<adapted_point>("AdapterPoint");
    CHECK_THROWS_AS(lookup.register_record_type<adapted_point_duplicate_name>("AdapterPoint"),
                    std::invalid_argument);
}

TEST_CASE("record adapters separate CEL source names from nominal identities") {
    cel::op_lookup lookup;
    lookup.register_record_type<adapted_point>("Point");
    lookup.register_record_type<adapted_point>("OtherPoint");
    const auto type = lookup.find_type("Point");
    if (!type) throw std::logic_error("registered Point missing");
    CHECK(type->record_identity() == "sample.point");
    lookup.register_op<double, adapted_point>(
        "sum", [](adapted_point point) { return point.x + point.y; });
    cel::parser parser(std::move(lookup));
    cel::raw_stack stack;
    CHECK(parser.parse_str("Point {y: 2.0, x: 1.0}.sum()").execute(stack, {}).as<double>() == 3.0);
    CHECK(parser.parse_str("OtherPoint {x: 4.0, y: 5.0}.sum()").execute(stack, {}).as<double>() ==
          9.0);
    CHECK(parser.parse_str("(|point: Point| point.x)(Point {x: 4.0, y: 5.0})")
              .execute(stack, {})
              .as<double>() == 4.0);
}

TEST_CASE("record registration rejects source names that cannot construct records") {
    for (const std::string_view name :
         {"sample.point", "first-name", "true", "if", "else", "as", "f64", "name "}) {
        CAPTURE(name);
        cel::op_lookup lookup;
        const auto original = lookup.find_type(name);
        CHECK_THROWS_AS(lookup.register_record_type<adapted_point>(name), std::invalid_argument);
        const bool first_unchanged = lookup.find_type(name).has_value() == original.has_value();
        CHECK(first_unchanged);
        CHECK_THROWS_AS(lookup.register_record(name, std::array{cel::record_field{
                                                         "x", cel::value_type::leaf<double>()}}),
                        std::invalid_argument);
        const auto after = lookup.find_type(name);
        const bool second_unchanged = after.has_value() == original.has_value();
        CHECK(second_unchanged);
        if (original && after) CHECK(after->same_shape(*original));
    }

    cel::op_lookup lookup;
    lookup.register_record_type<adapted_point>("r#if");
    cel::parser parser(std::move(lookup));
    cel::raw_stack stack;
    CHECK(parser.parse_str("r#if {x: 2.0, y: 3.0}.x").execute(stack, {}).as<double>() == 2.0);
}

TEST_CASE("record registration rejects field names that CEL cannot spell") {
    for (const std::string_view name :
         {"first-name", "first name", "true", "1name", "name ", "r#_", "\xFF"}) {
        CAPTURE(name);
        cel::op_lookup lookup;
        const auto fields =
            std::array{cel::record_field{std::string(name), cel::value_type::leaf<double>()}};
        CHECK_THROWS_AS(lookup.register_record("Bad", fields), std::invalid_argument);
        CHECK_FALSE(lookup.find_type("Bad").has_value());
    }

    cel::op_lookup lookup;
    CHECK_THROWS_AS(lookup.register_record_type<adapted_unspellable_field>("Unspellable"),
                    std::invalid_argument);
    CHECK_FALSE(lookup.find_type("Unspellable").has_value());
}

TEST_CASE("record registration rejects unspellable nested fields") {
    const auto nested = cel::value_type::record(
        "Nested", std::array{cel::record_field{"first-name", cel::value_type::leaf<double>()}});
    for (const auto& field_type : std::array{
             nested, cel::value_type::array(nested), cel::value_type::tuple(std::array{nested}),
             cel::value_type::callable({}, nested),
             cel::value_type::callable(std::array{nested}, cel::value_type::leaf<double>())}) {
        cel::op_lookup lookup;
        CHECK_THROWS_AS(
            lookup.register_record("Outer", std::array{cel::record_field{"nested", field_type}}),
            std::invalid_argument);
        CHECK_FALSE(lookup.find_type("Outer").has_value());
    }
}

TEST_CASE("record registration accepts spellable field identifiers") {
    cel::op_lookup lookup;
    const auto number = cel::value_type::leaf<double>();
    lookup.register_record("Valid", std::array{cel::record_field{"if", number},
                                               cel::record_field{"r#true", number},
                                               cel::record_field{"\xce\xb1", number}});
    cel::parser parser(std::move(lookup));
    cel::raw_stack stack;
    CHECK(parser.parse_str("Valid {if: 1.0, r#true: 2.0, \xce\xb1: 3.0}.r#true")
              .execute(stack, {})
              .as<double>() == 2.0);
}
