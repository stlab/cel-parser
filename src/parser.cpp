// SPDX-License-Identifier: BSL-1.0
#include "lexer.hpp"

#include "unicode_data.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

namespace cel::parser_detail {
namespace {

static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 &&
                  std::numeric_limits<double>::max_exponent == 1024 && sizeof(double) == 8,
              "CEL requires IEEE binary64");

/// Stores a decoded Unicode scalar and its UTF-8 byte extent.
struct scalar {
    std::uint32_t value;
    std::size_t size;
};

/// Decodes one UTF-8 scalar, rejecting truncation, overlong forms, and invalid scalars.
/// Returns no value at an invalid offset; reads at most four bytes.
std::optional<scalar> decode(std::string_view source, std::size_t offset) noexcept {
    if (offset >= source.size()) return std::nullopt;
    const auto first = static_cast<unsigned char>(source[offset]);
    if (first < 0x80) return scalar{first, 1};
    const std::size_t count = first >= 0xc2 && first <= 0xdf ? 2 :
                              first >= 0xe0 && first <= 0xef ? 3 :
                              first >= 0xf0 && first <= 0xf4 ? 4 :
                                                               0;
    if (count == 0 || count > source.size() - offset) return std::nullopt;
    std::uint32_t value = first & (0x7fU >> count);
    // Validates and folds each continuation byte into the scalar.
    for (std::size_t i = 1; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(source[offset + i]);
        if ((byte & 0xc0U) != 0x80U) return std::nullopt;
        value = (value << 6U) | (byte & 0x3fU);
    }
    if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
        (count == 4 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        return std::nullopt;
    return scalar{value, count};
}

/// Decodes a scalar in previously validated UTF-8 source.
/// Precondition: offset identifies a scalar boundary before source.end().
scalar validated_scalar(std::string_view source, std::size_t offset) noexcept {
    const auto value = decode(source, offset);
    assert(value.has_value());
    return *value;
}

/// Finds a scalar in sorted, disjoint inclusive ranges in O(log R) time.
template <std::size_t N>
bool contains(const std::array<detail::unicode_range, N>& ranges, std::uint32_t value) noexcept {
    const auto found = std::lower_bound(ranges.begin(), ranges.end(), value,
                                        [](const detail::unicode_range& range,
                                           std::uint32_t needle) { return range.last < needle; });
    return found != ranges.end() && found->first <= value;
}

/// Checks the proc-macro2 identifier-start rule.
/// Complexity: O(log R) time for R Unicode ranges.
bool ident_start(std::uint32_t value) noexcept {
    return value == '_' || contains(detail::xid_start, value);
}

/// Checks the proc-macro2 identifier-continuation rule.
/// Complexity: O(log R) time for R Unicode ranges.
bool ident_continue(std::uint32_t value) noexcept { return contains(detail::xid_continue, value); }

/// Checks Rust White_Space plus the producer's U+200E/U+200F additions.
bool whitespace(std::uint32_t value) noexcept {
    return (value >= 9 && value <= 13) || value == 0x20 || value == 0x85 || value == 0xa0 ||
           value == 0x1680 || (value >= 0x2000 && value <= 0x200a) || value == 0x200e ||
           value == 0x200f || value == 0x2028 || value == 0x2029 || value == 0x202f ||
           value == 0x205f || value == 0x3000;
}

/// Returns a hexadecimal digit's value, or -1 for a nondigit.
int digit(char ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/// Converts power-of-two radix digits into exact hexadecimal integer spelling.
/// Preconditions: digits contain only underscores and valid base digits.
/// Complexity: O(digits.size()) time and space.
std::string hexadecimal(std::string_view digits, unsigned bits) {
    constexpr std::string_view alphabet = "0123456789abcdef";
    std::string result;
    result.reserve(digits.size());
    unsigned pending = 0;
    unsigned count = 0;
    // Groups bits from the least significant digit without rounding or overflow.
    for (auto i = digits.rbegin(); i != digits.rend(); ++i) {
        if (*i == '_') continue;
        assert(digit(*i) >= 0);
        pending |= static_cast<unsigned>(digit(*i)) << count;
        count += bits;
        while (count >= 4) {
            result.push_back(alphabet[pending & 15U]);
            pending >>= 4U;
            count -= 4;
        }
    }
    if (count != 0) result.push_back(alphabet[pending]);
    std::reverse(result.begin(), result.end());
    return result;
}

/// Classifies a decimal range failure as overflow or underflow without converting it.
/// Preconditions: text is a valid, nonnegative, underscore-free decimal number.
/// Complexity: O(text.size()) time and O(1) extra space.
bool decimal_overflow(std::string_view text) noexcept {
    const auto e = text.find_first_of("eE");
    auto mantissa = text;
    if (e != std::string_view::npos) mantissa.remove_suffix(text.size() - e);
    const auto dot = mantissa.find('.');
    const auto integer_end = dot == std::string_view::npos ? mantissa.size() : dot;
    const auto first = mantissa.find_first_of("123456789");
    if (first == std::string_view::npos) return false;
    // Only the sign of the decimal order is needed after from_chars reports range.
    // Clamp at n so even a huge exponent can be classified without integer overflow.
    const auto cap = text.size();
    std::size_t exponent = 0;
    bool negative = false;
    if (e != std::string_view::npos) {
        std::size_t i = e + 1;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            negative = text[i] == '-';
            ++i;
        }
        for (; i < text.size(); ++i) {
            const auto next = static_cast<std::size_t>(text[i] - '0');
            if (exponent > cap / 10 || next > cap - exponent * 10)
                exponent = cap;
            else
                exponent = exponent * 10 + next;
        }
    }
    if (first < integer_end) {
        const auto positive_order = integer_end - first - 1;
        return !negative || positive_order > exponent;
    }
    const auto negative_order = first - integer_end;
    return !negative && exponent > negative_order;
}

/// Converts validated unsigned decimal or radix spelling with locale-independent from_chars.
/// Range failures produce Rust's positive infinity or zero; representable values use
/// the standard converter's binary64 rounding.
/// Complexity: O(spelling.size()) time and space.
double convert(std::string_view spelling, unsigned base) {
    std::string normalized;
    if (base != 10) {
        normalized = hexadecimal(spelling.substr(2), base == 2 ? 1 : base == 8 ? 3 : 4);
    } else {
        normalized.reserve(spelling.size());
        std::copy_if(spelling.begin(), spelling.end(), std::back_inserter(normalized),
                     [](char ch) { return ch != '_'; });
    }
    double value = 0;
    const auto result =
        std::from_chars(normalized.data(), normalized.data() + normalized.size(), value,
                        base == 10 ? std::chars_format::general : std::chars_format::hex);
    if (result.ec == std::errc::result_out_of_range)
        return base != 10 || decimal_overflow(normalized) ?
                   std::numeric_limits<double>::infinity() :
                   0.0;
    if (result.ec != std::errc{} || result.ptr != normalized.data() + normalized.size())
        throw std::logic_error("validated CEL number rejected by from_chars");
    return value;
}

/// Returns the delimiter kind for an ASCII boundary, or no value.
std::optional<delimiter> delimiter_kind(char ch) noexcept {
    switch (ch) {
        case '(':
        case ')':
            return delimiter::parenthesis;
        case '[':
        case ']':
            return delimiter::bracket;
        case '{':
        case '}':
            return delimiter::brace;
        default:
            return std::nullopt;
    }
}
} // namespace

punctuation::punctuation(std::string_view spelling) : size_(spelling.size()) {
    if (spelling.empty() || spelling.size() > bytes_.size() ||
        spelling.find_first_not_of("~!@#$%^&*-=+|;:,<.>/?'") != std::string_view::npos)
        throw std::invalid_argument("punctuation requires one to three punctuation bytes");
    std::copy(spelling.begin(), spelling.end(), bytes_.begin());
}

token_cursor::token_cursor(std::span<const token> tokens) : tokens_(tokens) {}

const token& token_cursor::current() const noexcept {
    return tokens_.empty() ? end_ : tokens_.front();
}

void token_cursor::advance() noexcept {
    if (tokens_.empty() || std::holds_alternative<end_token>(tokens_.front().value)) return;
    const auto end = tokens_.front().span.end;
    if (std::holds_alternative<parse_error>(tokens_.front().value))
        tokens_ = {};
    else
        tokens_ = tokens_.subspan(1);
    end_.span = {end, end};
}

lex_lexer::lex_lexer(std::string_view text) : source_(text) {
    // Validates the whole owned source once, including comments and unsupported literals.
    for (std::size_t offset = 0; offset < source_.size();) {
        const auto ch = decode(source_, offset);
        if (!ch) {
            fail(error_category::malformed_input, {offset, offset + 1}, "invalid UTF-8");
            return;
        }
        offset += ch->size;
    }
    read_next();
}

void lex_lexer::fail(error_category category, source_span span, std::string message) {
    current_ = {span, parse_error(category, span, std::move(message))};
}

void lex_lexer::advance() {
    if (std::holds_alternative<end_token>(current_.value)) return;
    if (std::holds_alternative<parse_error>(current_.value)) {
        position_ = source_.size();
        delimiters_.clear();
        current_ = {{position_, position_}, end_token{}};
        return;
    }
    read_next();
}

bool lex_lexer::comment() {
    const std::string_view text(source_);
    const auto start = position_;
    const bool line = text.substr(start).starts_with("//");
    const bool inner = text.size() - start > 2 && text[start + 2] == '!';
    const bool doc =
        inner ||
        (line ? text.substr(start).starts_with("///") && !text.substr(start).starts_with("////") :
                text.substr(start).starts_with("/**") && !text.substr(start).starts_with("/***") &&
                    !text.substr(start).starts_with("/**/"));
    std::size_t content_end = 0;
    if (line) {
        const auto newline = text.find('\n', start + 2);
        position_ = newline == std::string_view::npos ? text.size() : newline;
        content_end = position_;
        if (content_end > start + 2 && text[content_end - 1] == '\r' && position_ < text.size())
            --content_end;
    } else {
        std::size_t depth = 1;
        position_ += 2;
        while (position_ < text.size()) {
            const auto rest = text.substr(position_);
            if (rest.starts_with("/*")) {
                ++depth;
                position_ += 2;
            } else if (rest.starts_with("*/")) {
                --depth;
                position_ += 2;
                if (depth == 0) break;
            } else
                ++position_;
        }
        if (depth != 0) {
            fail(error_category::malformed_input, {start, position_}, "unfinished block comment");
            return true;
        }
        content_end = position_ - 2;
    }
    if (!doc) return false;
    const auto content = text.substr(start + 3, content_end - start - 3);
    // Doc comments reject bare CR; ordinary comments do not.
    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r' && (i + 1 == content.size() || content[i + 1] != '\n')) {
            const auto offset = start + 3 + i;
            fail(error_category::malformed_input, {offset, offset + 1}, "bare CR in doc comment");
            return true;
        }
    }
    current_ = {{start, position_}, doc_comment{std::string(content), inner}};
    return true;
}

void lex_lexer::name() {
    const auto start = position_;
    const std::string_view text(source_);
    const bool raw = text.substr(start).starts_with("r#");
    if (raw) position_ += 2;
    const auto first = decode(text, position_);
    if (!first || !ident_start(first->value)) {
        fail(error_category::malformed_input, {start, position_}, "invalid raw identifier");
        return;
    }
    position_ += first->size;
    while (const auto ch = decode(text, position_)) {
        if (!ident_continue(ch->value)) break;
        position_ += ch->size;
    }
    const auto name = text.substr(start + (raw ? 2 : 0), position_ - start - (raw ? 2 : 0));
    if (raw &&
        (name == "_" || name == "super" || name == "self" || name == "Self" || name == "crate")) {
        fail(error_category::malformed_input, {start, position_}, "forbidden raw identifier");
    } else if (!raw && (name == "true" || name == "false")) {
        current_ = {{start, position_}, name == "true"};
    } else
        current_ = {{start, position_},
                    identifier{std::string(text.substr(start, position_ - start))}};
}

void lex_lexer::number() {
    const std::string_view text(source_);
    const auto start = position_;
    unsigned base = 10;
    if (text.substr(start).starts_with("0x"))
        base = 16;
    else if (text.substr(start).starts_with("0o"))
        base = 8;
    else if (text.substr(start).starts_with("0b"))
        base = 2;
    if (base != 10) position_ += 2;
    bool has_digit = false;
    while (position_ < text.size()) {
        const auto ch = text[position_];
        if (ch == '_') {
            ++position_;
            continue;
        }
        const auto value = digit(ch);
        if (value < 0 || (base == 10 && value >= 10)) break;
        if (static_cast<unsigned>(value) >= base) {
            if (ch >= '0' && ch <= '9') {
                ++position_;
                fail(error_category::malformed_input, {start, position_},
                     "digit outside literal radix");
                return;
            }
            break;
        }
        has_digit = true;
        ++position_;
    }
    if (!has_digit) {
        fail(error_category::malformed_input, {start, position_}, "radix literal has no digits");
        return;
    }
    auto form = numeric_form::integer;
    if (base == 10 && position_ < text.size() && text[position_] == '.') {
        const auto after = decode(text, position_ + 1);
        if (!after || (after->value != '.' && !ident_start(after->value))) {
            form = numeric_form::decimal;
            ++position_;
            while (position_ < text.size() &&
                   ((text[position_] >= '0' && text[position_] <= '9') || text[position_] == '_'))
                ++position_;
        }
    }
    if (base == 10 && position_ < text.size() &&
        (text[position_] == 'e' || text[position_] == 'E')) {
        auto end = position_ + 1;
        bool sign = false;
        bool exponent_digit = false;
        while (end < text.size()) {
            const char ch = text[end];
            if (ch == '_')
                ++end;
            else if (ch >= '0' && ch <= '9') {
                exponent_digit = true;
                ++end;
            } else if ((ch == '+' || ch == '-') && !sign && !exponent_digit) {
                sign = true;
                ++end;
            } else
                break;
        }
        if (exponent_digit) {
            position_ = end;
            form = numeric_form::exponent;
        }
    }
    const auto suffix_start = position_;
    if (const auto first = decode(text, position_); first && ident_start(first->value)) {
        position_ += first->size;
        while (const auto ch = decode(text, position_)) {
            if (!ident_continue(ch->value)) break;
            position_ += ch->size;
        }
    }
    if (const auto ch = decode(text, position_); ch && ident_continue(ch->value)) {
        fail(error_category::malformed_input, {position_, position_ + ch->size},
             "invalid number boundary");
        return;
    }
    const auto suffix = text.substr(suffix_start, position_ - suffix_start);
    if (!suffix.empty() && suffix != "f64") {
        fail(error_category::unsupported_literal, {start, position_}, "unsupported numeric suffix");
        return;
    }
    const auto spelling = text.substr(start, suffix_start - start);
    current_ = {{start, position_},
                numeric_literal{convert(spelling, base),
                                std::string(text.substr(start, position_ - start)),
                                std::string(suffix), form}};
}

void lex_lexer::quoted_literal(std::size_t quote) {
    const auto start = position_;
    const std::string_view text(source_);
    const char marker = text[quote];
    const auto prefix = text.substr(start, quote - start);
    const bool raw = prefix.find('r') != std::string_view::npos;
    const auto first_hash = prefix.find('#');
    const auto hashes = first_hash == std::string_view::npos ? 0 : prefix.size() - first_hash;
    position_ = quote + 1;
    while (position_ < text.size()) {
        if (!raw && text[position_] == '\\') {
            ++position_;
            if (position_ < text.size()) position_ += validated_scalar(text, position_).size;
            continue;
        }
        if (text[position_] == marker) {
            auto end = position_ + 1;
            std::size_t matched = 0;
            while (matched < hashes && end < text.size() && text[end] == '#') {
                ++matched;
                ++end;
            }
            if (matched == hashes) {
                position_ = end;
                if (const auto first = decode(text, position_);
                    first && ident_start(first->value)) {
                    position_ += first->size;
                    while (const auto ch = decode(text, position_)) {
                        if (!ident_continue(ch->value)) break;
                        position_ += ch->size;
                    }
                }
                fail(error_category::unsupported_literal, {start, position_},
                     "unsupported quoted literal");
                return;
            }
        }
        ++position_;
    }
    fail(error_category::malformed_input, {start, position_}, "unfinished quoted literal");
}

void lex_lexer::read_next() {
    const std::string_view text(source_);
    while (position_ < text.size()) {
        const auto start = position_;
        const auto ch = validated_scalar(text, position_);
        if (whitespace(ch.value)) {
            position_ += ch.size;
            continue;
        }
        const auto rest = text.substr(start);
        if (rest.starts_with("//") || rest.starts_with("/*")) {
            if (comment()) return;
            continue;
        }
        if (const auto kind = delimiter_kind(text[start])) {
            ++position_;
            if (text[start] == '(' || text[start] == '[' || text[start] == '{') {
                delimiters_.emplace_back(*kind, source_span{start, position_});
                current_ = {{start, position_}, open_delimiter{*kind}};
            } else if (delimiters_.empty() || delimiters_.back().first != *kind) {
                fail(error_category::delimiter_error, {start, position_},
                     "unmatched closing delimiter");
            } else {
                delimiters_.pop_back();
                current_ = {{start, position_}, close_delimiter{*kind}};
            }
            return;
        }
        if (ch.value >= '0' && ch.value <= '9') {
            number();
            return;
        }
        // Detects unsupported cooked/raw Rust literal prefixes before identifier scanning.
        auto quote = start;
        if (text[quote] == 'b' || text[quote] == 'c') ++quote;
        bool raw = quote < text.size() && text[quote] == 'r';
        if (raw) ++quote;
        if (raw)
            while (quote < text.size() && text[quote] == '#')
                ++quote;
        if (quote < text.size() && (text[quote] == '"' || (!raw && text[quote] == '\''))) {
            if (text[quote] == '\'' && quote == start) {
                const auto first = decode(text, quote + 1);
                if (first && ident_start(first->value)) {
                    auto end = quote + 1 + first->size;
                    while (const auto next = decode(text, end)) {
                        if (!ident_continue(next->value)) break;
                        end += next->size;
                    }
                    if (end == text.size() || text[end] != '\'') {
                        ++position_;
                        current_ = {{start, position_}, punctuation("'")};
                        return;
                    }
                }
            }
            quoted_literal(quote);
            return;
        }
        if (ident_start(ch.value)) {
            name();
            return;
        }
        if (ch.value < 0x80 &&
            std::string_view("~!@#$%^&*-=+|;:,<.>/?").find(text[start]) != std::string_view::npos) {
            std::size_t size = 1;
            constexpr std::array compounds{
                "&&", "||", "==", "!=", "<=", ">=", "<<", ">>", "->", "=>", ":=", ".."};
            if (std::any_of(compounds.begin(), compounds.end(),
                            [rest](std::string_view pair) { return rest.starts_with(pair); }))
                size = rest.starts_with("..=") ? 3 : 2;
            position_ += size;
            current_ = {{start, position_}, punctuation(text.substr(start, size))};
            return;
        }
        position_ += ch.size;
        fail(error_category::malformed_input, {start, position_}, "unrecognized input character");
        return;
    }
    if (!delimiters_.empty()) {
        fail(error_category::delimiter_error, delimiters_.back().second, "unclosed delimiter");
    } else
        current_ = {{position_, position_}, end_token{}};
}
} // namespace cel::parser_detail

namespace cel {
parse_error::parse_error(error_category category, source_span span, std::string message) :
    std::runtime_error(std::move(message)), category_(category), span_(span) {
    assert(span.begin <= span.end);
}
} // namespace cel
