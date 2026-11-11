// SPDX-License-Identifier: BSL-1.0

#ifndef CEL_PARSER_HPP
#define CEL_PARSER_HPP

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/// \file
/// Provides owned CEL tokens and forward-only cursors for token consumers and producers.
/// Include `<cel/parser/lookup.hpp>` for type parsing or `<cel/parser/compiler.hpp>` for
/// expression compilation.

/// Provides CEL token, lookup, and compilation interfaces.
namespace cel {

/// Identifies a half-open UTF-8 byte interval; begin never exceeds end.
/// Example: source_span{0, 4} covers all bytes of "true".
struct source_span {
    std::size_t begin = 0; ///< Inclusive byte offset in the producer's source.
    std::size_t end = 0;   ///< Exclusive byte offset; equal to begin for an empty interval.

    /// Returns true if both byte offsets are equal.
    bool operator==(const source_span&) const = default;
};

/// Classifies a source diagnostic independently of its human-readable message.
/// Example: error_category::unsupported_literal identifies an f32 suffix.
enum class error_category {
    malformed_input,     ///< Invalid UTF-8, spelling, comment, or input character.
    unsupported_literal, ///< A literal or suffix outside bool/binary64 scope.
    type_error,          ///< An unknown, unsupported, ineligible, or incompatible type.
    lookup_error,        ///< An undefined name or unmatched operation signature.
    delimiter_error,     ///< An unmatched, mismatched, or unclosed delimiter.
    resource_limit       ///< A documented producer or compilation resource limit.
};

/// Owns a source-located diagnostic; copied diagnostics own independent messages.
/// Example: parse_error(error_category::malformed_input, {0, 1}, "invalid input").
class parse_error : public std::runtime_error {
    error_category category_;
    source_span span_;

public:
    /// Constructs a diagnostic with an owned message, category, and byte interval.
    /// Precondition: span.begin <= span.end.
    /// Complexity: O(message.size()) time and space.
    parse_error(error_category category, source_span span, std::string message);
    /// Returns the category supplied at construction.
    [[nodiscard]] error_category category() const noexcept { return category_; }
    /// Returns the byte interval supplied at construction, without translating offsets.
    [[nodiscard]] source_span span() const noexcept { return span_; }
};

/// Retains the numeric syntax independently of its binary64 scalar type.
/// Example: numeric_form::decimal describes the coalesced selector token "0.1".
enum class numeric_form {
    integer, ///< Unsuffixed or suffixed integer spelling, including radix prefixes.
    decimal, ///< Decimal spelling with a dot and without an exponent.
    exponent ///< Decimal spelling with an exponent, with or without a dot.
};

/// Owns a numeric token's exact spelling, suffix, and converted binary64 value.
/// The text lexer uses round-to-nearest binary64 conversion.
/// Overflow yields positive infinity and underflow rounds to a subnormal or zero.
/// Copying takes O(spelling.size() + suffix.size()) time and space.
/// Example: numeric_literal{1.0, "1f64", "f64", numeric_form::integer}.
struct numeric_literal {
    double value;         ///< Converted scalar; the text lexer emits signs as punctuation.
    std::string spelling; ///< Exact source spelling, including underscores and suffix.
    std::string suffix;   ///< Empty or f64 for the text adapter.
    numeric_form form;    ///< Syntax classification, not a scalar type.
};

/// Owns an identifier's exact spelling, including a raw r# prefix when present.
/// Copying takes O(spelling.size()) time and space.
/// Example: identifier{"α"}.
struct identifier {
    std::string spelling; ///< Exact producer spelling; no Unicode normalization.
};

/// Owns one to three punctuation bytes without heap allocation.
/// Example: punctuation("..=").spelling() returns "..=".
class punctuation {
    std::array<char, 3> bytes_{};
    std::size_t size_ = 0;

public:
    /// Copies one to three bytes from the ASCII set `~!@#$%^&*-=+|;:,<.>/?'`.
    /// Throws std::invalid_argument for an empty, oversized, or non-punctuation spelling.
    /// The spelling need not be an operator recognized by the expression grammar.
    explicit punctuation(std::string_view spelling);
    /// Returns the spelling, borrowed until this object dies, is assigned, or is moved from.
    [[nodiscard]] std::string_view spelling() const noexcept { return {bytes_.data(), size_}; }
};

/// Identifies flattened group delimiters, including an external producer's invisible group.
/// Example: delimiter::bracket describes an array group's boundaries.
enum class delimiter {
    parenthesis, ///< Parenthesis group.
    bracket,     ///< Square-bracket group.
    brace,       ///< Brace group.
    none         ///< Invisible group supplied by another flattened producer.
};
/// Marks a group's opening boundary; example: open_delimiter{delimiter::brace}.
struct open_delimiter {
    delimiter kind; ///< Opening group kind, paired with a close of the same kind.
};
/// Marks a group's closing boundary; example: close_delimiter{delimiter::brace}.
struct close_delimiter {
    delimiter kind; ///< Closing group kind, paired with an open of the same kind.
};
/// Owns doc-comment content without its markers; example: doc_comment{"text", false}.
/// Copying takes O(text.size()) time and space.
struct doc_comment {
    std::string text; ///< Retained content, including spaces and nested comment markers.
    bool inner;       ///< True for //! and /*! comments; false for /// and /** comments.
};
/// Marks terminal input; example: end_token{}.
struct end_token {};
/// Stores one owned payload, including owned diagnostic messages.
/// Copying takes O(payload bytes) time and space for string-bearing alternatives.
/// Example: token_value{true}.
using token_value = std::variant<bool,
                                 numeric_literal,
                                 identifier,
                                 punctuation,
                                 open_delimiter,
                                 close_delimiter,
                                 doc_comment,
                                 end_token,
                                 parse_error>;
/// Owns a payload and producer span; copied tokens survive their producer and source.
/// Payload copies cost O(payload bytes), excluding inline punctuation and scalar values.
/// Example: token{{0, 4}, true}.
struct token {
    source_span span;  ///< Producer's half-open byte interval, not a character index.
    token_value value; ///< Owned payload; consumers use a parse_error's own diagnostic span.
};

/// Borrows a flattened producer's tokens and exposes only the current token.
/// Precondition: the sequence outlives this cursor and remains unchanged during traversal.
/// Explicit end and error tokens terminate traversal; following tokens are ignored.
/// An implicit end uses the last consumed token's exclusive end, or zero for empty input.
/// The cursor preserves payloads and spans; it does not validate groups or re-lex tokens.
/// Copying and moving are unavailable.
/// Consume a producer's boolean token:
/// \code{.cpp}
/// std::array<cel::token, 1> tokens{{{{0, 4}, true}}};
/// cel::token_cursor cursor(tokens);
/// bool value = std::get<bool>(cursor.current().value); // true
/// cursor.advance(); // current() now contains end_token at {4, 4}.
/// \endcode
class token_cursor {
    std::span<const token> tokens_;
    token end_{{0, 0}, end_token{}};

public:
    /// Primes the cursor to the first token or implicit end.
    /// Borrows tokens under this class's lifetime and immutability precondition.
    explicit token_cursor(std::span<const token> tokens);
    /// Disallows copying this forward-only cursor.
    token_cursor(const token_cursor&) = delete;
    /// Disallows copy assignment of this forward-only cursor.
    token_cursor& operator=(const token_cursor&) = delete;
    /// Returns the current token, borrowed until this cursor dies or advances.
    /// Returns the producer's explicit end unchanged, or a synthesized end after exhaustion.
    [[nodiscard]] const token& current() const noexcept;
    /// Consumes one token; advancing end does nothing and advancing an error reaches end.
    /// Postcondition: after an error, current() is end at that token's span.end.
    void advance() noexcept;
};

/// Owns a copy of decoded text and emits flattened CEL tokens through a single-token cursor.
/// Construction validates all UTF-8 and primes current(); malformed UTF-8 becomes an error
/// token before any source token. Other lexical errors are terminal diagnostic tokens.
/// Advancing an error reaches end; advancing end does nothing. Source offsets count UTF-8
/// bytes, including trivia. End has an empty span at source.size(). Copied tokens and
/// the source never borrow the constructor argument. Copying and moving are unavailable.
/// Lexical diagnostics are tokens, not thrown parse_error exceptions.
/// Complexity: O(n) source copy/validation; traversal O(n + u log R), where u is
/// identifier scalars and R is Unicode property ranges; O(n + d) owned storage for
/// source and delimiter depth d, plus the current payload. No fixed depth limit.
/// Read a token after releasing the constructor argument:
/// \code{.cpp}
/// cel::lex_lexer lexer(std::string("1f64"));
/// cel::token saved = lexer.current();
/// lexer.advance();
/// double value = std::get<cel::numeric_literal>(saved.value).value; // 1.0
/// \endcode
class lex_lexer {
    std::string source_;
    std::size_t position_ = 0;
    token current_{{0, 0}, end_token{}};
    std::vector<std::pair<delimiter, source_span>> delimiters_;
    /// Emits the next nontrivia token or terminal diagnostic; total traversal is linear.
    void read_next();
    /// Consumes a comment; emits doc metadata when applicable.
    /// Complexity: O(comment bytes) time, O(doc content bytes) space.
    bool comment();
    /// Emits a number and retains exact spelling and suffix.
    /// Complexity: O(literal bytes) time and space.
    void number();
    /// Emits an identifier with the verified Unicode start/continuation rules.
    /// Complexity: O(scalars log R) time and O(identifier bytes) space.
    void name();
    /// Diagnoses an unsupported quoted literal or an unfinished quote.
    /// Complexity: O(literal bytes) time and O(1) extra space.
    void quoted_literal(std::size_t quote);
    /// Emits a terminal diagnostic without substituting a successful token.
    void fail(error_category category, source_span span, std::string message);

public:
    /// Copies and validates decoded text, then primes the current token.
    /// Complexity: O(text.size()) time and space, plus first-token traversal.
    explicit lex_lexer(std::string_view text);
    /// Disallows copying this forward-only lexer.
    lex_lexer(const lex_lexer&) = delete;
    /// Disallows copy assignment of this forward-only lexer.
    lex_lexer& operator=(const lex_lexer&) = delete;
    /// Returns the current token, borrowed until advance() or destruction.
    [[nodiscard]] const token& current() const noexcept { return current_; }
    /// Emits the next token; a terminal diagnostic advances to end and end stays end.
    /// Skips whitespace and ordinary comments, but emits doc comments as tokens.
    /// Allocation failures propagate; lexical failures become terminal parse_error tokens.
    /// Complexity: O(consumed bytes + identifier scalars log R), plus payload storage.
    void advance();
};
// [knowledge] Keep delimiter checking iterative so the text adapter has no fixed depth limit.

} // namespace cel

#endif // CEL_PARSER_HPP
