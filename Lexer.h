/**
 * @file Lexer.h
 * @brief Lexical analyzer for Verilog source code.
 *
 * This file defines the token types and the LexicalAnalyzer class,
 * which is responsible for tokenizing raw Verilog source code strings
 * into a sequence of structured Token objects.
 */

#pragma once

#include <string_view>
#include <cstdint>

/**
 * @enum TokenType
 * @brief Represents the category of a recognized token.
 */
enum class TokenType : uint8_t
{
    Identifier,    ///< Variable names, module names, wire/reg names
    Keyword,       ///< Verilog reserved keywords (module, always, if, etc.)
    NumberLiteral, ///< Numeric literals (e.g., 8'hFF, 255)
    Symbol,        ///< Operators and punctuation (+, <=, ;, etc.)
    EndToken       ///< End of file/source
};

/**
 * @struct Token
 * @brief A single tokenized unit of Verilog code.
 */
struct Token
{
    TokenType type;
    std::string_view content;
};

/**
 * @class LexicalAnalyzer
 * @brief Scans a string view to extract sequential Verilog tokens.
 */
class LexicalAnalyzer
{
public:
    /**
     * @brief Constructs a LexicalAnalyzer.
     * @param source_code The string_view containing the Verilog source code.
     */
    LexicalAnalyzer(std::string_view source_code);

    /**
     * @brief Retrieves the next valid token from the source stream.
     * @return Token The next extracted token.
     */
    Token get_next_token();

private:
    const char *current_character_pointer;
    const char *end_character_pointer;

    static bool is_digit_character(char character);
    static bool is_alphabet_character(char character);
    static bool is_whitespace_character(char character);
    static bool is_verilog_keyword(std::string_view string_view_input);
};