#include "Lexer.h"

// ==========================================
// CONSTRUCTOR
// ==========================================

Lexer::Lexer(std::string_view src)
    : current(src.data()), end(src.data() + src.size())
{
}

// ==========================================
// MAIN LEXICAL SCANNER
// ==========================================

Token Lexer::next()
{
    while (true)
    {
        // 1. Check for End of File (EOF)
        if (current >= end)
            return {TokenType::EndToken, {}};

        // 2. Skip Whitespaces
        while (current < end && isSpace(*current))
            current++;

        // Re-check EOF after skipping whitespace
        if (current >= end)
            return {TokenType::EndToken, {}};

        // 3. Skip Comments
        if (*current == '/' && current + 1 < end)
        {
            // Single-line comment (//)
            if (*(current + 1) == '/')
            {
                current += 2; // Skip '//'
                while (current < end && *current != '\n')
                    current++; // Fast-forward to the end of the line
                continue;      // Restart the tokenization process
            }

            // Multi-line comment (/* ... */)
            if (*(current + 1) == '*')
            {
                current += 2; // Skip '/*'

                // Fast-forward until we hit '*/' or EOF
                while (current + 1 < end && !(*current == '*' && *(current + 1) == '/'))
                    current++;

                if (current + 1 < end)
                    current += 2; // Skip the closing '*/'

                continue; // Restart the tokenization process
            }
        }

        // ==========================================
        // TOKEN EXTRACTION
        // ==========================================

        const char *start = current; // Mark the beginning of the current token

        // 4. Parse Numbers (Supports Verilog formats like 8'hFF, 1'b0, etc.)
        if (isDigit(*current))
        {
            // Advance while characters are digits, letters (for hex 'A-F', 'h', 'b'), or the tick '\''
            while (current < end && (isDigit(*current) || isAlpha(*current) || *current == '\''))
                current++;

            return {TokenType::Number, std::string_view(start, current - start)};
        }

        // 5. Parse Identifiers and Keywords
        if (isAlpha(*current))
        {
            // Advance while valid identifier characters (letters, digits, underscores)
            while (current < end && (isAlpha(*current) || isDigit(*current)))
                current++;

            std::string_view text(start, current - start);

            // Check if the extracted identifier is actually a reserved Verilog keyword
            if (isKeyword(text))
                return {TokenType::Keyword, text};

            return {TokenType::Identifier, text};
        }

        // 6. Parse Multi-character Symbols (e.g., '<=', '==')
        if (current + 1 < end && (*current == '<' || *current == '=') && *(current + 1) == '=')
        {
            current += 2;
            return {TokenType::Symbol, std::string_view(start, 2)};
        }

        // 7. Parse Single-character Symbols (e.g., ';', '+', '(', ')')
        current++; // Consume the single character
        return {TokenType::Symbol, std::string_view(start, 1)};
    }
}

// ==========================================
// HELPER FUNCTIONS
// ==========================================

bool Lexer::isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool Lexer::isAlpha(char c)
{
    // Bitwise OR with 32 (0x20) converts uppercase ASCII letters to lowercase.
    // This allows a single range check ('a' to 'z') to validate both cases efficiently.
    // Also explicitly allows underscores ('_').
    return ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_';
}

bool Lexer::isSpace(char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

bool Lexer::isKeyword(std::string_view s)
{
    // Compares the extracted string against a list of supported Verilog keywords.
    return s == "module" || s == "endmodule" ||
           s == "input" || s == "output" || s == "inout" ||
           s == "reg" || s == "wire" ||
           s == "always" || s == "posedge" || s == "negedge" ||
           s == "begin" || s == "end" ||
           s == "if" || s == "else" ||
           s == "parameter" || s == "or" ||
           s == "case" || s == "endcase" || s == "default";
}