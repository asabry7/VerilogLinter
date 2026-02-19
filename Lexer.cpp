#include "Lexer.h"

Lexer::Lexer(std::string_view src) : current(src.data()), end(src.data() + src.size()) {}

Token Lexer::next()
{
    while (true)
    {

        /* Skip White spaces */
        while (current < end && isSpace(*current))
        {
            current++;
        }

        /* Handle Comments: */
        if (*current == '/')
        {
            /* Single line comments */
            if (*(current + 1) == '/')
            {
                current += 2;

                while (current < end && *current != '\n')
                {
                    current++;
                }
            }
            /* Multi line comments */
            else if (*(current + 1) == '*')
            {
                current += 2;

                while (current < end && !(*current == '*' && *(current + 1) == '/'))
                {
                    current++;
                }
                current += 2;
            }

            continue;
        }
        if (current >= end)
        {
            return {TokenType::EndToken, {}};
        }

        const char *start = current;

        /* Handle Numbers */
        if (isNumeric(*current))
        {
            while (current < end && isNumeric(*current))
            {
                current++;
            }

            if (*current == '\'') // a single qutation '
            {
                current++;
            }

            while (current < end && isAlpha(*current))
                current++; // handle Size and Type (Hexa h, Decimal d, etc)
            while (current < end && isNumeric(*current))
            {
                current++; // handle bits that comes afterward
                if (*current == '_')
                    current++;
            }

            return {TokenType::Number, std::string_view(start, current - start)};
        }

        /* Handle Keywords and Identifiers*/
        if (isAlpha(*current))
        {
            while (current < end && isAlpha(*current))
            {
                current++;
            }

            auto substring = std::string_view(start, current - start);
            if (isKeyword(substring))
            {
                return {TokenType::Keyword, std::string_view(start, current - start)};
            }
            else
            {
                return {TokenType::Identifier, std::string_view(start, current - start)};
            }
        }

        /* Handle Symbols */
        if (*current == '>')
        {
            current++;
            if (current < end && *(current + 1) == '=')
                current++;
            return {TokenType::Symbol, std::string_view(start, current - start)};
        }
        else if (*current == '<')
        {
            current++;
            if (current < end && *(current + 1) == '=')
                current++;
            return {TokenType::Symbol, std::string_view(start, current - start)};
        }
        else if (*current == '!')
        {
            current++;
            if (current < end && *(current + 1) == '=')
                current++;
            return {TokenType::Symbol, std::string_view(start, current - start)};
        }
        else if (*current == '=')
        {
            current++;
            if (current < end && *(current + 1) == '=')
                current++;
            return {TokenType::Symbol, std::string_view(start, current - start)};
        }
        else if (*current == '+' | *current == '-' | *current == '*' | *current == '/' |
                 *current == '(' | *current == ')' | *current == '[' | *current == ']' |
                 *current == '#')
        {
            current++;
            return {TokenType::Symbol, std::string_view(start, current - start)};
        }

        /* Fallback: return Unknown */
        current++;
        return {TokenType::Unknown, std::string_view(start, current - start)};
    }
}

/* Helper Functions: */
inline bool Lexer::isNumeric(char character)
{
    return (character >= '0' && character <= '9');
}
inline bool Lexer::isAlpha(char character)
{
    /* character | 32 == converting to lower case */
    return ((character | 32) >= 'a' && (character | 32) <= 'z') || character == '_';
}
inline bool Lexer::isSpace(char character)
{
    return (character == ' ' | character == '\n' | character == '\t' | character == '\r');
}
bool Lexer::isKeyword(std::string_view s)
{
    switch (s.size())
    {
    case 2:
        return (s == "if");

    case 3:
        return (s == "reg" || s == "end");

    case 5:
        return (s == "input" || s == "begin");

    case 6:
        return (s == "module");

    case 8:
        return (s == "parameter");

    case 9:
        return (s == "endmodule");

    default:
        return false;
    }
}
