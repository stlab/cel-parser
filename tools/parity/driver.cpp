// SPDX-License-Identifier: BSL-1.0
#include <bit>
#include <cel/parser.hpp>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
/// Identifies each ordinary named function's first marker.
struct tap_marker {};
struct hit_marker {};     ///< Identifies a traced boolean function.
struct explode_marker {}; ///< Identifies the selected failure function.
struct leaf_marker {};    ///< Identifies a client-leaf function.
struct named_marker {};   ///< Identifies the ordinary named operation.
struct second_marker {};  ///< Exercises rejection of a secondary marker.
/// Stores one admitted trivially copyable client value for cross-language fixtures.
struct client_leaf {
    double number; ///< Stores one independently copied trivial client value.
};
/// Identifies a deliberate selected callback failure.
struct callback_error : std::runtime_error {
    /// Constructs the distinct client-owned selected-failure error.
    callback_error() : std::runtime_error("selected callback") {}
};
/// Produces exact binary64 bits, normalizing only unspecified NaN payloads.
std::string bits(double number) {
    if (std::isnan(number)) return "nan";
    std::ostringstream result;
    result << std::hex << std::setw(16) << std::setfill('0')
           << std::bit_cast<std::uint64_t>(number);
    return result.str();
}
/// Serializes complete public semantic identities, excluding native carrier layout.
/// Complexity: O(type nodes + name bytes).
std::string type(const cel::value_type& shape) {
    switch (shape.kind()) {
        case cel::type_kind::leaf:
            if (shape.leaf_identity() == typeid(double)) return "D";
            if (shape.leaf_identity() == typeid(bool)) return "B";
            if (shape.leaf_identity() == typeid(client_leaf)) return "Client";
            if (shape.leaf_identity() == typeid(named_marker)) return "Marker:named";
            if (shape.leaf_identity() == typeid(second_marker)) return "Marker:secondary";
            throw std::logic_error("fixture uses an unregistered leaf identity");
        case cel::type_kind::array:
            return "A(" + type(shape.element()) + ")";
        case cel::type_kind::callable: {
            std::string result = "F(";
            for (const auto& parameter : shape.parameters())
                result += type(parameter) + ";";
            return result + "->" + type(shape.result()) + ")";
        }
        case cel::type_kind::range: {
            constexpr std::array names{"Range",          "RangeFrom", "RangeTo", "RangeToInclusive",
                                       "RangeInclusive", "RangeFull"};
            return std::string(names.at(static_cast<std::size_t>(shape.form()))) +
                   (shape.form() == cel::range_kind::range_full ?
                        "()" :
                        "(" + type(shape.element()) + ")");
        }
        case cel::type_kind::record:
        case cel::type_kind::tuple: {
            std::string result = shape.kind() == cel::type_kind::record ?
                                     "R:" + std::string(shape.record_identity()) + "(" :
                                     "T(";
            for (std::size_t index = 0; index != shape.fields().size(); ++index) {
                if (shape.kind() == cel::type_kind::record)
                    result += shape.field_names()[index] + ":";
                result += type(shape.fields()[index]) + ";";
            }
            return result + ")";
        }
    }
    throw std::logic_error("unknown fixture type");
}
/// Serializes public values structurally, never inspecting carrier bytes.
/// Complexity: O(values + public child copy work).
std::string value(const cel::value& object) {
    if (object.type().kind() == cel::type_kind::leaf) {
        if (object.type().leaf_identity() == typeid(double)) return bits(object.as<double>());
        if (object.type().leaf_identity() == typeid(bool))
            return object.as<bool>() ? "true" : "false";
        if (object.type().leaf_identity() == typeid(named_marker)) {
            (void)object.as<named_marker>();
            return "marker";
        }
        if (object.type().leaf_identity() == typeid(second_marker)) {
            (void)object.as<second_marker>();
            return "marker";
        }
        return bits(object.as<client_leaf>().number);
    }
    if (object.type().kind() == cel::type_kind::callable) return "fn";
    std::string result = "[";
    for (std::size_t index = 0; index != object.size(); ++index)
        result += value(object.at(index)) + ";";
    return result + "]";
}
/// Installs public callbacks, complete nominal schemas, and independent scope witnesses.
/// Complexity: O(registration and metadata work).
cel::op_lookup lookup(std::string_view setup, std::vector<double>& trace) {
    cel::op_lookup result;
    if (setup != "default") cel::install_standard_library(result);
    result.register_type<client_leaf>("Client");
    result.register_type<double>("Alias");
    result.register_record("Pair",
                           std::array{cel::record_field{"x", cel::value_type::leaf<double>()},
                                      cel::record_field{"y", cel::value_type::leaf<double>()}});
    result.register_record("Empty", {});
    result.register_record(
        "Box", std::array{cel::record_field{
                   "f", cel::value_type::callable({}, cel::value_type::leaf<double>())}});
    result.register_function("tap", tap_marker{});
    result.register_op<double, double>("tap", [&trace](double n) {
        trace.push_back(n);
        return n;
    });
    result.register_function("hit", hit_marker{});
    result.register_op<bool, double>("hit", [&trace](double n) {
        trace.push_back(n);
        return n > 0;
    });
    result.register_function("explode", explode_marker{});
    result.register_op<bool>("explode", []() -> bool { throw callback_error{}; });
    result.register_function("leaf", leaf_marker{});
    result.register_op<client_leaf>("leaf", [] { return client_leaf{8}; });
    result.register_function("named", named_marker{});
    result.register_function("named", second_marker{});
    result.register_op<double, double>("named", [](double n) { return n * 10; });
    result.register_op<double, double, double>("plus", [](double a, double b) { return a + b; });
    result.push_library_scope(
        [](std::string_view name, cel::dyn_segment& b, std::size_t n, cel::source_span) {
            if (n != 0 || (name != "library" && name != "shadow")) return false;
            b.just(cel::value::from(name == "library" ? 7.0 : 1.0));
            return true;
        });
    for (const auto number : {4.0, 5.0})
        result.push_scope(
            [number](std::string_view name, cel::dyn_segment& b, std::size_t n, cel::source_span) {
                if (n != 0) return false;
                if (name == "othermarker") {
                    b.just(cel::value::from(second_marker{}));
                    return true;
                }
                if (name != "ambient" && name != "shadow") return false;
                b.just(cel::value::from(name == "ambient" ? 3.0 : number));
                return true;
            });
    if (setup == "custom") {
        result.register_op<double, double, double>("|", [](double a, double b) { return a + b; });
        result.register_op<double, double, double>("^", [](double a, double b) { return a - b; });
        result.register_op<double, double, double>("&", [](double a, double b) { return a * b; });
        result.register_op<double, double, double>("<<", [](double a, double b) { return a + b; });
        result.register_op<double, double, double>(">>", [](double a, double b) { return a - b; });
        result.register_op<double, double, double>("()", [](double a, double b) { return a + b; });
        const auto pair = result.find_type("Pair");
        if (!pair) throw std::logic_error("Pair fixture missing");
        result.register_operation(
            "x", std::array{*pair}, cel::value_type::leaf<double>(),
            [](cel::dyn_segment& b, cel::source_span) { b.project_record("y"); });
    }
    return result;
}
/// Borrows one decoded expression and its explicit registration selection.
struct request {
    std::string_view setup;  ///< Selects explicitly configured public registrations.
    std::string_view source; ///< Borrows one decoded expression for compilation.
};
/// Executes one configured request after destroying its compiler.
/// Complexity: O(compilation + three executions + serialization).
void run(request input) {
    const auto [setup, source] = input;
    std::vector<double> trace;
    try {
        const auto program = [&] {
            cel::parser compiler(lookup(setup, trace));
            return compiler.parse_str(source);
        }();
        if (!trace.empty()) throw std::logic_error("callback executed during compilation");
        cel::raw_stack stack;
        stack.push(true);
        const auto needed = program.requirements().for_prefix(stack.cursor(), stack.depth());
        stack.reserve(needed.bytes.size, needed.bytes.alignment);
        stack.reserve_depth(needed.slots);
        std::string response;
        try {
            for (int repeat = 0; repeat != 3; ++repeat) {
                trace.clear();
                const auto object = program.execute(stack, {});
                const auto next = "ok\t" + type(object.type()) + "\t" + value(object);
                if (repeat && response != next) throw std::logic_error("cached result changed");
                response = next;
                if (stack.depth() != 1) throw std::logic_error("caller prefix lost");
            }
        } catch (const callback_error&) {
            response = "runtime\tcallback";
        } catch (const std::invalid_argument&) {
            // Only explicitly selected clamp fixtures admit this public runtime exception.
            if (setup != "clamp_error") throw;
            response = "runtime\tclamp";
        } catch (const std::out_of_range&) {
            if (setup != "index_error") throw;
            response = "runtime\tindex";
        }
        if (stack.depth() != 1 || !stack.pop<bool>())
            throw std::logic_error("caller prefix cleanup failed");
        std::cout << response << '\t';
        for (std::size_t i = 0; i != trace.size(); ++i)
            std::cout << (i ? "," : "") << bits(trace[i]);
        std::cout << '\n';
    } catch (const cel::parse_error& error) {
        constexpr std::array names{"malformed_input", "unsupported_literal", "type_error",
                                   "lookup_error",    "delimiter_error",     "resource_limit"};
        std::cout << "parse\t" << error.span().begin << '\t' << error.span().end << '\t'
                  << names.at(static_cast<std::size_t>(error.category())) << '\n';
    }
}
} // namespace

/// Reads a tab-delimited test protocol; transport decoding stays outside both product libraries.
int main() try {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) throw std::invalid_argument("fixture request missing setup");
        run({std::string_view(line).substr(0, tab), std::string_view(line).substr(tab + 1)});
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
