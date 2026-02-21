#include "Lexer.h"

Lexer::Lexer(std::string_view src) : current(src.data()), end(src.data() + src.size()) {}

Token Lexer::next()
{
    while (true)
    {
        if (current >= end)
            return {TokenType::EndToken, {}};

        while (current < end && isSpace(*current))
            current++;
        if (current >= end)
            return {TokenType::EndToken, {}};

        if (*current == '/' && current + 1 < end)
        {
            if (*(current + 1) == '/')
            {
                current += 2;
                while (current < end && *current != '\n')
                    current++;
                continue;
            }
            if (*(current + 1) == '*')
            {
                current += 2;
                while (current + 1 < end && !(*current == '*' && *(current + 1) == '/'))
                    current++;
                if (current + 1 < end)
                    current += 2;
                continue;
            }
        }

        const char *start = current;

        // Numbers (Supports formats like 8'h00)
        if (isDigit(*current))
        {
            while (current < end && (isDigit(*current) || isAlpha(*current) || *current == '\''))
                current++;
            return {TokenType::Number, std::string_view(start, current - start)};
        }

        if (isAlpha(*current))
        {
            while (current < end && (isAlpha(*current) || isDigit(*current)))
                current++;

            std::string_view text(start, current - start);
            if (isKeyword(text))
                return {TokenType::Keyword, text};
            return {TokenType::Identifier, text};
        }

        // Multi-character Symbols (like <=, ==)
        if (current + 1 < end && (*current == '<' || *current == '=') && *(current + 1) == '=')
        {
            current += 2;
            return {TokenType::Symbol, std::string_view(start, 2)};
        }

        current++;
        return {TokenType::Symbol, std::string_view(start, 1)};
    }
}

bool Lexer::isDigit(char c) { return c >= '0' && c <= '9'; }
bool Lexer::isAlpha(char c) { return ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_'; }
bool Lexer::isSpace(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }
bool Lexer::isKeyword(std::string_view s)
{
    return s == "module" || s == "endmodule" || s == "input" || s == "output" ||
           s == "inout" || s == "reg" || s == "wire" || s == "always" ||
           s == "posedge" || s == "negedge" || s == "begin" || s == "end" ||
           s == "if" || s == "else" || s == "parameter" || s == "or" ||
           s == "case" || s == "endcase" || s == "default";
}
