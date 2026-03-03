#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <getopt.h>

#include "Lexer.h"
#include "Parser.h"
#include "Linter.h"
#include "utils.h"
#include "AstExporter.h"

int main(int argument_count, char *argument_values[])
{
    std::string ast_output_path;

    static struct option long_options[] = {
        {"export-ast", required_argument, nullptr, 'e'},
        {nullptr, 0, nullptr, 0}};

    int opt;
    while ((opt = getopt_long(argument_count, argument_values, "", long_options, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'e':
            ast_output_path = optarg;
            break;
        default:
            std::cerr << "Usage: " << argument_values[0] << " <verilog_file.v> [--export-ast <output.json>]\n";
            return 1;
        }
    }

    // The verilog file should be the remaining non-option argument
    if (optind >= argument_count)
    {
        std::cerr << "Usage: " << argument_values[0] << " <verilog_file.v> [--export-ast <output.json>]\n";
        return 1;
    }

    std::filesystem::path source_file_path(argument_values[optind]);
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

    // Export to JSON only if --export-ast flag was provided
    if (!ast_output_path.empty())
    {
        json ast_json = AstExporter::export_module(abstract_syntax_tree_root);
        std::ofstream json_file(ast_output_path);
        if (!json_file.is_open())
        {
            std::cerr << "Error: Could not write to '" << ast_output_path << "'.\n";
            return 1;
        }
        json_file << ast_json.dump(4);
        json_file.close();
        std::cout << "Successfully exported AST to " << ast_output_path << "\n";
    }

    std::cout << "=== PARSED VERILOG MODULE ===\n";
    std::cout << "Module Name: " << abstract_syntax_tree_root.module_name << "\n";
    std::cout << "\nRunning Advanced Linter...\n";

    // Feed the Abstract Syntax Tree into the rules-based Linter
    VerilogStaticLinter verilog_linter;
    verilog_linter.analyze_module(abstract_syntax_tree_root);
    verilog_linter.print_linter_report();

    return 0;
}