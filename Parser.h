#pragma once

#include <iostream>
#include <string_view>
#include <vector>
#include <variant>
#include <memory_resource>
#include <optional>

#include "Lexer.h"

// ==========================================
// 1. UTILITIES
// ==========================================

/**
 * @brief Helper structure to enable easy lambda-based visitation for std::visit.
 *
 * Allows passing multiple lambda expressions to handle different types
 * inside a std::variant.
 */
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

// Deduction guide for the overloaded struct (Requires C++17)
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// ==========================================
// 2. MEMORY MANAGEMENT
// ==========================================

/**
 * @brief Memory arena for Abstract Syntax Tree (AST) node allocations.
 *
 * Uses std::pmr::monotonic_buffer_resource to provide fast, contiguous memory
 * allocations. Memory is automatically reclaimed when the arena is destroyed,
 * eliminating the need for manual memory management (e.g., delete) for AST nodes.
 */
class AstArena
{
    std::pmr::monotonic_buffer_resource memory_pool;

public:
    /**
     * @brief Get a polymorphic allocator tied to this arena.
     * @return std::pmr::polymorphic_allocator<std::byte>
     */
    std::pmr::polymorphic_allocator<std::byte> allocator()
    {
        return std::pmr::polymorphic_allocator<std::byte>(&memory_pool);
    }

    /**
     * @brief Allocates and constructs an object of type T in the arena.
     *
     * @tparam T The type of object to allocate.
     * @tparam Args The types of arguments for T's constructor.
     * @param args Arguments forwarded to T's constructor.
     * @return T* Pointer to the newly allocated and constructed object.
     */
    template <typename T, typename... Args>
    T *alloc(Args &&...args)
    {
        std::pmr::polymorphic_allocator<T> alloc(&memory_pool);
        T *ptr = alloc.allocate(1);
        alloc.construct(ptr, std::forward<Args>(args)...);
        return ptr;
    }
};

// ==========================================
// 3. AST FORWARD DECLARATIONS
// ==========================================
// Required to define recursive or pointer-based variant types (Expressions, Statements).

struct BinaryExpr;
struct NonBlockingAssign;
struct IfStmt;
struct BlockStmt;
struct CaseStmt;
struct AlwaysBlock;

// ==========================================
// 4. AST BASIC TYPES & TERMINALS
// ==========================================

/// @brief Represents an identifier (e.g., variable, wire, or port name).
struct Identifier
{
    std::string_view name;
};

/// @brief Represents a numeric literal.
struct Number
{
    std::string_view value;
};

/// @brief Represents a bit-range slice (e.g., [msb:lsb]).
struct Range
{
    // Note: Expression variant is defined below, but C++ allows this structurally
    // since Range is only fully utilized later.
    // To be strictly safe, we will define `Expression` right after terminals.
};

// ==========================================
// 5. AST EXPRESSIONS
// ==========================================

/**
 * @brief An Expression can be an Identifier, a Number, or a pointer to a Binary Expression.
 */
using Expression = std::variant<Identifier, Number, BinaryExpr *>;

/// @brief Redefinition of Range with the complete Expression type available.
struct RangeImpl // Renamed conceptually, but mapped structurally below
{
    Expression msb;
    Expression lsb;
};
// Overriding Range forward declaration structure
struct Range
{
    Expression msb;
    Expression lsb;
};

/// @brief Represents a binary operation (e.g., a + b, a == b).
struct BinaryExpr
{
    std::string_view op; ///< The binary operator (e.g., "+", "-", "==")
    Expression left;     ///< Left hand operand
    Expression right;    ///< Right hand operand
};

// ==========================================
// 6. AST STATEMENTS
// ==========================================

/**
 * @brief A Statement represents an executable instruction within a block.
 */
using Statement = std::variant<NonBlockingAssign *, IfStmt *, BlockStmt *, CaseStmt *>;

/// @brief Represents a Verilog non-blocking assignment (lhs <= rhs;).
struct NonBlockingAssign
{
    Expression lhs; ///< Target of the assignment
    Expression rhs; ///< Value being assigned
};

/// @brief Represents an if-else statement.
struct IfStmt
{
    Expression condition;                  ///< The condition to evaluate
    Statement true_branch;                 ///< Executed if condition is true
    std::optional<Statement> false_branch; ///< Executed if condition is false (optional)
};

/// @brief Represents a begin...end block containing multiple statements.
struct BlockStmt
{
    std::pmr::vector<Statement> statements; ///< List of statements inside the block
};

/// @brief Represents a Verilog case statement.
struct CaseStmt
{
    Expression condition;                                        ///< The variable/expression being evaluated
    std::pmr::vector<std::pair<Expression, Statement>> branches; ///< Case item branches mapping expression -> statement
    std::optional<Statement> default_branch;                     ///< Optional default branch
};

// ==========================================
// 7. AST MODULE & HARDWARE CONSTRUCTS
// ==========================================

/// @brief Clock edge types for sensitivity lists.
enum class Edge
{
    None,    ///< Combinatorial (e.g., *)
    Posedge, ///< Rising edge
    Negedge  ///< Falling edge
};

/// @brief Represents a single signal in an always block's sensitivity list.
struct Sensitivity
{
    Edge edge;         ///< The trigger edge (if any)
    Identifier signal; ///< The signal name being monitored
};

/// @brief Represents a Verilog always block.
struct AlwaysBlock
{
    std::pmr::vector<Sensitivity> sensitivities; ///< Sensitivity list (e.g., @(posedge clk))
    Statement body;                              ///< The block of code executed when triggered

    /**
     * @brief Checks if the always block represents purely combinatorial logic.
     * @return true if no edges (posedge/negedge) are specified.
     */
    bool is_combinatorial() const
    {
        for (const auto &s : sensitivities)
        {
            if (s.edge != Edge::None)
                return false;
        }
        return true;
    }
};

/// @brief Port direction for a hardware module.
enum class PortDir
{
    Input,
    Output,
    Inout
};

/// @brief Represents a module port definition.
struct Port
{
    PortDir direction;          ///< Input, Output, or Inout
    bool is_reg;                ///< True if declared as a register type
    std::optional<Range> range; ///< Bit width/range (if provided)
    std::string_view name;      ///< Name of the port
};

/// @brief Represents a parameter definition inside a module.
struct Parameter
{
    std::string_view name;    ///< Name of the parameter
    Expression default_value; ///< Default assigned expression
};

/**
 * @brief High-level module item. Currently supports Always Blocks.
 */
using ModuleItem = std::variant<AlwaysBlock *>;

/// @brief Represents a complete Hardware Module.
struct Module
{
    std::string_view name;                  ///< Module identifier name
    std::pmr::vector<Parameter> parameters; ///< List of parameters (e.g., #(...))
    std::pmr::vector<Port> ports;           ///< List of I/O ports
    std::pmr::vector<ModuleItem> items;     ///< Internal structural items (always blocks, etc.)
};

// ==========================================
// 8. PARSER
// ==========================================

/**
 * @brief Recursive descent parser for converting token streams into an AST.
 */
class Parser
{
    Lexer lexer;         ///< The lexical analyzer supplying tokens
    AstArena &arena;     ///< Reference to the memory arena for AST node allocation
    Token current_token; ///< The token currently being analyzed

    /**
     * @brief Consumes the current token and fetches the next one from the lexer.
     */
    void advance()
    {
        current_token = lexer.next();
    }

    /**
     * @brief Checks if the current token matches the given type (and optionally content).
     *        Advances to the next token if it matches.
     *
     * @param type The expected token type.
     * @param content The expected string content (optional).
     * @return true if matched and advanced, false otherwise.
     */
    bool match(TokenType type, std::string_view content = "")
    {
        if (current_token.type == type && (content.empty() || current_token.content == content))
        {
            advance();
            return true;
        }
        return false;
    }

    /**
     * @brief Enforces that the current token matches the given type/content.
     *        Aborts the program with a syntax error message if it fails.
     *
     * @param type The expected token type.
     * @param content The expected string content.
     */
    void expect(TokenType type, std::string_view content = "")
    {
        if (!match(type, content))
        {
            std::cerr << "Syntax Error: Expected '" << content
                      << "' but got '" << current_token.content << "'\n";
            exit(1);
        }
    }

public:
    /**
     * @brief Construct a new Parser object.
     *
     * @param src The source code string to be parsed.
     * @param arena The AST arena memory allocator.
     */
    Parser(std::string_view src, AstArena &arena);

    /**
     * @brief Parses the entire hardware module from the source token stream.
     * @return Module The root node of the constructed AST.
     */
    Module parseModule();

private:
    // Internal parsing routines corresponding to grammar rules

    Port parsePort();
    AlwaysBlock *parseAlwaysBlock();
    Statement parseStatement();
    Expression parseExpression();
};