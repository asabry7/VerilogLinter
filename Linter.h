/**
 * @file Linter.h
 * @brief Static analysis linter for Verilog AST.
 *
 * Provides the VerilogStaticLinter class which traverses the parsed Verilog
 * Module AST to detect common hardware design mistakes such as combinational loops,
 * multi-driven registers, latch inference, and structural width mismatches.
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "Parser.h"

/**
 * @struct ExpressionResult
 * @brief Stores the static evaluation result of an expression to infer width and size.
 */
struct ExpressionResult
{
    std::optional<uint64_t> statically_evaluated_value; // Set if resolved to a constant
    uint32_t bit_width_size;                            // Inferred hardware size
};

/**
 * @class VerilogStaticLinter
 * @brief Analyzes AST and reports design rule violations.
 */
class VerilogStaticLinter
{
public:
    /**
     * @brief Performs static analysis on the parsed module.
     * @param verilog_module The top-level module AST.
     */
    void analyze_module(const Module &verilog_module);

    /**
     * @brief Outputs all accumulated warnings and errors to standard output.
     */
    void print_linter_report() const;

private:
    struct LinterContext
    {
        const AlwaysBlock *current_always_block = nullptr;
        bool in_combinational_always_block = false;
    } current_linter_context;

    std::vector<std::string> linter_violations;

    std::unordered_map<std::string_view, uint64_t> evaluated_parameter_values;
    std::unordered_map<std::string_view, uint32_t> evaluated_signal_widths;
    std::unordered_map<std::string_view, const AlwaysBlock *> register_driven_by_block;
    std::unordered_map<std::string_view, bool> is_register_written_to;

    std::vector<std::string_view> finite_state_machine_states;
    std::unordered_set<std::string_view> used_case_statement_items;

    ExpressionResult evaluate_expression_properties(const Expression &expression_node);
    void analyze_statement_for_violations(const Statement &statement_node);
    void check_for_unreachable_finite_state_machine_states();
    void check_for_uninitialized_registers();
    void report_violation(const std::string &violation_message);
};