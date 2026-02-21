#include "Lexer.h"

// Initialize the two pointers that form our "sliding window" over the source buffer.
// We never copy the source — all tokens will be string_views into this original buffer.
LexicalAnalyzer::LexicalAnalyzer(std::string_view source_code)
    : current_character_pointer(source_code.data()),
      end_character_pointer(source_code.data() + source_code.size())
{
}

Token LexicalAnalyzer::get_next_token()
{
    // The outer loop lets us skip whitespace and comments and then retry,
    // rather than returning a meaningless token or recursing.
    while (true)
    {
        // ── End-of-input guard (checked before and after whitespace skipping) ──
        if (current_character_pointer >= end_character_pointer)
            return {TokenType::EndToken, {}};

        // ── Skip all consecutive whitespace characters ──
        while (current_character_pointer < end_character_pointer && is_whitespace_character(*current_character_pointer))
            current_character_pointer++;

        // After skipping whitespace we may have reached the end.
        if (current_character_pointer >= end_character_pointer)
            return {TokenType::EndToken, {}};

        // ── Comment detection: both styles start with '/' ──
        if (*current_character_pointer == '/' && current_character_pointer + 1 < end_character_pointer)
        {
            // ── Single-line comment: skip everything up to (but not including) the newline ──
            if (*(current_character_pointer + 1) == '/')
            {
                current_character_pointer += 2; // step past the '//'
                while (current_character_pointer < end_character_pointer && *current_character_pointer != '\n')
                    current_character_pointer++;
                continue; // restart the outer loop to look for the next real token
            }

            // ── Block comment: scan forward until the closing '*/' sequence ──
            if (*(current_character_pointer + 1) == '*')
            {
                current_character_pointer += 2; // step past the '/*'
                // Keep advancing until we find '*' immediately followed by '/'
                while (current_character_pointer + 1 < end_character_pointer &&
                       !(*current_character_pointer == '*' && *(current_character_pointer + 1) == '/'))
                    current_character_pointer++;
                // Step past the closing '*/' if we haven't hit the end
                if (current_character_pointer + 1 < end_character_pointer)
                    current_character_pointer += 2;
                continue; // restart the outer loop
            }
        }

        // ── At this point we are sitting on the first character of a real token ──
        // Record the start so we can slice a string_view over the token later.
        const char *start_character_pointer = current_character_pointer;

        // ── NUMBER LITERAL ──
        // A token starting with a digit is treated as a number.
        // We also consume letters and apostrophes to handle Verilog's
        // sized literals such as 8'hFF, 4'b1010, 16'd255, etc.
        if (is_digit_character(*current_character_pointer))
        {
            while (current_character_pointer < end_character_pointer &&
                   (is_digit_character(*current_character_pointer) ||
                    is_alphabet_character(*current_character_pointer) ||
                    *current_character_pointer == '\'')) // apostrophe separates size from base
                current_character_pointer++;

            // The full literal (e.g. "8'hFF") is now spanned by [start, current).
            return {TokenType::NumberLiteral,
                    std::string_view(start_character_pointer,
                                     current_character_pointer - start_character_pointer)};
        }

        // ── IDENTIFIER or KEYWORD ──
        // Identifiers start with a letter (or underscore, handled inside is_alphabet_character)
        // and may continue with letters or digits.
        if (is_alphabet_character(*current_character_pointer))
        {
            while (current_character_pointer < end_character_pointer &&
                   (is_alphabet_character(*current_character_pointer) ||
                    is_digit_character(*current_character_pointer)))
                current_character_pointer++;

            std::string_view extracted_text(start_character_pointer,
                                            current_character_pointer - start_character_pointer);

            // Check against the hard-coded keyword list; if it matches it's a
            // reserved word, otherwise it's a user-defined identifier.
            if (is_verilog_keyword(extracted_text))
                return {TokenType::Keyword, extracted_text};
            return {TokenType::Identifier, extracted_text};
        }

        // ── MULTI-CHARACTER SYMBOLS ──
        // Only two two-character operators exist in this subset of Verilog:
        //   '<=' (non-blocking assignment) and '==' (equality comparison).
        // We peek one character ahead to decide.
        if (current_character_pointer + 1 < end_character_pointer &&
            (*current_character_pointer == '<' || *current_character_pointer == '=') &&
            *(current_character_pointer + 1) == '=')
        {
            current_character_pointer += 2; // consume both characters
            return {TokenType::Symbol, std::string_view(start_character_pointer, 2)};
        }

        // ── SINGLE-CHARACTER SYMBOL ──
        // Everything else (punctuation, operators) is a one-character symbol token.
        current_character_pointer++; // consume the single character
        return {TokenType::Symbol, std::string_view(start_character_pointer, 1)};
    }
}

// ── Character classification helpers ──
// These are intentionally simple and branchless where possible.

// A digit is any character in the ASCII range '0'–'9'.
bool LexicalAnalyzer::is_digit_character(char character)
{
    return character >= '0' && character <= '9';
}

// A letter is a–z or A–Z (the '| 32' trick folds upper to lower case in ASCII),
// or an underscore which is legal in Verilog identifiers.
bool LexicalAnalyzer::is_alphabet_character(char character)
{
    return ((character | 32) >= 'a' && (character | 32) <= 'z') || character == '_';
}

// Whitespace covers the four common forms: space, newline, tab, carriage-return.
bool LexicalAnalyzer::is_whitespace_character(char character)
{
    return character == ' ' || character == '\n' || character == '\t' || character == '\r';
}

// Hard-coded list of every Verilog keyword recognised by this lexer.
// Using direct string comparisons keeps the implementation dependency-free
// and is fast enough for the keyword set size we support.
bool LexicalAnalyzer::is_verilog_keyword(std::string_view string_view_input)
{
    return string_view_input == "module" || string_view_input == "endmodule" ||
           string_view_input == "input" || string_view_input == "output" ||
           string_view_input == "inout" || string_view_input == "reg" ||
           string_view_input == "wire" || string_view_input == "always" ||
           string_view_input == "posedge" || string_view_input == "negedge" ||
           string_view_input == "begin" || string_view_input == "end" ||
           string_view_input == "if" || string_view_input == "else" ||
           string_view_input == "parameter" || string_view_input == "or" ||
           string_view_input == "case" || string_view_input == "endcase" ||
           string_view_input == "default";
}