#include "Linter.h"
#include <iostream>
#include <algorithm> // for std::max

void VerilogStaticLinter::report_violation(const std::string &violation_message)
{
    linter_violations.push_back(violation_message);
}

void VerilogStaticLinter::analyze_module(const Module &verilog_module)
{
    // 1. Evaluate Parameters to build a dictionary of constant constraints
    for (const auto &parameter_item : verilog_module.module_parameters)
    {
        finite_state_machine_states.push_back(parameter_item.parameter_name);
        auto expression_result = evaluate_expression_properties(parameter_item.default_value_expression);
        if (expression_result.statically_evaluated_value)
            evaluated_parameter_values[parameter_item.parameter_name] = *expression_result.statically_evaluated_value;
    }

    // 2. Evaluate Port Widths
    for (const auto &port_item : verilog_module.module_ports)
    {
        uint32_t evaluated_width = 1;
        if (port_item.bit_range)
        {
            auto msb_res = evaluate_expression_properties(port_item.bit_range->most_significant_bit);
            auto lsb_res = evaluate_expression_properties(port_item.bit_range->least_significant_bit);
            if (msb_res.statically_evaluated_value && lsb_res.statically_evaluated_value)
                evaluated_width = (*msb_res.statically_evaluated_value - *lsb_res.statically_evaluated_value) + 1;
        }
        evaluated_signal_widths[port_item.port_name] = evaluated_width;
        if (port_item.is_register_type && port_item.direction == PortDirection::Output)
            is_register_written_to[port_item.port_name] = false;
    }

    // 3. Process All Module Items using std::visit and the overloaded helper
    for (const auto &module_item : verilog_module.module_items)
    {
        std::visit(overloaded{[this](const AlwaysBlock *always_block)
                              {
                                  current_linter_context.current_always_block = always_block;
                                  current_linter_context.in_combinational_always_block = always_block->is_combinational_block();
                                  analyze_statement_for_violations(always_block->body_statement);
                                  current_linter_context.current_always_block = nullptr;
                              },
                              [this](const SignalDeclaration *sig_decl)
                              {
                                  uint32_t evaluated_width = 1;
                                  if (sig_decl->bit_range)
                                  {
                                      auto msb_res = evaluate_expression_properties(sig_decl->bit_range->most_significant_bit);
                                      auto lsb_res = evaluate_expression_properties(sig_decl->bit_range->least_significant_bit);
                                      if (msb_res.statically_evaluated_value && lsb_res.statically_evaluated_value)
                                          evaluated_width = (*msb_res.statically_evaluated_value - *lsb_res.statically_evaluated_value) + 1;
                                  }
                                  for (auto name : sig_decl->signal_names)
                                  {
                                      evaluated_signal_widths[name] = evaluated_width;
                                      if (sig_decl->is_register_type)
                                          is_register_written_to[name] = false;
                                  }
                              },
                              [this](const ContinuousAssignment *assign_stmt)
                              {
                                  auto rhs_res = evaluate_expression_properties(assign_stmt->right_hand_side_expression);
                                  if (std::holds_alternative<Identifier>(assign_stmt->left_hand_side_expression))
                                  {
                                      auto assigned_name = std::get<Identifier>(assign_stmt->left_hand_side_expression).identifier_name;
                                      is_register_written_to[assigned_name] = true;

                                      if (evaluated_signal_widths.count(assigned_name))
                                      {
                                          uint32_t lhs_width = evaluated_signal_widths[assigned_name];
                                          if (rhs_res.bit_width_size > lhs_width)
                                          {
                                              report_violation("Width Mismatch on continuous assignment: Assigning " +
                                                               std::to_string(rhs_res.bit_width_size) + "-bit to " +
                                                               std::to_string(lhs_width) + "-bit wire '" + std::string(assigned_name) + "'.");
                                          }
                                      }
                                  }
                              }},
                   module_item);
    }

    check_for_unreachable_finite_state_machine_states();
    check_for_uninitialized_registers();
}

ExpressionResult VerilogStaticLinter::evaluate_expression_properties(const Expression &expression_node)
{
    return std::visit(overloaded{[this](const Identifier &identifier_literal) -> ExpressionResult
                                 {
                                     if (evaluated_parameter_values.count(identifier_literal.identifier_name))
                                         return ExpressionResult{evaluated_parameter_values[identifier_literal.identifier_name], 32};
                                     if (evaluated_signal_widths.count(identifier_literal.identifier_name))
                                         return ExpressionResult{std::nullopt, evaluated_signal_widths[identifier_literal.identifier_name]};
                                     return ExpressionResult{std::nullopt, 32};
                                 },
                                 [this](const Number &number_literal) -> ExpressionResult
                                 {
                                     auto parsed_constant_value = parse_verilog_number(number_literal.numeric_value_string);
                                     if (parsed_constant_value)
                                         return ExpressionResult{parsed_constant_value->numeric_value, parsed_constant_value->bit_width_size};
                                     return ExpressionResult{std::nullopt, 32};
                                 },
                                 [this](const BinaryExpression *binary_expression) -> ExpressionResult
                                 {
                                     auto left_operand_result = evaluate_expression_properties(binary_expression->left_expression);
                                     auto right_operand_result = evaluate_expression_properties(binary_expression->right_expression);

                                     uint32_t operand_bit_width = std::max(left_operand_result.bit_width_size, right_operand_result.bit_width_size);
                                     uint32_t result_bit_width = operand_bit_width;

                                     // Inferred math sizes
                                     if (binary_expression->operator_symbol == "+" || binary_expression->operator_symbol == "-")
                                         result_bit_width += 1;
                                     else if (binary_expression->operator_symbol == "*")
                                         result_bit_width = left_operand_result.bit_width_size + right_operand_result.bit_width_size;
                                     else if (binary_expression->operator_symbol == "<<" || binary_expression->operator_symbol == ">>")
                                         result_bit_width = left_operand_result.bit_width_size;
                                     else if (binary_expression->operator_symbol == "==" || binary_expression->operator_symbol == "!=" ||
                                              binary_expression->operator_symbol == ">=" || binary_expression->operator_symbol == "<=" ||
                                              binary_expression->operator_symbol == "&&" || binary_expression->operator_symbol == "||")
                                         result_bit_width = 1;

                                     if (left_operand_result.statically_evaluated_value && right_operand_result.statically_evaluated_value)
                                     {
                                         uint64_t maximum_possible_value = (operand_bit_width >= 64) ? ~0ULL : ((1ULL << operand_bit_width) - 1);
                                         if (binary_expression->operator_symbol == "+")
                                         {
                                             if (*left_operand_result.statically_evaluated_value > maximum_possible_value - *right_operand_result.statically_evaluated_value)
                                                 report_violation("Constant Math Overflow: " + std::to_string(*left_operand_result.statically_evaluated_value) + " + " + std::to_string(*right_operand_result.statically_evaluated_value));
                                             return ExpressionResult{(*left_operand_result.statically_evaluated_value + *right_operand_result.statically_evaluated_value) & ((1ULL << result_bit_width) - 1), result_bit_width};
                                         }
                                     }
                                     return ExpressionResult{std::nullopt, result_bit_width};
                                 }},
                      expression_node);
}

void VerilogStaticLinter::analyze_statement_for_violations(const Statement &statement_node)
{
    std::visit(overloaded{[this](const Assignment *assignment_statement)
                          {
                              // Check correct assignment operator usage based on context
                              if (assignment_statement->is_blocking && !current_linter_context.in_combinational_always_block)
                              {
                                  report_violation("Design Practice: Using blocking assignment '=' inside a sequential (edge-triggered) block.");
                              }
                              else if (!assignment_statement->is_blocking && current_linter_context.in_combinational_always_block)
                              {
                                  report_violation("Design Practice: Using non-blocking assignment '<=' inside a combinational block.");
                              }

                              auto right_hand_side_result = evaluate_expression_properties(assignment_statement->right_hand_side_expression);

                              if (std::holds_alternative<Identifier>(assignment_statement->left_hand_side_expression))
                              {
                                  auto assigned_register_name = std::get<Identifier>(assignment_statement->left_hand_side_expression).identifier_name;
                                  is_register_written_to[assigned_register_name] = true;

                                  if (register_driven_by_block.count(assigned_register_name) && register_driven_by_block[assigned_register_name] != current_linter_context.current_always_block)
                                      report_violation("Multi-Driven Register: '" + std::string(assigned_register_name) + "' is driven by multiple blocks.");
                                  register_driven_by_block[assigned_register_name] = current_linter_context.current_always_block;

                                  if (evaluated_signal_widths.count(assigned_register_name))
                                  {
                                      uint32_t left_hand_side_width = evaluated_signal_widths[assigned_register_name];
                                      if (right_hand_side_result.bit_width_size > left_hand_side_width)
                                      {
                                          report_violation("Structural Width Mismatch (Carry Overflow): Assigning a " +
                                                           std::to_string(right_hand_side_result.bit_width_size) + "-bit mathematical result to a " +
                                                           std::to_string(left_hand_side_width) + "-bit register '" + std::string(assigned_register_name) + "'.");
                                      }
                                  }
                              }
                          },
                          [this](const IfStatement *if_statement_node)
                          {
                              auto condition_evaluation_result = evaluate_expression_properties(if_statement_node->condition_expression);
                              if (condition_evaluation_result.statically_evaluated_value && *condition_evaluation_result.statically_evaluated_value == 0)
                                  report_violation("Unreachable Block: 'if' condition statically evaluates to false (0).");

                              if (current_linter_context.in_combinational_always_block && !if_statement_node->false_branch_statement)
                                  report_violation("Infer Latch: 'if' statement inside combinational block without 'else' branch.");

                              analyze_statement_for_violations(if_statement_node->true_branch_statement);
                              if (if_statement_node->false_branch_statement)
                                  analyze_statement_for_violations(*if_statement_node->false_branch_statement);
                          },
                          [this](const BlockStatement *block_statement_node)
                          {
                              for (const auto &inner_statement : block_statement_node->contained_statements)
                                  analyze_statement_for_violations(inner_statement);
                          },
                          [this](const CaseStatement *case_statement_node)
                          {
                              if (!case_statement_node->default_branch_statement && current_linter_context.in_combinational_always_block)
                                  report_violation("Non Full/Parallel Case: 'case' missing 'default' in combinational logic.");
                              else if (case_statement_node->default_branch_statement)
                                  analyze_statement_for_violations(*case_statement_node->default_branch_statement);

                              for (const auto &case_branch : case_statement_node->case_branches)
                              {
                                  if (std::holds_alternative<Identifier>(case_branch.first))
                                      used_case_statement_items.insert(std::get<Identifier>(case_branch.first).identifier_name);
                                  analyze_statement_for_violations(case_branch.second);
                              }
                          }},
               statement_node);
}

void VerilogStaticLinter::check_for_unreachable_finite_state_machine_states()
{
    for (const auto &state_parameter : finite_state_machine_states)
    {
        if (state_parameter.find("STATE") != std::string_view::npos)
        {
            if (used_case_statement_items.find(state_parameter) == used_case_statement_items.end())
                report_violation("Unreachable Finite State Machine State: Parameter '" + std::string(state_parameter) + "' never used.");
        }
    }
}

void VerilogStaticLinter::check_for_uninitialized_registers()
{
    for (const auto &[register_name, has_been_written] : is_register_written_to)
    {
        if (!has_been_written)
            report_violation("Un-initialized Register/Wire: '" + std::string(register_name) + "' declared but never driven.");
    }
}

void VerilogStaticLinter::print_linter_report() const
{
    std::cout << "\n====================================\n";
    std::cout << "        LINTER VIOLATION REPORT       \n";
    std::cout << "====================================\n";
    if (linter_violations.empty())
        std::cout << "  No violations found. Clean code!\n";
    else
    {
        for (size_t index = 0; index < linter_violations.size(); index++)
            std::cout << "[" << (index + 1) << "] " << linter_violations[index] << "\n";
    }
    std::cout << "====================================\n\n";
}
