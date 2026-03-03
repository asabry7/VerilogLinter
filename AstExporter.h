/**
 * @file AstExporter.h
 * @brief Serializes the Verilog AST into a JSON format for backend/AI consumption.
 */
#pragma once
#include "Parser.h"
#include <nlohmann/json.hpp>

// using json = nlohmann::json;
using json = nlohmann::ordered_json;

/**
 * @class AstExporter
 * @brief Converts AST nodes into nlohmann::json objects.
 */
class AstExporter
{
public:
    static json export_module(const Module &verilog_module);

private:
    static json export_expression(const Expression &expr);
    static json export_statement(const Statement &stmt);
    static json export_module_item(const ModuleItem &item);
};