#pragma once

#include "Parser.h"
#include <unordered_map>

/* Helpers for number parsing: */
#include <charconv>
#include <algorithm> // for std::max

/**
 * @brief Holds the result of statically evaluating a Verilog expression.
 *
 * Every expression has an inferred hardware bit-width (@p bit_width_size).
 * If the expression reduces to a compile-time constant (a literal or a
 * resolved parameter), @p statically_evaluated_value is also populated.
 */
struct ExpressionResult
{
    std::optional<uint64_t> statically_evaluated_value; ///< Compile-time constant value, if known.
    uint32_t bit_width_size;                            ///< Inferred hardware bit-width; always present.
};

/**
 * @brief A fully resolved constant: a numeric value paired with its bit-width.
 *
 * Used internally to represent parameter values and numeric literals after
 * they have been evaluated to a concrete integer.
 */
struct ConstantValue
{
    uint64_t numeric_value;  ///< The resolved integer value.
    uint32_t bit_width_size; ///< The bit-width of the constant.
};

/**
 * @brief A static linter that checks a parsed Verilog module for common design violations.
 *
 * After a @ref Module has been produced by @ref VerilogParser, pass it to
 * @ref analyze_module() to run all lint checks. Any violations found are
 * collected internally and can be printed with @ref print_linter_report().
 *
 * Checks performed include (but are not limited to):
 * - Unreachable FSM states in `case` statements.
 * - Registers that are never initialized / written to.
 * - Width mismatches in expressions and assignments.
 *
 * Typical usage:
 * @code
 * VerilogStaticLinter linter;
 * linter.analyze_module(parsed_module);
 * linter.print_linter_report();
 * @endcode
 */
class VerilogStaticLinter
{
    /**
     * @brief Transient state that is valid only during the analysis of a single always block.
     */
    struct LinterContext
    {
        const AlwaysBlock *current_always_block = nullptr; ///< The always block currently being analyzed, or @c nullptr if none.
        bool in_combinational_always_block = false;        ///< @c true if the current block is combinational (no edge triggers).
    } current_linter_context;                              ///< Active linting context, reset for each always block.

    std::vector<std::string> linter_violations; ///< Accumulated human-readable violation messages.

    std::unordered_map<std::string_view, uint64_t> evaluated_parameter_values; ///< Maps parameter names to their resolved constant values.
    std::unordered_map<std::string_view, uint32_t> evaluated_signal_widths;    ///< Maps signal names to their inferred bit-widths.

    std::unordered_map<std::string_view, const AlwaysBlock *> register_driven_by_block; ///< Maps each register name to the always block that drives it.
    std::unordered_map<std::string_view, bool> is_register_written_to;                  ///< Tracks whether each register has been assigned at least once.
    std::vector<std::string_view> finite_state_machine_states;                          ///< Ordered list of FSM state identifiers discovered during analysis.
    std::unordered_set<std::string_view> used_case_statement_items;                     ///< Set of case-expression values that appear in at least one case arm.

    /**
     * @brief Records a violation message in the internal violations list.
     *
     * @param violation_message A human-readable description of the violation.
     */
    void report_violation(const std::string &violation_message);

public:
    /**
     * @brief Runs all lint checks over a parsed Verilog module.
     *
     * Iterates over the module's ports, parameters, and body items, populating
     * internal lookup tables and invoking each individual check. Results are
     * stored internally and retrievable via @ref print_linter_report().
     *
     * @param verilog_module The fully parsed module to analyze.
     */
    void analyze_module(const Module &verilog_module);

    /**
     * @brief Prints all collected violation messages to standard output.
     *
     * Outputs a summary line followed by each violation on its own line.
     * If no violations were found, prints a clean-report message instead.
     */
    void print_linter_report() const;

private:
    /**
     * @brief Evaluates the static properties (value and bit-width) of an expression node.
     *
     * Recursively resolves identifiers against @ref evaluated_parameter_values and
     * @ref evaluated_signal_widths, computes bit-widths for binary expressions,
     * and parses numeric literals.
     *
     * @param expression_node The expression to evaluate.
     * @return An @ref ExpressionResult containing the inferred bit-width and,
     *         if statically determinable, the constant value.
     */
    ExpressionResult evaluate_expression_properties(const Expression &expression_node);

    /**
     * @brief Recursively analyzes a statement and its sub-statements for violations.
     *
     * Dispatches to the appropriate handler based on the statement variant type
     * (non-blocking assignment, if, begin/end block, or case). Updates internal
     * tracking state (e.g., @ref is_register_written_to) as assignments are encountered.
     *
     * @param statement_node The statement to analyze.
     */
    void analyze_statement_for_violations(const Statement &statement_node);

    /**
     * @brief Checks for FSM states that appear in the state register but never in any case arm.
     *
     * Compares @ref finite_state_machine_states against @ref used_case_statement_items
     * and reports a violation for each state that is never matched by a case branch.
     */
    void check_for_unreachable_finite_state_machine_states();

    /**
     * @brief Checks for registers that are declared but never assigned anywhere in the module.
     *
     * Iterates over @ref is_register_written_to and reports a violation for every
     * register whose written flag remains @c false after full analysis.
     */
    void check_for_uninitialized_registers();
};