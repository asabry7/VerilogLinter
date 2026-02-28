/**
 * @file Parser.h
 * @brief Defines the Abstract Syntax Tree (AST) and the recursive descent Parser.
 *
 * This file contains the data structures for the Verilog AST, the arena memory
 * allocator, and the VerilogParser class used to convert tokens into a tree.
 * It also exports utility declarations for printing and number parsing.
 */

#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <variant>
#include <memory_resource>
#include <optional>

#include "Lexer.h"

// ==========================================
// UTILITIES FOR VISITORS & HELPERS
// ==========================================

/** Helper for std::visit overload resolution */
template <class... Types>
struct overloaded : Types...
{
    using Types::operator()...;
};
template <class... Types>
overloaded(Types...) -> overloaded<Types...>;

struct ConstantValue
{
    uint64_t numeric_value;
    uint32_t bit_width_size;
};

// ==========================================
// AST ARENA ALLOCATOR
// ==========================================
class AbstractSyntaxTreeArena
{
    std::pmr::monotonic_buffer_resource memory_pool;

public:
    std::pmr::polymorphic_allocator<std::byte> get_allocator()
    {
        return std::pmr::polymorphic_allocator<std::byte>(&memory_pool);
    }

    template <typename NodeType, typename... ConstructorArguments>
    NodeType *allocate_node(ConstructorArguments &&...arguments)
    {
        std::pmr::polymorphic_allocator<NodeType> type_allocator(&memory_pool);
        NodeType *allocated_pointer = type_allocator.allocate(1);
        type_allocator.construct(allocated_pointer, std::forward<ConstructorArguments>(arguments)...);
        return allocated_pointer;
    }
};

// ==========================================
// AST NODE DEFINITIONS
// ==========================================
struct BinaryExpression;
struct Identifier
{
    std::string_view identifier_name;
};
struct Number
{
    std::string_view numeric_value_string;
};

using Expression = std::variant<Identifier, Number, BinaryExpression *>;

struct BinaryExpression
{
    std::string_view operator_symbol;
    Expression left_expression;
    Expression right_expression;
};

struct Assignment;
struct IfStatement;
struct BlockStatement;
struct CaseStatement;

using Statement = std::variant<Assignment *, IfStatement *, BlockStatement *, CaseStatement *>;

struct Assignment
{
    Expression left_hand_side_expression;
    Expression right_hand_side_expression;
    bool is_blocking; // TRUE for '=', FALSE for '<='
};

struct IfStatement
{
    Expression condition_expression;
    Statement true_branch_statement;
    std::optional<Statement> false_branch_statement;
};

struct BlockStatement
{
    std::pmr::vector<Statement> contained_statements;
};

struct CaseStatement
{
    Expression condition_expression;
    std::pmr::vector<std::pair<Expression, Statement>> case_branches;
    std::optional<Statement> default_branch_statement;
};

enum class EdgeType
{
    None,
    PositiveEdge,
    NegativeEdge
};
struct Sensitivity
{
    EdgeType edge_trigger_type;
    Identifier signal_identifier;
};

struct AlwaysBlock
{
    std::pmr::vector<Sensitivity> sensitivity_list;
    Statement body_statement;
    bool is_combinational_block() const
    {
        for (const auto &sensitivity_item : sensitivity_list)
            if (sensitivity_item.edge_trigger_type != EdgeType::None)
                return false;
        return true;
    }
};

struct BitRange
{
    Expression most_significant_bit;
    Expression least_significant_bit;
};

struct SignalDeclaration
{
    bool is_register_type; // true for 'reg', false for 'wire'
    std::optional<BitRange> bit_range;
    std::pmr::vector<std::string_view> signal_names;
};

struct ContinuousAssignment
{
    Expression left_hand_side_expression;
    Expression right_hand_side_expression;
};

enum class PortDirection
{
    Input,
    Output,
    InOut
};

struct Port
{
    PortDirection direction;
    bool is_register_type;
    std::optional<BitRange> bit_range;
    std::string_view port_name;
};

struct Parameter
{
    std::string_view parameter_name;
    Expression default_value_expression;
};

using ModuleItem = std::variant<AlwaysBlock *, SignalDeclaration *, ContinuousAssignment *>;

struct Module
{
    std::string_view module_name;
    std::pmr::vector<Parameter> module_parameters;
    std::pmr::vector<Port> module_ports;
    std::pmr::vector<ModuleItem> module_items;
};

// ==========================================
// EXPORTED UTILITY FUNCTIONS (Implemented in utils.cpp)
// ==========================================
std::optional<ConstantValue> parse_verilog_number(std::string_view input_string);
void print_expression(const Expression &expression_node);
void print_statement(const Statement &statement_node, std::string indentation_string);

// ==========================================
// PARSER CLASS DECLARATION
// ==========================================
/**
 * @class VerilogParser
 * @brief Parses Verilog tokens into an Abstract Syntax Tree (AST).
 */
class VerilogParser
{
public:
    VerilogParser(std::string_view source_code, AbstractSyntaxTreeArena &syntax_tree_arena);
    Module parse_module_definition();

private:
    void advance_to_next_token();
    bool match_and_consume_token(TokenType expected_type, std::string_view expected_content = "");
    void expect_token_or_fail(TokenType expected_type, std::string_view expected_content = "");

    Port parse_port_definition();
    AlwaysBlock *parse_always_block_definition();
    Statement parse_statement_definition();

    // Recursive Descent Math Parsers
    Expression parse_primary();
    Expression parse_factor();
    Expression parse_term();
    Expression parse_expression();

    LexicalAnalyzer lexical_analyzer;
    AbstractSyntaxTreeArena &syntax_tree_arena;
    Token current_token;
};