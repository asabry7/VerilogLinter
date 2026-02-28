#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>

#include "Lexer.h"
#include "Parser.h"
#include "Linter.h"
#include "utils.h"

// Main entry point for the static analysis hardware tool
int main(int argument_count, char *argument_values[])
{
    // Ensure correct invocation arguments
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

    // Read the entire file into a std::string buffer
    std::string source_code_content((std::istreambuf_iterator<char>(input_file_stream)),
                                    std::istreambuf_iterator<char>());

    // Set up the memory arena and initiate the Parser
    AbstractSyntaxTreeArena syntax_tree_arena;
    VerilogParser verilog_parser(source_code_content, syntax_tree_arena);

    // Convert source into a parsed syntax tree
    Module abstract_syntax_tree_root = verilog_parser.parse_module_definition();

    std::cout << "=== PARSED VERILOG MODULE ===\n";
    std::cout << "Module Name: " << abstract_syntax_tree_root.module_name << "\n";
    std::cout << "\nRunning Advanced Linter...\n";

    // Feed the Abstract Syntax Tree into the rules-based Linter
    VerilogStaticLinter verilog_linter;
    verilog_linter.analyze_module(abstract_syntax_tree_root);
    verilog_linter.print_linter_report();

    return 0;
}