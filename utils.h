#pragma once

#include "Parser.h"
#include <iostream>
#include <charconv>

std::optional<ConstantValue> parse_verilog_number(std::string_view input_string);

// Utility function to print standard AST expressions to the console
void print_expression(const Expression &expression_node);

// Utility function to print standard AST statements recursively to the console
void print_statement(const Statement &statement_node, std::string indentation_string);