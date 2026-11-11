// SPDX-License-Identifier: BSL-1.0
#include "type.hpp"
#include "lookup_access.hpp"
#include <algorithm>

namespace cel::parser_detail {
namespace {
/// Borrows one current-token cursor and an immutable complete-type resolver.
template <class Cursor>
class type_parser {
    Cursor& cursor_;
    const op_lookup& lookup_;
    source_span last_{};
    /// Returns a current diagnostic-free token; producer errors retain their original message/span.
    const token& current() const {
        if (const auto* error = std::get_if<parse_error>(&cursor_.current().value)) throw *error;
        return cursor_.current();
    }
    /// Consumes a token and retains its exact span for the successful prefix boundary.
    /// Complexity: O(advanced lexer work), or O(1) for external tokens.
    void advance() {
        last_ = current().span;
        cursor_.advance();
    }
    /// Checks the current punctuation without consuming or looking ahead.
    bool punctuation_is(std::string_view spelling) const {
        const auto* p = std::get_if<punctuation>(&current().value);
        return p && p->spelling() == spelling;
    }
    /// Checks the current closing delimiter without consuming it.
    bool closing(delimiter kind) const {
        const auto* close = std::get_if<close_delimiter>(&current().value);
        return close && close->kind == kind;
    }
    /// Requires and consumes a closing delimiter, returning its producer end byte.
    /// Throws a source-located delimiter diagnostic on mismatch or missing closure.
    /// Complexity: O(1) for tokens, or O(source bytes advanced by a text lexer).
    std::size_t close(delimiter kind) {
        if (!closing(kind))
            throw parse_error(error_category::delimiter_error, current().span,
                              "expected closing delimiter");
        const auto end = current().span.end;
        advance();
        return end;
    }
    /// Resolves zero/one-argument range constructors without adapting endpoint types.
    /// Unknown names/signatures return nullopt; descriptor failures propagate.
    std::optional<value_type> range(std::string_view name, std::span<const value_type> args) const {
        if (name == "RangeFull" && args.empty())
            return value_type::range(range_kind::range_full, value_type::leaf<double>());
        if (args.size() != 1 || !args[0].same_shape(value_type::leaf<double>()))
            return std::nullopt;
        constexpr std::array forms{std::pair{"Range", range_kind::range},
                                   std::pair{"RangeInclusive", range_kind::range_inclusive},
                                   std::pair{"RangeFrom", range_kind::range_from},
                                   std::pair{"RangeTo", range_kind::range_to},
                                   std::pair{"RangeToInclusive", range_kind::range_to_inclusive}};
        const auto found =
            std::ranges::find(forms, name, [](const auto& entry) { return entry.first; });
        if (found == forms.end()) return std::nullopt;
        return value_type::range(found->second, args[0]);
    }
    /// type_expression = identifier [ "(" [ type_expression { "," type_expression } ] ")" ]
    /// | "[" type_expression "]" | "(" [ type_expression [ "," [ type_expression
    /// { "," type_expression } ] ] ] ")".
    /// Throws a source diagnostic before exceeding 128 active recursive productions.
    /// Complexity: O(subtree tokens + name bytes + complete descriptor construction + any
    /// lexed bytes), O(subtree metadata + depth) storage.
    value_type production(std::size_t depth) {
        if (depth >= 128)
            throw parse_error(error_category::resource_limit, current().span,
                              "type nesting exceeds 128");
        if (const auto* identifier_token = std::get_if<identifier>(&current().value)) {
            const auto start = current().span.begin;
            auto end = current().span.end;
            const auto name = identifier_token->spelling;
            advance();
            std::vector<value_type> args;
            const auto* opening = std::get_if<open_delimiter>(&current().value);
            if (opening && opening->kind == delimiter::parenthesis) {
                advance();
                if (!closing(delimiter::parenthesis)) {
                    args.push_back(production(depth + 1));
                    while (punctuation_is(",")) {
                        advance();
                        args.push_back(production(depth + 1));
                    }
                }
                end = close(delimiter::parenthesis);
            }
            if (name == "Fn") {
                if (args.size() != 2 || args[0].kind() != type_kind::tuple)
                    throw parse_error(error_category::type_error, {start, end},
                                      "Fn requires a parameter tuple and a result type");
                try {
                    return value_type::callable(args[0].fields(), args[1]);
                } catch (const std::invalid_argument& error) {
                    throw parse_error(error_category::type_error, {start, end}, error.what());
                }
            }
            if (auto type = range(name, args)) return *type;
            if (args.empty()) {
                if (auto type = lookup_.find_type(name)) return *type;
            }
            throw parse_error(error_category::type_error, {start, end},
                              "unknown or unsupported type `" + name + "`");
        }
        if (const auto* opening = std::get_if<open_delimiter>(&current().value)) {
            const auto kind = opening->kind;
            if (kind == delimiter::bracket) {
                advance();
                auto element = production(depth + 1);
                (void)close(delimiter::bracket);
                return value_type::array(std::move(element));
            }
            if (kind == delimiter::parenthesis) {
                advance();
                if (closing(kind)) {
                    advance();
                    return {};
                }
                auto first = production(depth + 1);
                if (!punctuation_is(",")) {
                    (void)close(kind);
                    return first;
                }
                advance();
                std::vector<value_type> fields;
                fields.push_back(std::move(first));
                if (!closing(kind)) {
                    fields.push_back(production(depth + 1));
                    while (punctuation_is(",")) {
                        advance();
                        fields.push_back(production(depth + 1));
                    }
                }
                (void)close(kind);
                return value_type::tuple(fields);
            }
        }
        throw parse_error(error_category::malformed_input, current().span,
                          "expected type expression");
    }

public:
    /// Borrows the cursor and lookup for this one prefix parse.
    type_parser(Cursor& cursor, const op_lookup& lookup) : cursor_(cursor), lookup_(lookup) {}
    /// Parses one prefix without consuming its following host token.
    /// Complexity: O(tokens + name bytes + descriptor construction + any lexed bytes),
    /// O(nodes + bounded recursion) storage.
    value_type parse(source_span* parsed_span = nullptr) {
        const auto start = current().span;
        auto result = production(0);
        if (parsed_span) *parsed_span = {start.begin, last_.end};
        return result;
    }
};
} // namespace
value_type parse_type(token_cursor& cursor, const op_lookup& lookup) {
    return type_parser(cursor, lookup).parse();
}
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup) {
    return type_parser(cursor, lookup).parse();
}
/// Parses a token prefix and publishes its exact successful source interval.
value_type parse_type(token_cursor& cursor, const op_lookup& lookup, source_span& parsed_span) {
    return type_parser(cursor, lookup).parse(&parsed_span);
}
/// Parses a text prefix and publishes its exact successful source interval.
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup, source_span& parsed_span) {
    return type_parser(cursor, lookup).parse(&parsed_span);
}
value_type parse_type(std::span<const token> tokens, const op_lookup& lookup) {
    token_cursor cursor(tokens);
    auto type = parse_type(cursor, lookup);
    if (const auto* error = std::get_if<parse_error>(&cursor.current().value)) throw *error;
    if (!std::holds_alternative<end_token>(cursor.current().value))
        throw parse_error(error_category::malformed_input, cursor.current().span,
                          "trailing type input");
    return type;
}
} // namespace cel::parser_detail
