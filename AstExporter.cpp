#include "AstExporter.h"

json AstExporter::export_expression(const Expression &expr)
{
    return std::visit(overloaded{[](const Identifier &id) -> json
                                 { return {{"type", "Identifier"}, {"name", id.identifier_name}}; }, [](const Number &num) -> json
                                 { return {{"type", "Number"}, {"value", num.numeric_value_string}}; }, [](const BinaryExpression *bin_op) -> json
                                 { return {
                                       {"type", "BinaryExpression"},
                                       {"operator", bin_op->operator_symbol},
                                       {"left", export_expression(bin_op->left_expression)},
                                       {"right", export_expression(bin_op->right_expression)}}; }},
                      expr);
}

json AstExporter::export_statement(const Statement &stmt)
{
    return std::visit(overloaded{[](const Assignment *assign) -> json
                                 {
                                     return {
                                         {"type", "Assignment"},
                                         {"is_blocking", assign->is_blocking},
                                         {"lhs", export_expression(assign->left_hand_side_expression)},
                                         {"rhs", export_expression(assign->right_hand_side_expression)}};
                                 },
                                 [](const IfStatement *if_stmt) -> json
                                 {
                                     json j = {
                                         {"type", "IfStatement"},
                                         {"condition", export_expression(if_stmt->condition_expression)},
                                         {"true_branch", export_statement(if_stmt->true_branch_statement)}};
                                     if (if_stmt->false_branch_statement)
                                         j["false_branch"] = export_statement(*if_stmt->false_branch_statement);
                                     return j;
                                 },
                                 [](const BlockStatement *block) -> json
                                 {
                                     json j = {{"type", "BlockStatement"}, {"statements", json::array()}};
                                     for (const auto &s : block->contained_statements)
                                         j["statements"].push_back(export_statement(s));
                                     return j;
                                 },
                                 [](const CaseStatement *case_stmt) -> json
                                 {
                                     json branches = json::array();
                                     for (const auto &branch : case_stmt->case_branches)
                                     {
                                         branches.push_back({{"condition", export_expression(branch.first)},
                                                             {"statement", export_statement(branch.second)}});
                                     }
                                     json j = {{"type", "CaseStatement"}, {"condition", export_expression(case_stmt->condition_expression)}, {"branches", branches}};
                                     if (case_stmt->default_branch_statement)
                                         j["default_branch"] = export_statement(*case_stmt->default_branch_statement);
                                     return j;
                                 }},
                      stmt);
}

json AstExporter::export_module_item(const ModuleItem &item)
{
    return std::visit(overloaded{[](const AlwaysBlock *always) -> json
                                 {
                                     json sens_list = json::array();
                                     for (const auto &s : always->sensitivity_list)
                                     {
                                         sens_list.push_back({{"edge", s.edge_trigger_type == EdgeType::PositiveEdge ? "posedge" : (s.edge_trigger_type == EdgeType::NegativeEdge ? "negedge" : "none")},
                                                              {"signal", s.signal_identifier.identifier_name}});
                                     }
                                     return {{"type", "AlwaysBlock"}, {"sensitivity", sens_list}, {"body", export_statement(always->body_statement)}};
                                 },
                                 [](const ContinuousAssignment *assign) -> json
                                 {
                                     return {{"type", "ContinuousAssignment"}, {"lhs", export_expression(assign->left_hand_side_expression)}, {"rhs", export_expression(assign->right_hand_side_expression)}};
                                 },
                                 [](const SignalDeclaration *sig) -> json
                                 {
                                     json names = json::array();
                                     for (auto n : sig->signal_names)
                                         names.push_back(n);
                                     return {{"type", "SignalDeclaration"}, {"is_reg", sig->is_register_type}, {"names", names}};
                                 }},
                      item);
}

json AstExporter::export_module(const Module &verilog_module)
{
    json j = {
        {"module_name", verilog_module.module_name},
        {"parameters", json::array()},
        {"ports", json::array()},
        {"items", json::array()}};

    for (const auto &p : verilog_module.module_parameters)
        j["parameters"].push_back({{"name", p.parameter_name}, {"default_value", export_expression(p.default_value_expression)}});

    for (const auto &p : verilog_module.module_ports)
        j["ports"].push_back({{"name", p.port_name}, {"direction", p.direction == PortDirection::Input ? "input" : (p.direction == PortDirection::Output ? "output" : "inout")}, {"is_reg", p.is_register_type}});

    for (const auto &item : verilog_module.module_items)
        j["items"].push_back(export_module_item(item));

    return j;
}