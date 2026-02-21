#include "Linter.h"
#include <charconv>

// ── NUMBER PARSER ────────────────────────────────────────────────────────────

// Parses Verilog numeric literals into a ConstantValue (integer + bit-width).
// Handles three formats:
//   - Plain decimal:        "255"
//   - Sized with base:      "8'hFF", "4'b1010", "16'd255", "8'o17"
//   - Underscore-separated: "8'b1010_0011"
// Returns nullopt for malformed input or for values containing 'x'/'z'.
std::optional<ConstantValue> parse_verilog_number(std::string_view input_string)
{
    if (input_string.empty())
        return std::nullopt;

    uint32_t parsed_bit_width = 32;                    // Verilog default width when no size is specified
    int numeric_base = 10;                             // Assume decimal unless a base character says otherwise
    std::string_view value_string_part = input_string; // Will be narrowed to just the digits part

    // ── Sized literal detection: look for the apostrophe separator ──
    size_t tick_character_position = input_string.find('\'');
    if (tick_character_position != std::string_view::npos)
    {
        // Everything before the tick is the bit-width (e.g. "8" in "8'hFF").
        if (tick_character_position > 0)
            std::from_chars(input_string.data(),
                            input_string.data() + tick_character_position,
                            parsed_bit_width);

        // Guard against a trailing tick with nothing after it.
        if (tick_character_position + 1 >= input_string.size())
            return std::nullopt;

        // The character immediately after the tick specifies the base.
        char base_character = input_string[tick_character_position + 1];

        // The actual digit string starts two characters after the tick.
        value_string_part = input_string.substr(tick_character_position + 2);

        // Map the base character to the numeric base.
        if (base_character == 'h' || base_character == 'H')
            numeric_base = 16;
        else if (base_character == 'b' || base_character == 'B')
            numeric_base = 2;
        else if (base_character == 'o' || base_character == 'O')
            numeric_base = 8;
        else if (base_character == 'd' || base_character == 'D')
            numeric_base = 10;
    }

    // ── Strip underscores used as visual separators (e.g. 8'b1010_0011) ──
    std::string cleaned_value_string;
    for (char current_character : value_string_part)
        if (current_character != '_')
            cleaned_value_string.push_back(current_character);

    uint64_t final_numeric_value = 0;

    if (numeric_base == 2)
    {
        // std::from_chars does not support base-2 in all C++17 standard library
        // implementations, so we parse binary manually with a left-shift accumulator.
        for (char binary_character : cleaned_value_string)
        {
            if (binary_character == '0' || binary_character == '1')
                final_numeric_value = (final_numeric_value << 1) | (binary_character - '0');
            else
                // 'x' (don't-care) and 'z' (high-impedance) cannot be folded into
                // a concrete integer, so we bail out.
                return std::nullopt;
        }
    }
    else
    {
        // For bases 8, 10, and 16 we can delegate to from_chars.
        auto [character_pointer, error_code] =
            std::from_chars(cleaned_value_string.data(),
                            cleaned_value_string.data() + cleaned_value_string.size(),
                            final_numeric_value,
                            numeric_base);
        if (error_code != std::errc()) // conversion failed (bad chars, overflow, etc.)
            return std::nullopt;
    }

    return ConstantValue{final_numeric_value, parsed_bit_width};
}

// ── VIOLATION REPORTING ──────────────────────────────────────────────────────

// Appends a human-readable violation message to the internal list.
// All checks funnel through here so reporting is centralised.
void VerilogStaticLinter::report_violation(const std::string &violation_message)
{
    linter_violations.push_back(violation_message);
}

// ── MODULE ANALYSIS ──────────────────────────────────────────────────────────

// Entry point for the linter. Analyses the module in four passes:
//   1. Resolve parameter values (needed for width calculations).
//   2. Determine port bit-widths and identify output registers.
//   3. Walk every always block and check each statement.
//   4. Run post-pass checks (FSM states, uninitialized registers).
void VerilogStaticLinter::analyze_module(const Module &verilog_module)
{
    // ── Pass 1: Parameters ────────────────────────────────────────────────
    // Parameters are recorded as potential FSM state names AND resolved to
    // numeric values so subsequent width calculations can use them.
    for (const auto &parameter_item : verilog_module.module_parameters)
    {
        // Any parameter could be an FSM state constant — record it for later.
        finite_state_machine_states.push_back(parameter_item.parameter_name);

        // Try to fold the default value to a compile-time constant.
        auto expression_result = evaluate_expression_properties(parameter_item.default_value_expression);
        if (expression_result.statically_evaluated_value)
            evaluated_parameter_values[parameter_item.parameter_name] =
                *expression_result.statically_evaluated_value;
    }

    // ── Pass 2: Port widths and register tracking ─────────────────────────
    for (const auto &port_item : verilog_module.module_ports)
    {
        uint32_t evaluated_width = 1; // Scalar (1-bit) unless a range is present

        if (port_item.bit_range)
        {
            // Evaluate both bounds; they may reference parameters resolved above.
            auto msb_result = evaluate_expression_properties(port_item.bit_range->most_significant_bit);
            auto lsb_result = evaluate_expression_properties(port_item.bit_range->least_significant_bit);

            // Width = (MSB - LSB) + 1; only computable when both bounds are constants.
            if (msb_result.statically_evaluated_value && lsb_result.statically_evaluated_value)
                evaluated_width = (*msb_result.statically_evaluated_value -
                                   *lsb_result.statically_evaluated_value) +
                                  1;
        }
        evaluated_signal_widths[port_item.port_name] = evaluated_width;

        // Track output regs so we can detect ones that are never driven.
        if (port_item.is_register_type && port_item.direction == PortDirection::Output)
            is_register_written_to[port_item.port_name] = false; // start as undriven
    }

    // ── Pass 3: Always block analysis ────────────────────────────────────
    for (const auto &module_item : verilog_module.module_items)
    {
        std::visit(overloaded{[this](const AlwaysBlock *always_block)
                              {
                                  // Update context so inner checks know which block they're inside.
                                  current_linter_context.current_always_block = always_block;
                                  current_linter_context.in_combinational_always_block = always_block->is_combinational_block();

                                  analyze_statement_for_violations(always_block->body_statement);

                                  // Reset context once we leave this block.
                                  current_linter_context.current_always_block = nullptr;
                              }},
                   module_item);
    }

    // ── Pass 4: Post-analysis checks ─────────────────────────────────────
    check_for_unreachable_finite_state_machine_states();
    check_for_uninitialized_registers();
}

// ── REPORT PRINTING ──────────────────────────────────────────────────────────

void VerilogStaticLinter::print_linter_report() const
{
    std::cout << "\n====================================\n";
    std::cout << "        LINTER VIOLATION REPORT       \n";
    std::cout << "====================================\n";

    if (linter_violations.empty())
        std::cout << "  No violations found. Clean code!\n";
    else
    {
        // Print each violation with a 1-based index for easy reference.
        for (size_t index = 0; index < linter_violations.size(); index++)
            std::cout << " [" << (index + 1) << "] " << linter_violations[index] << "\n";
    }
    std::cout << "====================================\n\n";
}

// ── EXPRESSION EVALUATOR ─────────────────────────────────────────────────────

// Recursively evaluates an expression node, returning:
//   - statically_evaluated_value : the folded constant, if determinable
//   - bit_width_size             : the inferred hardware width (always present)
//
// The three variant arms handle identifiers, number literals, and binary ops.
ExpressionResult VerilogStaticLinter::evaluate_expression_properties(const Expression &expression_node)
{
    return std::visit(overloaded{

                          // ── Identifier: look up in parameter or signal tables ──
                          [this](const Identifier &identifier_literal) -> ExpressionResult
                          {
                              // If this name is a known parameter, return its constant value.
                              if (evaluated_parameter_values.count(identifier_literal.identifier_name))
                                  return ExpressionResult{
                                      evaluated_parameter_values[identifier_literal.identifier_name], 32};

                              // If it's a known signal, return its hardware width (value unknown at compile time).
                              if (evaluated_signal_widths.count(identifier_literal.identifier_name))
                                  return ExpressionResult{
                                      std::nullopt,
                                      evaluated_signal_widths[identifier_literal.identifier_name]};

                              // Unknown identifier — assume 32-bit, value unknown.
                              return ExpressionResult{std::nullopt, 32};
                          },

                          // ── Number literal: parse and return concrete value + width ──
                          [this](const Number &number_literal) -> ExpressionResult
                          {
                              auto parsed_constant_value = parse_verilog_number(number_literal.numeric_value_string);
                              if (parsed_constant_value)
                                  return ExpressionResult{parsed_constant_value->numeric_value,
                                                          parsed_constant_value->bit_width_size};
                              // Unparseable literal (e.g. contains 'x'/'z') — fall back to 32-bit unknown.
                              return ExpressionResult{std::nullopt, 32};
                          },

                          // ── Binary expression: evaluate both sides, then fold and check ──
                          [this](const BinaryExpression *binary_expression) -> ExpressionResult
                          {
                              auto left_result = evaluate_expression_properties(binary_expression->left_expression);
                              auto right_result = evaluate_expression_properties(binary_expression->right_expression);

                              // The operand width is the wider of the two sides (Verilog extension rules).
                              uint32_t operand_width = std::max(left_result.bit_width_size, right_result.bit_width_size);
                              uint32_t result_width = operand_width;

                              // Addition and subtraction can produce a carry/borrow, so the result
                              // is one bit wider than the wider operand.
                              if (binary_expression->operator_symbol == "+" ||
                                  binary_expression->operator_symbol == "-")
                                  result_width = operand_width + 1;
                              // Multiplication output width is the sum of both operand widths.
                              else if (binary_expression->operator_symbol == "*")
                                  result_width = left_result.bit_width_size +
                                                 right_result.bit_width_size;
                              // Equality comparison always produces a single-bit boolean result.
                              else if (binary_expression->operator_symbol == "==")
                                  result_width = 1;

                              // ── Constant folding: only possible when both operands are known ──
                              if (left_result.statically_evaluated_value && right_result.statically_evaluated_value)
                              {
                                  // The maximum representable value in `operand_width` bits
                                  // (guard against shifting by 64 which is UB).
                                  uint64_t max_value = (operand_width >= 64) ? ~0ULL : ((1ULL << operand_width) - 1);

                                  if (binary_expression->operator_symbol == "+")
                                  {
                                      // Detect overflow before computing the sum.
                                      if (*left_result.statically_evaluated_value >
                                          max_value - *right_result.statically_evaluated_value)
                                          report_violation("Constant Math Overflow: " +
                                                           std::to_string(*left_result.statically_evaluated_value) +
                                                           " + " +
                                                           std::to_string(*right_result.statically_evaluated_value));

                                      // Mask the result to the declared result width.
                                      uint64_t sum = (*left_result.statically_evaluated_value +
                                                      *right_result.statically_evaluated_value) &
                                                     ((1ULL << result_width) - 1);
                                      return ExpressionResult{sum, result_width};
                                  }
                                  else if (binary_expression->operator_symbol == "-")
                                  {
                                      // Subtraction: mask to result width (underflow wraps, which is
                                      // intentional two's-complement behaviour in hardware).
                                      uint64_t diff = (*left_result.statically_evaluated_value -
                                                       *right_result.statically_evaluated_value) &
                                                      ((1ULL << result_width) - 1);
                                      return ExpressionResult{diff, result_width};
                                  }
                              }

                              // At least one operand is non-constant — return width only.
                              return ExpressionResult{std::nullopt, result_width};
                          }

                      },
                      expression_node);
}

// ── STATEMENT CHECKER ────────────────────────────────────────────────────────

// Recursively walks a statement node and fires any relevant lint checks.
// Each variant arm handles one statement kind.
void VerilogStaticLinter::analyze_statement_for_violations(const Statement &statement_node)
{
    std::visit(overloaded{

                   // ── Non-blocking assignment: lhs <= rhs ───────────────────────────
                   [this](const NonBlockingAssignment *assignment_statement)
                   {
                       // Evaluate the RHS to get its bit-width (and value if constant).
                       auto rhs_result = evaluate_expression_properties(
                           assignment_statement->right_hand_side_expression);

                       // We only perform register-level checks when the LHS is a plain identifier
                       // (not a complex expression like a bit-select).
                       if (std::holds_alternative<Identifier>(assignment_statement->left_hand_side_expression))
                       {
                           auto assigned_name = std::get<Identifier>(
                                                    assignment_statement->left_hand_side_expression)
                                                    .identifier_name;

                           // Mark the register as driven.
                           is_register_written_to[assigned_name] = true;

                           // Check if this register is already driven by a DIFFERENT always block —
                           // two drivers on the same register is a classic multiple-driver bug.
                           if (register_driven_by_block.count(assigned_name) &&
                               register_driven_by_block[assigned_name] !=
                                   current_linter_context.current_always_block)
                               report_violation("Multi-Driven Register: '" + std::string(assigned_name) +
                                                "' is driven by multiple blocks.");

                           // Record which always block now owns this register.
                           register_driven_by_block[assigned_name] = current_linter_context.current_always_block;

                           // Check for a width mismatch: if the RHS result is wider than the
                           // declared register width, bits will be silently truncated in hardware.
                           if (evaluated_signal_widths.count(assigned_name))
                           {
                               uint32_t lhs_width = evaluated_signal_widths[assigned_name];
                               if (rhs_result.bit_width_size > lhs_width)
                                   report_violation(
                                       "Structural Width Mismatch (Carry Overflow): Assigning a " +
                                       std::to_string(rhs_result.bit_width_size) +
                                       "-bit mathematical result to a " +
                                       std::to_string(lhs_width) +
                                       "-bit register '" + std::string(assigned_name) + "'.");
                           }
                       }
                   },

                   // ── If statement ──────────────────────────────────────────────────
                   [this](const IfStatement *if_statement_node)
                   {
                       auto condition_result = evaluate_expression_properties(
                           if_statement_node->condition_expression);

                       // A statically-false condition means the true branch is dead code.
                       if (condition_result.statically_evaluated_value &&
                           *condition_result.statically_evaluated_value == 0)
                           report_violation("Unreachable Block: 'if' condition evaluates to false (0).");

                       // In a combinational block, every 'if' without an 'else' implies a latch
                       // because the output has no defined value when the condition is false.
                       if (current_linter_context.in_combinational_always_block &&
                           !if_statement_node->false_branch_statement)
                           report_violation("Infer Latch: 'if' statement without 'else' branch.");

                       // Recurse into both branches.
                       analyze_statement_for_violations(if_statement_node->true_branch_statement);
                       if (if_statement_node->false_branch_statement)
                           analyze_statement_for_violations(*if_statement_node->false_branch_statement);
                   },

                   // ── Begin/end block: simply recurse into each contained statement ──
                   [this](const BlockStatement *block_statement_node)
                   {
                       for (const auto &inner_statement : block_statement_node->contained_statements)
                           analyze_statement_for_violations(inner_statement);
                   },

                   // ── Case statement ────────────────────────────────────────────────
                   [this](const CaseStatement *case_statement_node)
                   {
                       // Evaluate the switch expression (populates width info for identifiers).
                       evaluate_expression_properties(case_statement_node->condition_expression);

                       // A case without a 'default' arm is not fully specified — any unmatched
                       // value produces undefined or latched output (non-full/parallel case).
                       if (!case_statement_node->default_branch_statement)
                           report_violation("Non Full/Parallel Case: 'case' missing 'default'.");
                       else
                           analyze_statement_for_violations(*case_statement_node->default_branch_statement);

                       // Process each arm: record the matched identifier (for FSM state tracking)
                       // and recurse into the arm's body statement.
                       for (const auto &case_branch : case_statement_node->case_branches)
                       {
                           // If the case value is an identifier (e.g. a parameter like STATE_IDLE),
                           // mark it as "used" so the FSM check can spot unreachable states.
                           if (std::holds_alternative<Identifier>(case_branch.first))
                               used_case_statement_items.insert(
                                   std::get<Identifier>(case_branch.first).identifier_name);

                           analyze_statement_for_violations(case_branch.second);
                       }
                   }

               },
               statement_node);
}

// ── FSM STATE REACHABILITY CHECK ─────────────────────────────────────────────

// Compares the set of parameter names that look like FSM states (contain "STATE")
// against the set of identifiers actually used in case arms.
// Any parameter never matched by a case arm is reported as unreachable.
void VerilogStaticLinter::check_for_unreachable_finite_state_machine_states()
{
    for (const auto &state_parameter : finite_state_machine_states)
    {
        // Heuristic: parameters whose name contains "STATE" are treated as FSM constants.
        if (state_parameter.find("STATE") != std::string_view::npos)
        {
            // If this state never appears in any case arm, it's unreachable.
            if (used_case_statement_items.find(state_parameter) ==
                used_case_statement_items.end())
                report_violation("Unreachable Finite State Machine State: Parameter '" +
                                 std::string(state_parameter) + "' never used.");
        }
    }
}

// ── UNINITIALIZED REGISTER CHECK ─────────────────────────────────────────────

// After all always blocks have been walked, any output register that was never
// assigned to is reported — it will hold an undefined value in simulation and
// may synthesise to an unintended latch or constant.
void VerilogStaticLinter::check_for_uninitialized_registers()
{
    for (const auto &[register_name, has_been_written] : is_register_written_to)
    {
        if (!has_been_written)
            report_violation("Un-initialized Register: '" + std::string(register_name) +
                             "' declared but never driven.");
    }
}