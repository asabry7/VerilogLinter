#include <string_view>
#include <string>
#include <unordered_set>
#include <iostream>
#include <cstdint>

/* Enum for different token types */
enum class TokenType
{
    Identifier,
    Keyword,
    Number,
    Symbol,
    Unknown,
    EndToken
};

/* Struct representing each token */
struct Token
{
    TokenType type;
    std::string_view content;
};

/* The Lexer Class */

class Lexer
{
public:
    /* Contstructor */
    Lexer(std::string_view src);

    /* Main Logic */
    Token next();

private:
    /* 2 Pointers for code scanning */
    const char *current;
    const char *end;

    /* Helper Functions: */
    // static inline bool isNumeric(char character);
    bool isDigit(char c);
    static inline bool isAlpha(char character);
    static inline bool isSpace(char character);
    static bool isKeyword(std::string_view s);
};
