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

/// Provides decoded CEL text tokens and producer-independent forward traversal.
namespace cel {

/// Identifies a half-open UTF-8 byte interval; begin never exceeds end.
/// Example: source_span{0, 4} covers all bytes of "true".
struct source_span {
    std::size_t begin = 0; ///< Inclusive byte offset.
    std::size_t end = 0;   ///< Exclusive byte offset.

    /// Compares byte intervals.
    bool operator==(const source_span&) const = default;
};

/// Classifies a source diagnostic independently of its human-readable message.
/// Example: error_category::unsupported_literal identifies an f32 suffix.
enum class error_category {
    malformed_input,     ///< Invalid UTF-8, spelling, comment, or input character.
    unsupported_literal, ///< A literal or suffix outside bool/binary64 scope.
    delimiter_error,     ///< An unmatched, mismatched, or unclosed delimiter.
    resource_limit       ///< A producer's explicitly documented resource limit.
};

/// Owns a source-located diagnostic; copied diagnostics own independent messages.
/// Example: parse_error(error_category::malformed_input, {0, 1}, "invalid input").
class parse_error : public std::runtime_error {
    error_category category_;
    source_span span_;

public:
    /// Constructs a diagnostic with an owned message and ordered byte interval.
    /// Complexity: O(message.size()) time and space.
    parse_error(error_category category, source_span span, std::string message);
    /// Returns the diagnostic category.
    [[nodiscard]] error_category category() const noexcept { return category_; }
    /// Returns the byte interval supplied by the producer.
    [[nodiscard]] source_span span() const noexcept { return span_; }
};

/// Retains the numeric syntax independently of its binary64 scalar type.
/// Example: numeric_form::decimal describes the coalesced selector token "0.1".
enum class numeric_form {
    integer, ///< Unsuffixed or suffixed integer spelling, including radix prefixes.
    decimal, ///< Decimal spelling with a dot and without an exponent.
    exponent ///< Decimal spelling with an exponent, with or without a dot.
};

/// Owns exact spelling and suffix; value uses round-to-nearest binary64 conversion.
/// Overflow yields positive infinity and underflow rounds to a subnormal or zero.
/// Example: numeric_literal{1.0, "1f64", "f64", numeric_form::integer}.
struct numeric_literal {
    double value;         ///< Converted unsigned scalar; signs are punctuation.
    std::string spelling; ///< Exact source spelling, including underscores and suffix.
    std::string suffix;   ///< Empty or f64 for the text adapter.
    numeric_form form;    ///< Syntax classification, not a scalar type.
};

/// Owns an identifier's exact spelling, including a raw r# prefix when present.
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
    /// Copies one to three ASCII punctuation bytes.
    /// Throws std::invalid_argument for an empty, oversized, or non-punctuation spelling.
    explicit punctuation(std::string_view spelling);
    /// Borrows the spelling until this punctuation object dies or is assigned.
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
    delimiter kind; ///< Group kind.
};
/// Marks a group's closing boundary; example: close_delimiter{delimiter::brace}.
struct close_delimiter {
    delimiter kind; ///< Group kind.
};
/// Owns doc-comment content without its markers; example: doc_comment{"text", false}.
struct doc_comment {
    std::string text; ///< Retained content, including spaces and nested comment markers.
    bool inner;       ///< True for //! and /*! comments; false for outer comments.
};
/// Marks terminal input; example: end_token{}.
struct end_token {};
/// Stores one owned payload; example: token_value{true}.
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
    source_span span;  ///< Producer's half-open byte interval.
    token_value value; ///< Owned payload; error diagnostics retain their own span.
};

/// Borrows a flattened producer's tokens and exposes only the current token.
/// The sequence must outlive this cursor and remain unchanged during traversal.
/// Explicit end and error tokens terminate traversal; following tokens are ignored.
/// An implicit end uses the last consumed token's exclusive end, or zero for empty input.
/// Example: token_cursor cursor(std::span<const token>{}); cursor.advance().
class token_cursor {
    std::span<const token> tokens_;
    token end_{{0, 0}, end_token{}};

public:
    /// Primes the cursor to the first token or implicit end.
    explicit token_cursor(std::span<const token> tokens);
    /// Prevents cloning traversal state.
    token_cursor(const token_cursor&) = delete;
    /// Prevents assignment of traversal state.
    token_cursor& operator=(const token_cursor&) = delete;
    /// Returns the current token, borrowed until the cursor dies or advances.
    [[nodiscard]] const token& current() const noexcept;
    /// Consumes one token; advance at end does nothing and advance after error reaches end.
    void advance() noexcept;
};

/// Owns decoded text and lazily emits flattened CEL tokens through a single-token cursor.
/// Construction validates all UTF-8 and primes current(); malformed UTF-8 becomes an error
/// token before any source token. Other lexical errors are terminal diagnostic tokens.
/// Advancing an error reaches end; advancing end does nothing. The source and copied
/// payloads never borrow the constructor argument. Delimiter checking is iterative.
/// Complexity: O(n) source copy/validation; traversal O(n + u log R), where u is
/// identifier scalars and R is Unicode property ranges; O(n + d) owned storage for
/// source and delimiter depth d, plus the current payload. No fixed depth limit.
/// Example: lex_lexer lexer("1f64"); std::get<numeric_literal>(lexer.current().value).value.
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
    /// Prevents cloning traversal state.
    lex_lexer(const lex_lexer&) = delete;
    /// Prevents assignment of traversal state.
    lex_lexer& operator=(const lex_lexer&) = delete;
    /// Returns the current token, borrowed until advance(), assignment, or destruction.
    [[nodiscard]] const token& current() const noexcept { return current_; }
    /// Emits the next token; a terminal diagnostic advances to end and end stays end.
    /// Complexity: O(consumed bytes + identifier scalars log R), plus payload storage.
    void advance();
};

} // namespace cel

#endif // CEL_PARSER_HPP
