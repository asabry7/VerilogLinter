#include "utils.h"
// Parses strings representing numbers like "8'hFF", "4'b1010", or "255" into ConstantValue
std::optional<ConstantValue> parse_verilog_number(std::string_view input_string)
{
    if (input_string.empty())
        return std::nullopt;

    uint32_t parsed_bit_width = 32;
    int numeric_base = 10;
    std::string_view value_string_part = input_string;

    size_t tick_character_position = input_string.find('\'');
    if (tick_character_position != std::string_view::npos)
    {
        if (tick_character_position > 0)
            std::from_chars(input_string.data(), input_string.data() + tick_character_position, parsed_bit_width);
        if (tick_character_position + 1 >= input_string.size())
            return std::nullopt;

        char base_character = input_string[tick_character_position + 1];
        value_string_part = input_string.substr(tick_character_position + 2);

        if (base_character == 'h' || base_character == 'H')
            numeric_base = 16;
        else if (base_character == 'b' || base_character == 'B')
            numeric_base = 2;
        else if (base_character == 'o' || base_character == 'O')
            numeric_base = 8;
        else if (base_character == 'd' || base_character == 'D')
            numeric_base = 10;
    }

    std::string cleaned_value_string;
    for (char current_character : value_string_part)
        if (current_character != '_')
            cleaned_value_string.push_back(current_character);

    uint64_t final_numeric_value = 0;
    if (numeric_base == 2)
    {
        for (char binary_character : cleaned_value_string)
        {
            if (binary_character == '0' || binary_character == '1')
                final_numeric_value = (final_numeric_value << 1) | (binary_character - '0');
            else
                return std::nullopt;
        }
    }
    else
    {
        auto [character_pointer, error_code] = std::from_chars(cleaned_value_string.data(), cleaned_value_string.data() + cleaned_value_string.size(), final_numeric_value, numeric_base);
        if (error_code != std::errc())
            return std::nullopt;
    }

    return ConstantValue{final_numeric_value, parsed_bit_width};
}

// Utility function to print standard AST expressions to the console
void print_expression(const Expression &expression_node)
{
    std::visit(overloaded{[](const Identifier &identifier_literal)
                          { std::cout << identifier_literal.identifier_name; }, [](const Number &number_literal)
                          { std::cout << number_literal.numeric_value_string; }, [](const BinaryExpression *binary_expression)
                          {
            std::cout << "(";
            print_expression(binary_expression->left_expression);
            std::cout << " " << binary_expression->operator_symbol << " ";
            print_expression(binary_expression->right_expression);
            std::cout << ")"; }},
               expression_node);
}

// Utility function to print standard AST statements recursively to the console
void print_statement(const Statement &statement_node, std::string indentation_string)
{
    std::visit(overloaded{[=](const Assignment *assignment_statement)
                          {
                              std::cout << indentation_string;
                              print_expression(assignment_statement->left_hand_side_expression);
                              std::cout << (assignment_statement->is_blocking ? " = " : " <= ");
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