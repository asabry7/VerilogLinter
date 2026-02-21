#pragma once

#include <iostream>
#include <string_view>
#include <vector>
#include <variant>
#include <memory_resource>
#include <optional>

#include "Lexer.h"

/* Helpers to use std::visit */
template <class... Types>
struct overloaded : Types...
{
    using Types::operator()...;
};
template <class... Types>
overloaded(Types...) -> overloaded<Types...>;

/**
 * @brief A memory arena for fast, bump-pointer allocation of AST nodes.
 *
 * Backed by a @c std::pmr::monotonic_buffer_resource, all allocations are
 * made in O(1) time and freed together when the arena is destroyed. This
 * avoids the overhead of individual @c new / @c delete calls per node.
 *
 * Nodes allocated here must not be individually deleted; their lifetime is
 * tied to the arena.
 */
class AbstractSyntaxTreeArena
{
    std::pmr::monotonic_buffer_resource memory_pool;

public:
    /**
     * @brief Returns a polymorphic allocator backed by this arena.
     *
     * Suitable for constructing PMR-aware containers (e.g., @c std::pmr::vector)
     * whose memory is managed by this arena.
     *
     * @return A @c std::pmr::polymorphic_allocator<std::byte> tied to the internal pool.
     */
    std::pmr::polymorphic_allocator<std::byte> get_allocator()
    {
        return std::pmr::polymorphic_allocator<std::byte>(&memory_pool);
    }

    /**
     * @brief Allocates and constructs a single AST node of type @p NodeType.
     *
     * Memory is drawn from the internal monotonic pool. The node is
     * constructed in-place using perfect forwarding of the supplied arguments.
     *
     * @tparam NodeType             The AST node type to allocate.
     * @tparam ConstructorArguments Types of arguments forwarded to the constructor.
     * @param  arguments            Arguments forwarded to @p NodeType's constructor.
     * @return A non-owning pointer to the newly constructed node.
     *         The pointer remains valid until the arena is destroyed.
     */
    template <typename NodeType, typename... ConstructorArguments>
    NodeType *allocate_node(ConstructorArguments &&...arguments)
    {
        std::pmr::polymorphic_allocator<NodeType> type_allocator(&memory_pool);
        NodeType *allocated_pointer = type_allocator.allocate(1);
        type_allocator.construct(allocated_pointer, std::forward<ConstructorArguments>(arguments)...);
        return allocated_pointer;
    }
};

// Forward declarations required for recursive variant and pointer members.
struct BinaryExpression;
struct NonBlockingAssignment;
struct IfStatement;
struct BlockStatement;
struct CaseStatement;
struct AlwaysBlock;

// ==========================================
// 2. AST NODE TYPES — EXPRESSIONS
// ==========================================

/**
 * @brief AST leaf node representing a named identifier (e.g., a signal or variable name).
 */
struct Identifier
{
    std::string_view identifier_name; ///< The raw text of the identifier in the source.
};

/**
 * @brief AST leaf node representing a numeric literal (e.g., `8'hFF`, `0`, `1`).
 */
struct Number
{
    std::string_view numeric_value_string; ///< The raw text of the numeric literal in the source.
};

/**
 * @brief A Verilog expression: either an identifier, a number, or a binary expression.
 *
 * @c BinaryExpression is stored as a pointer to break the recursive type definition
 * and to allow allocation inside the @ref AbstractSyntaxTreeArena.
 */
using Expression = std::variant<Identifier, Number, BinaryExpression *>;

/**
 * @brief AST node for a binary operation (e.g., `a + b`, `x & y`).
 */
struct BinaryExpression
{
    std::string_view operator_symbol; ///< The operator token text (e.g., `"+"`, `"&"`).
    Expression left_expression;       ///< The left-hand operand.
    Expression right_expression;      ///< The right-hand operand.
};

// ==========================================
// 2. AST NODE TYPES — STATEMENTS
// ==========================================

/**
 * @brief A Verilog statement: one of non-blocking assignment, if, block, or case.
 *
 * All variants are stored as pointers and must be allocated via @ref AbstractSyntaxTreeArena.
 */
using Statement = std::variant<NonBlockingAssignment *, IfStatement *, BlockStatement *, CaseStatement *>;

/**
 * @brief AST node for a non-blocking assignment (`lhs <= rhs`).
 */
struct NonBlockingAssignment
{
    Expression left_hand_side_expression;  ///< The target (left-hand side) of the assignment.
    Expression right_hand_side_expression; ///< The value (right-hand side) being assigned.
};

/**
 * @brief AST node for an `if`/`else` statement.
 */
struct IfStatement
{
    Expression condition_expression;                 ///< The boolean condition being tested.
    Statement true_branch_statement;                 ///< The statement executed when condition is true.
    std::optional<Statement> false_branch_statement; ///< The optional `else` branch statement.
};

/**
 * @brief AST node for a sequential `begin`/`end` block containing multiple statements.
 */
struct BlockStatement
{
    std::pmr::vector<Statement> contained_statements; ///< Ordered list of statements inside the block.
};

/**
 * @brief AST node for a `case` statement.
 */
struct CaseStatement
{
    Expression condition_expression;                                  ///< The expression being switched on.
    std::pmr::vector<std::pair<Expression, Statement>> case_branches; ///< Ordered list of (value, statement) arms.
    std::optional<Statement> default_branch_statement;                ///< The optional `default` branch.
};

// ==========================================
// 3. AST NODE TYPES — MODULE STRUCTURE
// ==========================================

/**
 * @brief Describes the edge sensitivity of a signal in an always block sensitivity list.
 */
enum class EdgeType
{
    None,         ///< Level-sensitive (used in combinational `always @(*)` blocks).
    PositiveEdge, ///< Rising-edge sensitive (`posedge`).
    NegativeEdge  ///< Falling-edge sensitive (`negedge`).
};

/**
 * @brief A single entry in an always block sensitivity list (e.g., `posedge clk`).
 */
struct Sensitivity
{
    EdgeType edge_trigger_type;   ///< The edge type triggering this sensitivity entry.
    Identifier signal_identifier; ///< The signal being watched.
};

/**
 * @brief AST node representing a Verilog `always` block.
 */
struct AlwaysBlock
{
    std::pmr::vector<Sensitivity> sensitivity_list; ///< The list of sensitivity entries (the `@(...)` clause).
    Statement body_statement;                       ///< The statement forming the body of the block.

    /**
     * @brief Determines whether this block is purely combinational.
     *
     * A block is considered combinational if every entry in the sensitivity
     * list has @c EdgeType::None (i.e., no posedge/negedge triggers).
     *
     * @return @c true if all sensitivities are level-sensitive, @c false otherwise.
     */
    bool is_combinational_block() const
    {
        for (const auto &sensitivity_item : sensitivity_list)
            if (sensitivity_item.edge_trigger_type != EdgeType::None)
                return false;
        return true;
    }
};

/**
 * @brief Describes an inclusive bit-range slice (e.g., `[7:0]`).
 */
struct BitRange
{
    Expression most_significant_bit;  ///< The upper (MSB) bound expression.
    Expression least_significant_bit; ///< The lower (LSB) bound expression.
};

/**
 * @brief The direction of a module port.
 */
enum class PortDirection
{
    Input,  ///< An input port (`input`).
    Output, ///< An output port (`output`).
    InOut   ///< A bidirectional port (`inout`).
};

/**
 * @brief AST node describing a single module port declaration.
 */
struct Port
{
    PortDirection direction;           ///< Whether the port is input, output, or inout.
    bool is_register_type;             ///< @c true if the port is declared as `reg`.
    std::optional<BitRange> bit_range; ///< Optional bit-width range (e.g., `[7:0]`); absent for scalar ports.
    std::string_view port_name;        ///< The name of the port as it appears in the source.
};

/**
 * @brief AST node describing a module parameter declaration (e.g., `parameter WIDTH = 8`).
 */
struct Parameter
{
    std::string_view parameter_name;     ///< The name of the parameter.
    Expression default_value_expression; ///< The default value assigned to the parameter.
};

/**
 * @brief A top-level item inside a module body.
 *
 * Currently only `always` blocks are supported as module-level items.
 */
using ModuleItem = std::variant<AlwaysBlock *>;

/**
 * @brief AST node representing a complete Verilog module definition.
 */
struct Module
{
    std::string_view module_name;                  ///< The declared name of the module.
    std::pmr::vector<Parameter> module_parameters; ///< List of `parameter` declarations.
    std::pmr::vector<Port> module_ports;           ///< List of port declarations.
    std::pmr::vector<ModuleItem> module_items;     ///< List of top-level items in the module body.
};

// ==========================================
// 4. RECURSIVE DESCENT PARSER
// ==========================================

/**
 * @brief A recursive-descent parser for a subset of the Verilog HDL.
 *
 * Consumes tokens from a @ref LexicalAnalyzer and builds an AST whose nodes
 * are allocated inside a shared @ref AbstractSyntaxTreeArena. The parser
 * exposes a single public entry point, @ref parse_module_definition(), which
 * returns a fully populated @ref Module.
 *
 * Typical usage:
 * @code
 * std::string src = load_file("top.v");
 * AbstractSyntaxTreeArena arena;
 * VerilogParser parser(src, arena);
 * Module mod = parser.parse_module_definition();
 * @endcode
 */
class VerilogParser
{
    LexicalAnalyzer lexical_analyzer;           ///< The underlying token stream.
    AbstractSyntaxTreeArena &syntax_tree_arena; ///< Arena used to allocate all AST nodes.
    Token current_token;                        ///< The most recently consumed token (lookahead).

    /**
     * @brief Advances @ref current_token to the next token from the lexer.
     */
    void advance_to_next_token();

    /**
     * @brief Checks whether the current token matches the given type and content,
     *        and if so, advances past it.
     *
     * @param expected_type    The @ref TokenType the current token must have.
     * @param expected_content The exact text the current token must contain.
     * @return @c true if the token matched and was consumed, @c false otherwise.
     */
    bool match_and_consume_token(TokenType expected_type, std::string_view expected_content);

    /**
     * @brief Asserts that the current token matches the given type and content,
     *        then advances past it. Terminates (or throws) on mismatch.
     *
     * @param expected_type    The @ref TokenType the current token must have.
     * @param expected_content The exact text the current token must contain.
     */
    void expect_token_or_fail(TokenType expected_type, std::string_view expected_content);

public:
    /**
     * @brief Constructs a VerilogParser for the given source code.
     *
     * Initializes the internal lexer and reads the first lookahead token.
     *
     * @param source_code       A view of the Verilog source text to parse.
     *                          Must remain valid for the lifetime of this object.
     * @param syntax_tree_arena The arena into which all AST nodes will be allocated.
     */
    VerilogParser(std::string_view source_code, AbstractSyntaxTreeArena &syntax_tree_arena);

    /**
     * @brief Parses a complete Verilog `module` ... `endmodule` definition.
     *
     * This is the top-level entry point of the parser. It consumes the entire
     * module declaration including its parameter list, port list, and body items.
     *
     * @return A @ref Module struct populated with the parsed AST data.
     */
    Module parse_module_definition();

private:
    /**
     * @brief Parses a single port declaration (direction, type, optional range, and name).
     *
     * @return A @ref Port struct describing the parsed port.
     */
    Port parse_port_definition();

    /**
     * @brief Parses an `always` block, including its sensitivity list and body.
     *
     * The returned node is allocated inside @ref syntax_tree_arena.
     *
     * @return A pointer to the newly allocated @ref AlwaysBlock node.
     */
    AlwaysBlock *parse_always_block_definition();

    /**
     * @brief Parses a single statement (assignment, if, begin/end block, or case).
     *
     * Dispatches to the appropriate sub-parser based on the current token.
     *
     * @return A @ref Statement variant holding the parsed statement node.
     */
    Statement parse_statement_definition();

    /**
     * @brief Parses a single expression (identifier, number literal, or binary expression).
     *
     * Handles operator precedence by recursive or iterative sub-parsing as needed.
     *
     * @return An @ref Expression variant holding the parsed expression.
     */
    Expression parse_expression();
};