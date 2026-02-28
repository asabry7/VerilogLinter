#pragma once

#include <string_view>
#include <string>
#include <unordered_set>
#include <iostream>
#include <cstdint>

// ==========================================
// 1. TOKEN DEFINITIONS
// ==========================================

/**
 * @brief Represents the category of a lexical token.
 *
 * Uses uint8_t as the underlying type to minimize memory footprint.
 */
enum class TokenType : uint8_t
{
    Identifier,    ///< A user-defined name (e.g., variable or module name).
    Keyword,       ///< A reserved Verilog keyword (e.g., `module`, `wire`).
    NumberLiteral, ///< A numeric literal value (e.g., `8'hFF`, `42`).
    Symbol,        ///< A punctuation or operator symbol (e.g., `;`, `=`, `(`).
    EndToken       ///< Sentinel token indicating the end of the source input.
};

/**
 * @brief Represents a single lexical token produced by the lexer.
 *
 * The @p content field is a non-owning view into the original source string,
 * so the source must remain valid for the lifetime of any Token referencing it.
 */
struct Token
{
    TokenType type;           ///< The category of this token.
    std::string_view content; ///< A view of the raw token text in the source code.
};

/**
 * @brief A lexical analyzer (lexer) for Verilog source code.
 *
 * Iterates over a source string and produces a sequence of @ref Token objects
 * one at a time via @ref get_next_token(). The analyzer does not own the
 * source buffer; the caller must ensure the buffer outlives the analyzer.
 *
 * Typical usage:
 * @code
 * std::string src = "module foo; endmodule";
 * LexicalAnalyzer lexer(src);
 * Token tok;
 * while ((tok = lexer.get_next_token()).type != TokenType::EndToken) {
 *     // process tok
 * }
 * @endcode
 */
class LexicalAnalyzer
{
public:
    /**
     * @brief Constructs a LexicalAnalyzer over the given source code.
     *
     * @param source_code A view of the Verilog source text to tokenize.
     *                    The underlying character data must remain valid and
     *                    unmodified for the lifetime of this object.
     */
    LexicalAnalyzer(std::string_view source_code);

    /**
     * @brief Extracts and returns the next token from the source code.
     *
     * Skips any leading whitespace, then consumes characters to form the
     * longest valid token. Returns a token of type @ref TokenType::EndToken
     * when the entire source has been consumed.
     *
     * @return The next @ref Token in the source stream.
     */
    Token get_next_token();

    /**
     * @brief increments the pointer by one.
     *
     */
    void advance();

    /**
     * @brief increments the pointer by a given count.
     *
     */
    void advance(size_t count);

    /**
     * @brief returns the current line number.
     *
     * @return The next @ref value of the current line number.
     */
    int get_current_line_number_value();

private:
    const char *current_character_pointer; ///< Points to the next character to be consumed.
    const char *end_character_pointer;     ///< Points one past the last character of the source.
    int current_line_number;               ///< Counter that keeps track of the current line number

    /**
     * @brief Checks whether a character is a decimal digit (0–9).
     * @param character The character to test.
     * @return @c true if @p character is a digit, @c false otherwise.
     */
    static bool is_digit_character(char character);

    /**
     * @brief Checks whether a character is an ASCII letter (a–z or A–Z).
     * @param character The character to test.
     * @return @c true if @p character is alphabetic, @c false otherwise.
     */
    static bool is_alphabet_character(char character);

    /**
     * @brief Checks whether a character is a whitespace character (space, tab, newline, etc.).
     * @param character The character to test.
     * @return @c true if @p character is whitespace, @c false otherwise.
     */
    static bool is_whitespace_character(char character);

    /**
     * @brief Checks whether a string matches a reserved Verilog keyword.
     * @param string_view_input The identifier text to look up.
     * @return @c true if @p string_view_input is a Verilog keyword, @c false otherwise.
     */
    static bool is_verilog_keyword(std::string_view string_view_input);
};