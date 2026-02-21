#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>

// #include "Lexer.h"
#include "Linter.h"

// ==========================================
// 6. AST PRINTER
// ==========================================
void print_expression(const Expression &expression_node)
{
    std::visit(overloaded{[](const Identifier &identifier_literal)
                          { std::cout << identifier_literal.identifier_name; },
                          [](const Number &number_literal)
                          { std::cout << number_literal.numeric_value_string; },
                          [](const BinaryExpression *binary_expression)
                          {
                              print_expression(binary_expression->left_expression);
                              std::cout << " " << binary_expression->operator_symbol << " ";
                              print_expression(binary_expression->right_expression);
                          }},
               expression_node);
}

void print_statement(const Statement &statement_node, std::string indentation_string)
{
    std::visit(overloaded{[=](const NonBlockingAssignment *assignment_statement)
                          {
                              std::cout << indentation_string;
                              print_expression(assignment_statement->left_hand_side_expression);
                              std::cout << " <= ";
                              print_expression(assignment_statement->right_hand_side_expression);
                              std::cout << ";\n";
                          },
                          [=](const IfStatement *if_statement_node)
                          {
                              std::cout << indentation_string << "if (";
                              print_expression(if_statement_node->condition_expression);
                              std::cout << ")\n";
                              print_statement(if_statement_node->true_branch_statement, indentation_string + "  ");
                              if (if_statement_node->false_branch_statement)
                              {
                                  std::cout << indentation_string << "else\n";
                                  print_statement(*if_statement_node->false_branch_statement, indentation_string + "  ");
                              }
                          },
                          [=](const BlockStatement *block_statement_node)
                          {
                              std::cout << indentation_string << "begin\n";
                              for (const auto &inner_statement : block_statement_node->contained_statements)
                                  print_statement(inner_statement, indentation_string + "  ");
                              std::cout << indentation_string << "end\n";
                          },
                          [=](const CaseStatement *case_statement_node)
                          {
                              std::cout << indentation_string << "case (";
                              print_expression(case_statement_node->condition_expression);
                              std::cout << ")\n";
                              for (const auto &case_branch : case_statement_node->case_branches)
                              {
                                  std::cout << indentation_string << "  ";
                                  print_expression(case_branch.first);
                                  std::cout << " :\n";
                                  print_statement(case_branch.second, indentation_string + "    ");
                              }
                              if (case_statement_node->default_branch_statement)
                              {
                                  std::cout << indentation_string << "  default :\n";
                                  print_statement(*case_statement_node->default_branch_statement, indentation_string + "    ");
                              }
                              std::cout << indentation_string << "endcase\n";
                          }},
               statement_node);
}

#include <fstream>
#include <filesystem>
#include <sstream>

int main(int argument_count, char *argument_values[])
{
    if (argument_count != 2)
    {
        std::cerr << "Usage: " << argument_values[0] << " <verilog_file.v>\n";
        return 1;
    }

    std::filesystem::path source_file_path(argument_values[1]);
    if (!std::filesystem::exists(source_file_path))
    {
        std::cerr << "Error: File '" << source_file_path << "' not found.\n";
        return 1;
    }

    std::ifstream input_file_stream(source_file_path);
    if (!input_file_stream.is_open())
    {
        std::cerr << "Error: Could not open '" << source_file_path << "'.\n";
        return 1;
    }

    std::string source_code_content((std::istreambuf_iterator<char>(input_file_stream)),
                                    std::istreambuf_iterator<char>());

    AbstractSyntaxTreeArena syntax_tree_arena;
    VerilogParser verilog_parser(source_code_content, syntax_tree_arena);
    Module abstract_syntax_tree_root = verilog_parser.parse_module_definition();

    std::cout << "=== PARSED VERILOG MODULE ===\n";
    std::cout << "Module Name: " << abstract_syntax_tree_root.module_name << "\n";
    std::cout << "\nRunning Linter...\n";

    VerilogStaticLinter verilog_linter;
    verilog_linter.analyze_module(abstract_syntax_tree_root);
    verilog_linter.print_linter_report();
}