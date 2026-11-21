// SPDX-License-Identifier: BSL-1.0
#include "lexer.hpp"
#include "lookup_access.hpp"
#include "type.hpp"
#include <algorithm>
#include <cassert>
#include <cel/parser/compiler.hpp>
#include <charconv>
#include <cpp-library/testing.hpp>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace cel {
using namespace parser_detail;
namespace {
/// Holds exclusive access to a parser until compilation exits, including on exceptions.
class compilation_guard {
    bool& active_;

public:
    /// Rejects recursive parser entry before changing cursor or lookup state.
    explicit compilation_guard(bool& active) : active_(active) {
        if (active_) throw std::logic_error("recursive parser entry");
        active_ = true;
    }
    /// Releases exclusive compilation access.
    ~compilation_guard() { active_ = false; }
};

/// Restores transient lookup scopes after an independent closure body on every exit.
class closure_scopes {
    op_lookup& lookup_;
    std::vector<scope_callback> saved_;

public:
    /// Transfers the enclosing transient scopes out before installing formals.
    explicit closure_scopes(op_lookup& lookup) :
        lookup_(lookup), saved_(lookup_access::isolate_scopes(lookup)) {}
    /// Retires formals and restores the original scope order without allocation.
    /// Complexity: O(retired formal names and metadata).
    ~closure_scopes() { lookup_access::restore_scopes(lookup_, std::move(saved_)); }
};

/// Restores a temporary condition-header restriction on every exit.
class restriction_guard {
    bool& state_;
    bool saved_;

public:
    /// Selects whether unparenthesized record literals can consume a following brace.
    restriction_guard(bool& state, bool selected) : state_(state), saved_(state) {
        state_ = selected;
    }
    /// Restores the enclosing restriction.
    ~restriction_guard() { state_ = saved_; }
};

/// Bounds recursive expression nesting without limiting iterative flat chains.
class nesting_guard {
    std::size_t& depth_;

public:
    /// Reports untrusted excess nesting before another recursive production starts.
    nesting_guard(std::size_t& depth, source_span span) : depth_(depth) {
        if (depth_ == 128)
            throw parse_error(error_category::resource_limit, span,
                              "expression nesting exceeds 128");
        ++depth_;
    }
    /// Restores the enclosing nesting count.
    ~nesting_guard() { --depth_; }
};

/// Converts raw unsigned selector spelling after stripping its lexical underscores.
/// Throws a located diagnostic for invalid digits or size_t overflow, before structural access.
/// Complexity: O(spelling bytes) time and normalized spelling storage.
std::size_t selector_index(std::string_view spelling, int radix, source_span span) {
    std::string digits;
    digits.reserve(spelling.size());
    std::ranges::copy_if(spelling, std::back_inserter(digits), [](char c) { return c != '_'; });
    std::size_t result = 0;
    const auto converted =
        std::from_chars(digits.data(), digits.data() + digits.size(), result, radix);
    if (digits.empty() || converted.ec != std::errc{} ||
        converted.ptr != digits.data() + digits.size())
        throw parse_error(error_category::malformed_input, span,
                          "invalid or unrepresentable tuple index");
    return result;
}

/// Compiles one borrowed current-token stream directly into public checked runtime builders.
/// Construction owns only temporary names/witnesses; frozen code never retains this object.
template <class Cursor>
class expression_compiler {
    Cursor& cursor_;
    op_lookup& lookup_;
    source_span last_{};
    std::size_t depth_ = 0;
    bool no_record_ = false;

    /// Borrows the current token, preserving producer diagnostics without relabeling.
    const token& current() const {
        if (const auto* error = std::get_if<parse_error>(&cursor_.current().value)) throw *error;
        return cursor_.current();
    }
    /// Consumes one current token and records its exact producer span.
    /// Complexity: O(advanced lexer work), or O(1) for external tokens.
    void advance() {
        last_ = current().span;
        cursor_.advance();
    }
    /// Checks a current punctuation token without consuming it.
    bool punctuation_is(std::string_view spelling) const {
        const auto* p = std::get_if<punctuation>(&current().value);
        return p && p->spelling() == spelling;
    }
    /// Checks a current identifier without consuming it.
    /// Complexity: O(compared identifier bytes).
    bool keyword_is(std::string_view spelling) const {
        const auto* name = std::get_if<identifier>(&current().value);
        return name && name->spelling == spelling;
    }
    /// Checks one current opening delimiter.
    bool opening(delimiter kind) const {
        const auto* group = std::get_if<open_delimiter>(&current().value);
        return group && group->kind == kind;
    }
    /// Checks one current closing delimiter.
    bool closing(delimiter kind) const {
        const auto* group = std::get_if<close_delimiter>(&current().value);
        return group && group->kind == kind;
    }
    /// Requires and consumes punctuation, diagnosing the current token on failure.
    void expect_punctuation(std::string_view spelling) {
        if (!punctuation_is(spelling))
            throw parse_error(error_category::malformed_input, current().span,
                              "expected `" + std::string(spelling) + "`");
        advance();
    }
    /// Requires and consumes a visible group boundary of exactly the specified kind.
    void expect_group(delimiter kind, bool open) {
        if (!(open ? opening(kind) : closing(kind)))
            throw parse_error(error_category::delimiter_error, current().span,
                              open ? "expected opening delimiter" : "expected closing delimiter");
        advance();
    }
    /// Owns and consumes an identifier before a streaming lexer retires its token.
    /// Complexity: O(identifier bytes + advanced lexer work).
    std::string expect_name() {
        const auto* name = std::get_if<identifier>(&current().value);
        if (!name)
            throw parse_error(error_category::malformed_input, current().span,
                              "expected identifier");
        auto result = name->spelling;
        advance();
        return result;
    }
    /// Returns the source interval from a production's first token through its last token.
    source_span span_from(source_span start) const { return {start.begin, last_.end}; }
    /// Counts another list operand without unsigned overflow.
    std::size_t increment(std::size_t count) const {
        if (count == std::numeric_limits<std::size_t>::max())
            throw parse_error(error_category::resource_limit, current().span,
                              "operand count overflow");
        return count + 1;
    }
    /// Reports checked structural/type validation failures, never arbitrary client emitter errors.
    /// Complexity: O(emitter work).
    template <class F>
    void structural(source_span span, F emit) {
        try {
            emit();
        } catch (const std::invalid_argument& error) {
            throw parse_error(error_category::type_error, span, error.what());
        } catch (const std::overflow_error& error) {
            throw parse_error(error_category::resource_limit, span, error.what());
        }
    }
    /// Recognizes the FIRST set of an or-expression from the current token alone.
    bool starts_or() const {
        return std::holds_alternative<bool>(current().value) ||
               std::holds_alternative<numeric_literal>(current().value) ||
               std::holds_alternative<identifier>(current().value) ||
               opening(delimiter::parenthesis) || opening(delimiter::bracket) ||
               punctuation_is("-") || punctuation_is("!");
    }
    /// ordinary_expression = expression; explicit contexts permit record literals.
    /// Complexity: O(expression compilation).
    void ordinary(dyn_segment& builder) {
        restriction_guard restriction(no_record_, false);
        expression(builder);
    }
    /// expression = closure_expression | range_expression.
    /// Complexity: O(selected production compilation).
    void expression(dyn_segment& builder) {
        nesting_guard nesting(depth_, current().span);
        if (punctuation_is("||") || punctuation_is("|"))
            closure(builder);
        else
            range(builder);
    }
    /// range_expression = or_expression [ ".." [ or_expression ] | "..=" or_expression ]
    /// | ".." [ or_expression ] | "..=" or_expression.
    /// Complexity: O(endpoint compilation + lookup/emission work).
    void range(dyn_segment& builder) {
        const auto start = current().span;
        if (punctuation_is("..") || punctuation_is("..=")) {
            const bool inclusive = punctuation_is("..=");
            advance();
            if (inclusive || starts_or()) {
                binary(builder, 0);
                lookup_access::apply(lookup_, inclusive ? "range_to_inclusive" : "range_to",
                                     builder, 1, span_from(start));
            } else {
                lookup_access::apply(lookup_, "range_full", builder, 0, span_from(start));
            }
            return;
        }
        binary(builder, 0);
        if (punctuation_is("..") || punctuation_is("..=")) {
            const bool inclusive = punctuation_is("..=");
            advance();
            if (inclusive || starts_or()) {
                binary(builder, 0);
                lookup_access::apply(lookup_, inclusive ? "range_inclusive" : "range", builder, 2,
                                     span_from(start));
            } else {
                lookup_access::apply(lookup_, "range_from", builder, 1, span_from(start));
            }
        }
    }
    /// Matches the current operator at one source-bound binary precedence level.
    bool operator_at(std::size_t level) const {
        switch (level) {
            case 0:
                return punctuation_is("||");
            case 1:
                return punctuation_is("&&");
            case 2:
                return punctuation_is("==") || punctuation_is("!=") || punctuation_is("<") ||
                       punctuation_is(">") || punctuation_is("<=") || punctuation_is(">=");
            case 3:
                return punctuation_is("|");
            case 4:
                return punctuation_is("^");
            case 5:
                return punctuation_is("&");
            case 6:
                return punctuation_is("<<") || punctuation_is(">>");
            case 7:
                return punctuation_is("+") || punctuation_is("-");
            case 8:
                return punctuation_is("*") || punctuation_is("/") || punctuation_is("%");
            default:
                return false;
        }
    }
    /// Compiles iterative left-associated precedence productions, with at most one comparison.
    /// Levels are or, and, comparison, bitwise-or/xor/and/shift, additive, multiplicative.
    /// Lazy levels create dependent RHS code; higher levels use exact ordinary lookup.
    /// Complexity: O(tokens + emission/lookup work), O(fixed precedence depth + code).
    void binary(dyn_segment& builder, std::size_t level) {
        if (level == 9) {
            cast(builder);
            return;
        }
        const auto start = current().span;
        binary(builder, level + 1);
        while (operator_at(level)) {
            binary_rhs(builder, level, start);
            if (level == 2) break;
        }
    }
    /// Consumes the current binary operator and compiles its RHS.
    /// Keeps operator-local storage out of ordinary precedence frames.
    /// Precondition: operator_at(level).
    /// Complexity: O(RHS compilation and lookup/emission work).
    void binary_rhs(dyn_segment& builder, std::size_t level, source_span start) {
        assert(operator_at(level));
        const auto op = std::get<punctuation>(current().value);
        advance();
        if (level < 2) {
            dyn_segment rhs(builder.before_top());
            binary(rhs, level + 1);
            structural(span_from(start), [&] {
                if (level == 0)
                    builder.logical_or(std::move(rhs));
                else
                    builder.logical_and(std::move(rhs));
            });
        } else {
            binary(builder, level + 1);
            lookup_access::apply(lookup_, op.spelling(), builder, 2, span_from(start));
        }
    }
    /// cast_expression = unary_expression { "as" identifier }.
    /// Complexity: O(tokens + unary compilation + cast lookup/emission work).
    void cast(dyn_segment& builder) {
        const auto start = current().span;
        unary(builder);
        while (keyword_is("as")) {
            advance();
            const auto target = expect_name();
            lookup_access::apply_cast(lookup_, target, builder, span_from(start));
        }
    }
    /// unary_expression = ("-" | "!") unary_expression | postfix_expression.
    /// Collects prefixes iteratively and emits in reverse order, avoiding flat-chain recursion.
    /// Complexity: O(prefix count + postfix/emission work), O(prefix count) temporary storage.
    void unary(dyn_segment& builder) {
        std::vector<std::pair<punctuation, source_span>> prefixes;
        while (punctuation_is("-") || punctuation_is("!")) {
            prefixes.emplace_back(std::get<punctuation>(current().value), current().span);
            advance();
        }
        postfix(builder);
        for (auto i = prefixes.rbegin(); i != prefixes.rend(); ++i)
            lookup_access::apply(lookup_, i->first.spelling(), builder, 1, span_from(i->second));
    }
    /// arguments = "(" [ expression { "," expression } ] ")".
    /// Returns the explicit argument count; a consumed comma always requires another expression.
    /// Complexity: O(argument compilation), O(nested expression metadata + code) storage.
    std::size_t arguments(dyn_segment& builder) {
        expect_group(delimiter::parenthesis, true);
        std::size_t count = 0;
        if (!closing(delimiter::parenthesis)) {
            ordinary(builder);
            count = 1;
            while (punctuation_is(",")) {
                advance();
                ordinary(builder);
                count = increment(count);
            }
        }
        expect_group(delimiter::parenthesis, false);
        return count;
    }
    /// selector = unsuffixed_integer | unsuffixed_dotted_decimal.
    /// Decimal token spelling may supply two tuple selectors; exponent digits cannot parse.
    /// Malformed selectors identify their token; structural failures identify the full postfix.
    /// Complexity: O(spelling bytes + structural metadata work), O(normalized spelling) storage.
    void tuple_selector(dyn_segment& builder, source_span postfix_start) {
        const auto start = current().span;
        const auto* number = std::get_if<numeric_literal>(&current().value);
        if (!number)
            throw parse_error(error_category::malformed_input, start,
                              "expected integer or member name after `.`");
        if (!number->suffix.empty())
            throw parse_error(error_category::malformed_input, start,
                              "tuple index must be an unsuffixed integer");
        std::array<std::size_t, 2> indices{};
        std::size_t count = 1;
        std::string_view spelling = number->spelling;
        if (number->form == numeric_form::integer) {
            int radix = 10;
            if (spelling.starts_with("0x")) {
                radix = 16;
                spelling.remove_prefix(2);
            } else if (spelling.starts_with("0o")) {
                radix = 8;
                spelling.remove_prefix(2);
            } else if (spelling.starts_with("0b")) {
                radix = 2;
                spelling.remove_prefix(2);
            }
            indices[0] = selector_index(spelling, radix, start);
        } else {
            const auto dot = spelling.find('.');
            if (dot == std::string_view::npos)
                throw parse_error(error_category::malformed_input, start,
                                  "tuple index chain must use decimal notation");
            indices[0] = selector_index(spelling.substr(0, dot), 10, start);
            indices[1] = selector_index(spelling.substr(dot + 1), 10, start);
            count = 2;
        }
        advance();
        structural(span_from(postfix_start), [&] {
            for (std::size_t i = 0; i != count; ++i)
                builder.project_tuple(indices[i]);
        });
    }
    /// postfix_expression = primary { arguments | "." (identifier [arguments] | selector) }.
    /// Complexity: O(postfix tokens + emission work), O(nested expressions + code) storage.
    void postfix(dyn_segment& builder) {
        const auto start = current().span;
        primary(builder);
        while (true) {
            if (opening(delimiter::parenthesis)) {
                const auto count = arguments(builder);
                lookup_access::apply(lookup_, "()", builder, increment(count), span_from(start));
            } else if (punctuation_is(".")) {
                advance();
                if (std::holds_alternative<identifier>(current().value)) {
                    const auto name_span = current().span;
                    const auto name = expect_name();
                    const auto count = opening(delimiter::parenthesis) ? arguments(builder) : 0;
                    lookup_access::apply(lookup_, name, builder, increment(count), name_span,
                                         lookup_selector::member);
                } else {
                    tuple_selector(builder, start);
                }
            } else
                break;
        }
    }
    /// primary_expression = literal | identifier [record_fields] | tuple_or_group
    /// | array_expression | if_expression.
    /// Complexity: O(selected primary compilation).
    void primary(dyn_segment& builder) {
        const auto start = current().span;
        if (const auto* boolean = std::get_if<bool>(&current().value)) {
            builder.just(value::from(*boolean));
            advance();
        } else if (const auto* number = std::get_if<numeric_literal>(&current().value)) {
            if (number->form == numeric_form::integer &&
                (number->suffix != "f64" || number->spelling.starts_with("0b") ||
                 number->spelling.starts_with("0o") || number->spelling.starts_with("0x")))
                throw parse_error(error_category::unsupported_literal, start,
                                  "integer value literals are outside the bool/f64 core");
            if (!number->suffix.empty() && number->suffix != "f64")
                throw parse_error(error_category::unsupported_literal, start,
                                  "unsupported numeric suffix");
            builder.just(value::from(number->value));
            advance();
        } else if (std::holds_alternative<identifier>(current().value)) {
            const auto name = expect_name();
            if (name == "if")
                conditional(builder, start);
            else if (name == "else" || name == "as")
                throw parse_error(error_category::malformed_input, start,
                                  "reserved keyword is not a value name");
            else if (!no_record_ && opening(delimiter::brace))
                record(builder, name, start);
            else
                lookup_access::apply(lookup_, name, builder, 0, start);
        } else if (opening(delimiter::parenthesis))
            tuple(builder);
        else if (opening(delimiter::bracket))
            array(builder);
        else
            throw parse_error(error_category::malformed_input, start, "expected expression");
    }
    /// tuple_or_group = "(" [ expression [ "," [ expression { "," expression } ] ] ] ")".
    /// Complexity: O(element compilation + structural metadata work).
    void tuple(dyn_segment& builder) {
        const auto start = current().span;
        advance();
        const auto ambient = builder.snapshot();
        std::size_t count = 0;
        if (!closing(delimiter::parenthesis)) {
            ordinary(builder);
            count = 1;
            if (!punctuation_is(",")) {
                expect_group(delimiter::parenthesis, false);
                return;
            }
            advance();
            if (!closing(delimiter::parenthesis)) {
                ordinary(builder);
                count = 2;
                while (punctuation_is(",")) {
                    advance();
                    ordinary(builder);
                    count = increment(count);
                }
            }
        }
        expect_group(delimiter::parenthesis, false);
        structural(span_from(start), [&] { builder.make_tuple(count, ambient); });
    }
    /// Parses P2's complete type prefix and retains its exact consumed source interval.
    /// The P2 public return is semantic metadata; it is not an initialized callable owner.
    /// Complexity: O(type prefix tokens + complete descriptor construction).
    value_type annotation() { return parse_type(cursor_, lookup_, last_); }
    /// array_expression = "[" [expression { "," expression }] "]" [ ":" type_expression ].
    /// An annotation names the whole array; an empty array requires it and binds its DAG.
    /// Complexity: O(element/type compilation + compared or bound metadata),
    /// O(compilation metadata + bound descriptor nodes) storage.
    void array(dyn_segment& builder) {
        const auto start = current().span;
        advance();
        const auto ambient = builder.snapshot();
        std::size_t count = 0;
        if (!closing(delimiter::bracket)) {
            ordinary(builder);
            count = 1;
            while (punctuation_is(",")) {
                advance();
                ordinary(builder);
                count = increment(count);
            }
        }
        expect_group(delimiter::bracket, false);
        std::optional<value_type> declared;
        if (punctuation_is(":")) {
            advance();
            declared = annotation();
            const auto type_span = last_;
            if (declared->kind() != type_kind::array)
                throw parse_error(error_category::type_error, type_span,
                                  "array annotations must name a complete array type");
        }
        if (count == 0 && !declared)
            throw parse_error(error_category::type_error, span_from(start),
                              "an empty array literal has no inferable element type");
        structural(span_from(start), [&] {
            auto element = count == 0 ? bind_runtime_ownership(declared->element()) :
                                        builder.operand_types(count).front();
            if (declared && !declared->element().same_shape(element))
                throw parse_error(error_category::type_error, span_from(start),
                                  "array annotation element mismatch");
            builder.make_array(count, std::move(element), ambient);
        });
    }
    /// record_fields = "{" [ identifier ":" expression { "," identifier ":" expression } ] "}".
    /// Evaluates fields in source order and maps them into the complete canonical nominal schema.
    /// Complexity: O(fields + name bytes) expected lookup time, plus emitted/type metadata work;
    /// O(fields + names) temporary storage.
    void record(dyn_segment& builder, std::string_view name, source_span start) {
        const auto type = lookup_.find_type(name);
        const bool registered = type && type->kind() == type_kind::record;
        advance();
        const auto ambient = builder.snapshot();
        std::unordered_map<std::string_view, std::size_t> positions;
        const auto names = registered ? type->field_names() : std::span<const std::string>{};
        for (std::size_t i = 0; i != names.size(); ++i)
            positions.emplace(names[i], i);
        std::vector<bool> supplied(names.size(), false);
        std::vector<std::size_t> permutation;
        if (!closing(delimiter::brace)) {
            do {
                const auto field_span = current().span;
                const auto field = expect_name();
                const auto found = positions.find(field);
                if (registered && (found == positions.end() || supplied[found->second]))
                    throw parse_error(error_category::type_error, field_span,
                                      "unknown or duplicate record field");
                expect_punctuation(":");
                ordinary(builder);
                if (registered) {
                    if (!builder.operand_types(1).front().same_shape(type->fields()[found->second]))
                        throw parse_error(error_category::type_error, field_span,
                                          "record field type mismatch");
                    supplied[found->second] = true;
                    permutation.push_back(found->second);
                }
                if (!punctuation_is(",")) break;
                advance();
            } while (true);
        }
        expect_group(delimiter::brace, false);
        if (!registered)
            throw parse_error(error_category::type_error, span_from(start),
                              "unknown record constructor");
        structural(span_from(start), [&] {
            builder.make_record(bind_runtime_ownership(*type), permutation, ambient);
        });
    }
    /// closure_expression = "||" expression | "|" closure_param { "," closure_param } "|"
    /// expression. closure_param = identifier ":" type_expression. Restores isolated scopes even
    /// when type checking, formal installation, or body emission fails. Complexity: O(parameters +
    /// name bytes + metadata/body compilation) time and storage.
    void closure(dyn_segment& builder) {
        const auto start = current().span;
        const bool empty = punctuation_is("||");
        advance();
        std::vector<value_type> parameters;
        std::unordered_map<std::string, std::pair<std::size_t, value_type>,
                           parser_detail::name_hash, std::equal_to<>>
            formals;
        if (!empty) {
            do {
                const auto name_span = current().span;
                const auto name = expect_name();
                if (name == "if" || name == "else" || name == "as" || name == "true" ||
                    name == "false")
                    throw parse_error(error_category::malformed_input, name_span,
                                      "reserved keyword is not a closure parameter name");
                if (formals.contains(name))
                    throw parse_error(error_category::malformed_input, name_span,
                                      "duplicate closure parameter name");
                expect_punctuation(":");
                auto type = annotation();
                formals.emplace(name, std::pair{parameters.size(), type});
                parameters.push_back(std::move(type));
                if (!punctuation_is(",")) break;
                advance();
            } while (true);
            expect_punctuation("|");
        }
        structural(span_from(start), [&] { (void)value_type::callable(parameters, value_type{}); });
        dyn_segment body;
        {
            closure_scopes scopes(lookup_);
            lookup_access::push_scope(
                lookup_, [formals = std::move(formals)](std::string_view name, dyn_segment& code,
                                                        std::size_t arity, source_span) {
                    if (arity != 0) return false;
                    const auto found = formals.find(name);
                    if (found == formals.end()) return false;
                    code.push_argument(found->second.first, found->second.second);
                    return true;
                });
            expression(body);
        }
        structural(span_from(start),
                   [&] { builder.push_closure(std::move(body).finish(), parameters); });
    }
    /// if_expression = "if" expression "{" expression "}" [ "else" ("{" expression "}" |
    /// if_expression) ]. Validates both dependent results and emits only selected execution; an
    /// absent else is unit.
    /// Complexity: O(condition/branch compilation + compared metadata), O(code + bounded nesting)
    /// storage.
    void conditional(dyn_segment& builder, source_span start) {
        nesting_guard nesting(depth_, start);
        {
            restriction_guard restriction(no_record_, true);
            expression(builder);
        }
        expect_group(delimiter::brace, true);
        dyn_segment yes(builder.before_top());
        ordinary(yes);
        expect_group(delimiter::brace, false);
        dyn_segment no(builder.before_top());
        if (keyword_is("else")) {
            advance();
            if (keyword_is("if")) {
                const auto nested_start = current().span;
                advance();
                conditional(no, nested_start);
            } else {
                expect_group(delimiter::brace, true);
                ordinary(no);
                expect_group(delimiter::brace, false);
            }
        } else {
            no.make_tuple(0, no.snapshot());
        }
        structural(span_from(start), [&] { builder.if_else(std::move(yes), std::move(no)); });
    }

public:
    /// Borrows an owned lookup and current-token cursor for this compilation only.
    expression_compiler(Cursor& cursor, op_lookup& lookup) : cursor_(cursor), lookup_(lookup) {}
    /// Compiles one prefix, optionally requiring the producer's terminal end token.
    /// Complexity: O(tokens + emitted operations + lookup/type/structural metadata work).
    compiled_segment compile(bool full) {
        dyn_segment builder;
        expression(builder);
        if (full && !std::holds_alternative<end_token>(current().value))
            throw parse_error(error_category::malformed_input, current().span,
                              "unexpected trailing input");
        return std::move(builder).finish();
    }
};
} // namespace

/// Compiles a complete source with exclusive lookup access and a streaming owned lexer.
/// Complexity: O(lexed bytes + emitted operations + lookup/type/structural metadata work).
compiled_segment parser::parse_str(std::string_view source) {
    compilation_guard exclusive(active_);
    lex_lexer cursor(source);
    return expression_compiler(cursor, lookup_).compile(true);
}
} // namespace cel

#if CPP_LIBRARY_TESTING()
#include <cmath>
#include <doctest/doctest.h>
#include <ostream>

namespace cel::parser_detail {
/// Compiles internal producer streams under the parser's ownership and exclusivity contracts.
struct parser_access {
    /// Compiles a complete borrowed stream; terminal producer tokens end traversal.
    /// Complexity: O(tokens + emitted code + lookup and type metadata).
    static compiled_segment parse_tokens(parser& compiler, std::span<const token> tokens);
    /// Compiles a prefix, leaving the following host token unconsumed.
    /// Complexity: O(consumed tokens + emitted code + lookup and type metadata).
    static compiled_segment parse_expression(parser& compiler, token_cursor& cursor);
};
} // namespace cel::parser_detail

namespace cel {
/// Compiles a complete external stream without copying its token payloads.
/// Complexity: O(tokens + emitted operations + lookup/type/structural metadata work).
compiled_segment parser_detail::parser_access::parse_tokens(parser& compiler,
                                                            std::span<const token> tokens) {
    compilation_guard exclusive(compiler.active_);
    token_cursor cursor(tokens);
    return expression_compiler(cursor, compiler.lookup_).compile(true);
}
/// Compiles one borrowed prefix without consuming its following host token.
/// Complexity: O(tokens + emitted operations + lookup/type/structural metadata work).
compiled_segment parser_detail::parser_access::parse_expression(parser& compiler,
                                                                token_cursor& cursor) {
    compilation_guard exclusive(compiler.active_);
    return expression_compiler(cursor, compiler.lookup_).compile(false);
}
} // namespace cel

#include <cel/runtime.hpp>
#include <string>

namespace {
/// Collects an external producer stream for the private borrowed-token boundary.
/// Complexity: O(source bytes + tokens) time and storage.
std::vector<cel::parser_detail::token> tokens(std::string_view source) {
    cel::parser_detail::lex_lexer lexer(source);
    std::vector<cel::parser_detail::token> result;
    do {
        result.push_back(lexer.current());
        if (std::holds_alternative<cel::parser_detail::end_token>(lexer.current().value)) break;
        lexer.advance();
    } while (true);
    return result;
}
/// Compiles and executes text with a fresh caller stack.
/// Complexity: O(compilation + execution).
cel::value evaluate(cel::parser& parser, std::string_view text) {
    cel::raw_stack stack;
    return parser.parse_str(text).execute(stack, {});
}
/// Checks an exact source diagnostic through the public compiler.
/// Complexity: O(compilation).
void error_at(cel::parser& parser,
              std::string_view text,
              cel::error_category category,
              cel::source_span span) {
    try {
        (void)parser.parse_str(text);
        FAIL("invalid expression accepted");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == category);
        CHECK(error.span() == span);
    }
}
} // namespace

TEST_CASE("numeric values require floating literal forms without integer coercion") {
    cel::parser parser{cel::op_lookup{}};
    for (const auto& [text, span] : std::array{
             std::pair{"1", cel::source_span{0, 1}}, std::pair{"1 as f64", cel::source_span{0, 1}},
             std::pair{"1+2.0", cel::source_span{0, 1}}, std::pair{"1.0+1", cel::source_span{4, 5}},
             std::pair{"(|x:f64|x)(1)", cel::source_span{11, 12}},
             std::pair{"0x10", cel::source_span{0, 4}}, std::pair{"0b10", cel::source_span{0, 4}},
             std::pair{"0o10", cel::source_span{0, 4}}, std::pair{"0b1f64", cel::source_span{0, 6}},
             std::pair{"0o7_f64", cel::source_span{0, 7}},
             std::pair{"0x1f64", cel::source_span{0, 6}}}) {
        CAPTURE(text);
        error_at(parser, text, cel::error_category::unsupported_literal, span);
        CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, tokens(text)),
                        cel::parse_error);
    }
    CHECK(evaluate(parser, "1.0+2.0").as<double>() == 3.0);
    CHECK(evaluate(parser, "(|x:f64|x)(1.0)").as<double>() == 1.0);
    CHECK(evaluate(parser, "(1.0,2.0).0x1").as<double>() == 2.0);
}

TEST_CASE("decimal integer-form f64 literals produce floating values") {
    cel::parser parser{cel::op_lookup{}};
    for (const auto& [text, expected] : std::array{
             std::pair{"1f64", 1.0}, std::pair{"0f64", 0.0}, std::pair{"1_f64", 1.0},
             std::pair{"1__f64", 1.0}, std::pair{"1_000f64", 1000.0},
             std::pair{"9007199254740993f64", 9007199254740992.0}, std::pair{"1e2f64", 100.0},
             std::pair{"1f64 + 2.0", 3.0}, std::pair{"-1f64", -1.0}, std::pair{"1f64 as f64", 1.0},
             std::pair{"(|x:f64|x)(1f64)", 1.0}}) {
        CAPTURE(std::string_view(text));
        CHECK(evaluate(parser, text).as<double>() == expected);
        cel::raw_stack stack;
        CHECK(cel::parser_detail::parser_access::parse_tokens(parser, tokens(text))
                  .execute(stack, {})
                  .as<double>() == expected);
    }
    CHECK(std::signbit(evaluate(parser, "-0f64").as<double>()));
    for (auto text : {"1f64x", "1f64_", "1f640", "1f32", "1.0f64x", "(1.0,2.0).1f64"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE("postfix brackets remain host input and are not core array indexing") {
    cel::parser parser{cel::op_lookup{}};
    const auto input = tokens("[1.0][0.0]");
    CHECK_THROWS_AS(parser.parse_str("[1.0][0.0]"), cel::parse_error);
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, input),
                    cel::parse_error);
    cel::parser_detail::token_cursor cursor(input);
    const auto program = cel::parser_detail::parser_access::parse_expression(parser, cursor);
    REQUIRE(std::holds_alternative<cel::parser_detail::open_delimiter>(cursor.current().value));
    CHECK(cursor.current().span == cel::source_span{5, 6});
    cel::raw_stack stack;
    CHECK(program.execute(stack, {}).at(0).as<double>() == 1.0);
}

TEST_CASE("all six range forms preserve complete type and endpoint order") {
    cel::parser parser{cel::op_lookup{}};
    const auto forms = std::array{std::pair{"1.0 + 2.0..4.0 * 2.0", cel::range_kind::range},
                                  std::pair{"1.0..=3.0", cel::range_kind::range_inclusive},
                                  std::pair{"1.0..", cel::range_kind::range_from},
                                  std::pair{"..3.0", cel::range_kind::range_to},
                                  std::pair{"..=3.0", cel::range_kind::range_to_inclusive},
                                  std::pair{"..", cel::range_kind::range_full}};
    for (const auto& [text, kind] : forms) {
        CAPTURE(text);
        CHECK(evaluate(parser, text).type().form() == kind);
    }
    auto range = evaluate(parser, "1.0 + 2.0..4.0 * 2.0");
    CHECK(range.at(0).as<double>() == 3);
    CHECK(range.at(1).as<double>() == 8);
    for (auto text : {"..=", "1.0..=", "1.0..2.0..3.0", "true..false", "1.0==2.0..3.0==4.0"}) {
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
    for (auto text : {"..else", "1.0..else", "..as", "1.0..as"}) {
        const auto input = tokens(text);
        cel::parser_detail::token_cursor cursor(input);
        CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_expression(parser, cursor),
                        cel::parse_error);
    }
}

TEST_CASE("independent closure scope isolation restores scopes after success and failure") {
    cel::op_lookup lookup;
    lookup.push_library_scope(
        [](std::string_view name, cel::dyn_segment& b, std::size_t n, cel::source_span) {
            if (name != "library" || n != 0) return false;
            b.just(cel::value::from(7.0));
            return true;
        });
    cel::parser_detail::lookup_access::push_scope(
        lookup, [](std::string_view name, cel::dyn_segment& b, std::size_t n, cel::source_span) {
            if (name != "ambient" || n != 0) return false;
            b.just(cel::value::from(3.0));
            return true;
        });
    cel::parser parser(std::move(lookup));
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK(evaluate(parser, "(|| library)()").as<double>() == 7);
    CHECK_THROWS_AS(parser.parse_str("|| ambient"), cel::parse_error);
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK_THROWS_AS(parser.parse_str("|x:f64| || x"), cel::parse_error);
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK(evaluate(parser, "(|ambient:f64| ambient)(9.0)").as<double>() == 9);
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK(evaluate(parser, "if true {ambient} else {0.0}").as<double>() == 3);
    CHECK(evaluate(parser, "(|ambient:bool| if ambient {1.0} else {2.0})(true)").as<double>() == 1);
}

TEST_CASE(
    "text and token methods reject trailing input inline leaves host token diagnostics are owned") {
    cel::parser parser{cel::op_lookup{}};
    auto input = tokens("1.0+2.0, host");
    cel::parser_detail::token_cursor cursor(input);
    auto program = cel::parser_detail::parser_access::parse_expression(parser, cursor);
    CHECK(std::get<cel::parser_detail::punctuation>(cursor.current().value).spelling() == ",");
    cel::raw_stack stack;
    CHECK(program.execute(stack, {}).as<double>() == 3);
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, input),
                    cel::parse_error);
    CHECK(cel::parser_detail::parser_access::parse_tokens(parser, tokens("(1.0,2.0).1"))
              .execute(stack, {})
              .as<double>() == 2);
    error_at(parser, "1.0 2.0", cel::error_category::malformed_input, {4, 7});
    error_at(parser, "[1.0,]", cel::error_category::malformed_input, {5, 6});
    error_at(parser, "(1.0]", cel::error_category::delimiter_error, {4, 5});
    error_at(parser, "missing", cel::error_category::lookup_error, {0, 7});
    error_at(parser, "[1.0,true]:[f64]   ", cel::error_category::type_error, {0, 16});
    for (const auto& [text, expected] :
         std::array{std::pair{"[]:(f64)   , host", cel::source_span{3, 8}},
                    std::pair{"[true]:([f64])   , host", cel::source_span{0, 14}},
                    std::pair{"[[]:[bool]]:([[f64]])   , host", cel::source_span{0, 21}}}) {
        CAPTURE(std::string_view(text));
        const auto annotated = tokens(text);
        cel::parser_detail::token_cursor prefix(annotated);
        try {
            (void)cel::parser_detail::parser_access::parse_expression(parser, prefix);
            FAIL("invalid annotation accepted");
        } catch (const cel::parse_error& error) {
            CHECK(error.category() == cel::error_category::type_error);
            CHECK(error.span() == expected);
        }
        CHECK(std::get<cel::parser_detail::punctuation>(prefix.current().value).spelling() == ",");
    }
    auto invalid = std::array{cel::parser_detail::token{
        {20, 21},
        cel::parse_error(cel::error_category::unsupported_literal, {20, 21}, "producer")}};
    try {
        (void)cel::parser_detail::parser_access::parse_tokens(parser, invalid);
        FAIL("producer diagnostic ignored");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == cel::error_category::unsupported_literal);
        CHECK(error.span() == cel::source_span{20, 21});
        CHECK(std::string(error.what()) == "producer");
    }
    const auto malformed = std::array{
        cel::parser_detail::token{
            {10, 11},
            cel::parser_detail::open_delimiter{cel::parser_detail::delimiter::parenthesis}},
        cel::parser_detail::token{{11, 14},
                                  cel::parser_detail::numeric_literal{
                                      1, "1.0", "", cel::parser_detail::numeric_form::decimal}},
        cel::parser_detail::token{
            {14, 15}, cel::parser_detail::close_delimiter{cel::parser_detail::delimiter::bracket}}};
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, malformed),
                    cel::parse_error);
    const auto invisible = std::array{
        cel::parser_detail::token{
            {0, 0}, cel::parser_detail::open_delimiter{cel::parser_detail::delimiter::none}},
        cel::parser_detail::token{
            {0, 0}, cel::parser_detail::close_delimiter{cel::parser_detail::delimiter::none}}};
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, invisible),
                    cel::parse_error);
    auto host_close = tokens("1.0+2.0");
    host_close.back() = {{7, 8},
                         cel::parser_detail::close_delimiter{cel::parser_detail::delimiter::brace}};
    cel::parser_detail::token_cursor host(host_close);
    CHECK(cel::parser_detail::parser_access::parse_expression(parser, host)
              .execute(stack, {})
              .as<double>() == 3);
    CHECK(std::holds_alternative<cel::parser_detail::close_delimiter>(host.current().value));
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, host_close),
                    cel::parse_error);
    const auto terminal = std::array{
        cel::parser_detail::token{{0, 3},
                                  cel::parser_detail::numeric_literal{
                                      1, "1.0", "", cel::parser_detail::numeric_form::decimal}},
        cel::parser_detail::token{{3, 3}, cel::parser_detail::end_token{}},
        cel::parser_detail::token{{4, 5}, cel::parser_detail::identifier{"ignored"}}};
    CHECK(cel::parser_detail::parser_access::parse_tokens(parser, terminal)
              .execute(stack, {})
              .as<double>() == 1);
    const auto external_suffix = std::array{cel::parser_detail::token{
        {0, 4},
        cel::parser_detail::numeric_literal{1, "1i32", "i32",
                                            cel::parser_detail::numeric_form::integer}}};
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, external_suffix),
                    cel::parse_error);
}
#endif
