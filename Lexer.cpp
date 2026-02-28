#include "Lexer.h"

// Initialize pointers to the start and end of the provided source code.
LexicalAnalyzer::LexicalAnalyzer(std::string_view source_code)
    : current_character_pointer(source_code.data()),
      end_character_pointer(source_code.data() + source_code.size()) {}

Token LexicalAnalyzer::get_next_token()
{
    while (true)
    {
        // 1. End of file check
        if (current_character_pointer >= end_character_pointer)
            return {TokenType::EndToken, {}};

        // 2. Skip whitespaces (spaces, tabs, newlines)
        while (current_character_pointer < end_character_pointer && is_whitespace_character(*current_character_pointer))
            current_character_pointer++;
        if (current_character_pointer >= end_character_pointer)
            return {TokenType::EndToken, {}};

        // 3. Skip comments
        if (*current_character_pointer == '/' && current_character_pointer + 1 < end_character_pointer)
        {
            // Single-line comment "//"
            if (*(current_character_pointer + 1) == '/')
            {
                current_character_pointer += 2;
                while (current_character_pointer < end_character_pointer && *current_character_pointer != '\n')
                    current_character_pointer++;
                continue;
            }
            // Block comment "/* ... */"
            if (*(current_character_pointer + 1) == '*')
            {
                current_character_pointer += 2;
                while (current_character_pointer + 1 < end_character_pointer && !(*current_character_pointer == '*' && *(current_character_pointer + 1) == '/'))
                    current_character_pointer++;
                if (current_character_pointer + 1 < end_character_pointer)
                    current_character_pointer += 2; // skip "*/"
                continue;
            }
        }

        const char *start_character_pointer = current_character_pointer;

        // 4. Extract Numbers (Supports formats like 8'h00)
        if (is_digit_character(*current_character_pointer))
        {
            while (current_character_pointer < end_character_pointer &&
                   (is_digit_character(*current_character_pointer) ||
                    is_alphabet_character(*current_character_pointer) ||
                    *current_character_pointer == '\''))
            {
                current_character_pointer++;
            }
            return {TokenType::NumberLiteral, std::string_view(start_character_pointer, current_character_pointer - start_character_pointer)};
        }

        // 5. Extract Identifiers and Keywords
        if (is_alphabet_character(*current_character_pointer))
        {
            while (current_character_pointer < end_character_pointer &&
                   (is_alphabet_character(*current_character_pointer) || is_digit_character(*current_character_pointer)))
            {
                current_character_pointer++;
            }
            std::string_view extracted_text(start_character_pointer, current_character_pointer - start_character_pointer);
            if (is_verilog_keyword(extracted_text))
                return {TokenType::Keyword, extracted_text};
            return {TokenType::Identifier, extracted_text};
        }

        // 6. Extract Advanced Multi-character Symbols (<=, >=, ==, !=, <<, >>, &&, ||)
        if (current_character_pointer + 1 < end_character_pointer)
        {
            std::string_view two_chars(start_character_pointer, 2);
            if (two_chars == "<=" || two_chars == "==" || two_chars == "!=" ||
                two_chars == "<<" || two_chars == ">>" ||
                two_chars == "&&" || two_chars == "||" ||
                two_chars == ">=")
            {
                current_character_pointer += 2;
                return {TokenType::Symbol, two_chars};
            }
        }

        // 7. Single character symbols
        current_character_pointer++;
        return {TokenType::Symbol, std::string_view(start_character_pointer, 1)};
    }
}

// Private helper implementations
bool LexicalAnalyzer::is_digit_character(char character)
{
    return character >= '0' && character <= '9';
}

bool LexicalAnalyzer::is_alphabet_character(char character)
{
    return ((character | 32) >= 'a' && (character | 32) <= 'z') || character == '_';
}

bool LexicalAnalyzer::is_whitespace_character(char character)
{
    return character == ' ' || character == '\n' || character == '\t' || character == '\r';
}

bool LexicalAnalyzer::is_verilog_keyword(std::string_view string_view_input)
{
    return string_view_input == "module" || string_view_input == "endmodule" || string_view_input == "input" ||
           string_view_input == "output" || string_view_input == "inout" || string_view_input == "reg" ||
           string_view_input == "wire" || string_view_input == "always" || string_view_input == "assign" ||
           string_view_input == "posedge" || string_view_input == "negedge" || string_view_input == "begin" ||
           string_view_input == "end" || string_view_input == "if" || string_view_input == "else" ||
           string_view_input == "parameter" || string_view_input == "or" || string_view_input == "case" ||
           string_view_input == "endcase" || string_view_input == "default";
}