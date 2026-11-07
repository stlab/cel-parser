// SPDX-License-Identifier: BSL-1.0
#include <algorithm>
#include <cassert>
#include <cel/parser/lookup.hpp>
#include <cmath>
#include <limits>

namespace cel {
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
/// Rounds binary64 halfway values to even independently of the ambient rounding mode.
/// NaNs, infinities, signed zero, and already integral values retain Rust method behavior.
double round_ties_even(double input) {
    double integral = 0.0;
    const double fraction = std::modf(input, &integral);
    if (std::abs(fraction) != 0.5) return std::round(input);
    if (std::fmod(integral, 2.0) == 0.0) return integral;
    return integral + std::copysign(1.0, fraction);
}
/// Marks the first standard min binding without runtime-owned mutable state.
struct min_marker {};
/// Marks the first standard max binding.
struct max_marker {};
/// Marks the first standard clamp binding.
struct clamp_marker {};
/// Marks the first standard abs binding.
struct abs_marker {};
/// Marks the first standard signum binding.
struct signum_marker {};
/// Marks the first standard sqrt binding.
struct sqrt_marker {};
/// Marks the first standard floor binding.
struct floor_marker {};
/// Marks the first standard ceil binding.
struct ceil_marker {};
/// Marks the first standard trunc binding.
struct trunc_marker {};
/// Marks the first standard fract binding.
struct fract_marker {};
/// Marks the first standard halfway-even binding.
struct round_marker {};
} // namespace

stack_origin operand_origin(const dyn_segment& builder, std::size_t count) {
    const auto origin = builder.origin();
    const auto local_depth = builder.snapshot().depth - origin.depth;
    if (count > local_depth) throw std::invalid_argument("operation operand underflow");
    const auto prefix = builder.operand_types(local_depth).first(local_depth - count);
    std::size_t cursor = origin.bytes;
    // Reconstruct only the live canonical prefix from the builder's actual immutable witnesses.
    for (const auto& type : prefix) {
        cursor = align_offset(cursor, type.byte_layout().alignment);
        if (type.byte_layout().size > std::numeric_limits<std::size_t>::max() - cursor)
            throw std::overflow_error("canonical origin overflow");
        cursor += type.byte_layout().size;
    }
    return {cursor, builder.snapshot().depth - count};
}

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
    insert_type(name, value_type::record(name, fields));
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
                    const auto ambient = operand_origin(builder, arity);
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

void install_standard_library(op_lookup& lookup) {
    lookup.register_function("min", min_marker{});
    lookup.register_function("max", max_marker{});
    lookup.register_function("clamp", clamp_marker{});
    lookup.register_function("abs", abs_marker{});
    lookup.register_function("signum", signum_marker{});
    lookup.register_function("sqrt", sqrt_marker{});
    lookup.register_function("floor", floor_marker{});
    lookup.register_function("ceil", ceil_marker{});
    lookup.register_function("trunc", trunc_marker{});
    lookup.register_function("fract", fract_marker{});
    lookup.register_function("round_ties_even", round_marker{});
    lookup.register_op<double, double, double>("min",
                                               [](double a, double b) { return std::fmin(a, b); });
    lookup.register_op<double, double, double>("max",
                                               [](double a, double b) { return std::fmax(a, b); });
    lookup.register_op<double, double, double, double>("clamp", [](double x, double lo, double hi) {
        if (!(lo <= hi)) throw std::invalid_argument("invalid clamp bounds");
        return x < lo ? lo : x > hi ? hi : x;
    });
    lookup.register_op<double, double>("abs", [](double a) { return std::abs(a); });
    lookup.register_op<double, double>(
        "signum", [](double a) { return std::isnan(a) ? a : std::copysign(1.0, a); });
    lookup.register_op<double, double>("sqrt", [](double a) { return std::sqrt(a); });
    lookup.register_op<double, double>("floor", [](double a) { return std::floor(a); });
    lookup.register_op<double, double>("ceil", [](double a) { return std::ceil(a); });
    lookup.register_op<double, double>("trunc", [](double a) { return std::trunc(a); });
    lookup.register_op<double, double>("fract", [](double a) { return a - std::trunc(a); });
    lookup.register_op<double, double>("round_ties_even", round_ties_even);
}
} // namespace cel
