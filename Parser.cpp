#include "Parser.h"
#include <iostream>

VerilogParser::VerilogParser(std::string_view source_code, AbstractSyntaxTreeArena &syntax_tree_arena)
    : lexical_analyzer(source_code), syntax_tree_arena(syntax_tree_arena)
{
    advance_to_next_token();
}

void VerilogParser::advance_to_next_token()
{
    current_token = lexical_analyzer.get_next_token();
}

bool VerilogParser::match_and_consume_token(TokenType expected_type, std::string_view expected_content)
{
    if (current_token.type == expected_type && (expected_content.empty() || current_token.content == expected_content))
    {
        advance_to_next_token();
        return true;
    }
    return false;
}

void VerilogParser::expect_token_or_fail(TokenType expected_type, std::string_view expected_content)
{
    if (!match_and_consume_token(expected_type, expected_content))
    {
        std::cerr << "Syntax Error: Expected '" << expected_content << "' but got '" << current_token.content << "'\n";
        exit(1);
    }
}

Module VerilogParser::parse_module_definition()
{
    expect_token_or_fail(TokenType::Keyword, "module");
    std::string_view parsed_module_name = current_token.content;
    expect_token_or_fail(TokenType::Identifier);

    // 1. Parse Parameters
    auto parsed_parameters = std::pmr::vector<Parameter>(syntax_tree_arena.get_allocator());
    if (match_and_consume_token(TokenType::Symbol, "#"))
    {
        expect_token_or_fail(TokenType::Symbol, "(");
        while (current_token.type == TokenType::Keyword && current_token.content == "parameter")
        {
            advance_to_next_token();
            std::string_view parsed_parameter_name = current_token.content;
            expect_token_or_fail(TokenType::Identifier);
            expect_token_or_fail(TokenType::Symbol, "=");
            Expression parsed_parameter_value = parse_expression();
            parsed_parameters.push_back(Parameter{parsed_parameter_name, parsed_parameter_value});
            match_and_consume_token(TokenType::Symbol, ",");
        }
        expect_token_or_fail(TokenType::Symbol, ")");
    }

    // 2. Parse Ports
    auto parsed_ports = std::pmr::vector<Port>(syntax_tree_arena.get_allocator());
    expect_token_or_fail(TokenType::Symbol, "(");
    while (current_token.type != TokenType::Symbol || current_token.content != ")")
    {
        parsed_ports.push_back(parse_port_definition());
        match_and_consume_token(TokenType::Symbol, ",");
    }
    expect_token_or_fail(TokenType::Symbol, ")");
    expect_token_or_fail(TokenType::Symbol, ";");

    // 3. Parse internal module items (signals, assigns, always blocks)
    auto parsed_module_items = std::pmr::vector<ModuleItem>(syntax_tree_arena.get_allocator());

    while (current_token.type != TokenType::Keyword || current_token.content != "endmodule")
    {
        if (current_token.content == "always")
        {
            parsed_module_items.push_back(parse_always_block_definition());
        }
        else if (current_token.content == "assign")
        {
            advance_to_next_token();
            Expression lhs = parse_expression();
            expect_token_or_fail(TokenType::Symbol, "=");
            Expression rhs = parse_expression();
            expect_token_or_fail(TokenType::Symbol, ";");
            parsed_module_items.push_back(syntax_tree_arena.allocate_node<ContinuousAssignment>(ContinuousAssignment{lhs, rhs}));
        }
        else if (current_token.content == "wire" || current_token.content == "reg")
        {
            bool is_reg = (current_token.content == "reg");
            advance_to_next_token();

            std::optional<BitRange> bit_range = std::nullopt;
            if (match_and_consume_token(TokenType::Symbol, "["))
            {
                Expression msb = parse_expression();
                expect_token_or_fail(TokenType::Symbol, ":");
                Expression lsb = parse_expression();
                expect_token_or_fail(TokenType::Symbol, "]");
                bit_range = BitRange{msb, lsb};
            }

            auto signal_names = std::pmr::vector<std::string_view>(syntax_tree_arena.get_allocator());
            while (true)
            {
                signal_names.push_back(current_token.content);
                expect_token_or_fail(TokenType::Identifier);
                if (match_and_consume_token(TokenType::Symbol, ","))
                    continue;
                break;
            }
            expect_token_or_fail(TokenType::Symbol, ";");
            parsed_module_items.push_back(syntax_tree_arena.allocate_node<SignalDeclaration>(SignalDeclaration{is_reg, bit_range, std::move(signal_names)}));
        }
        else
        {
            std::cerr << "Unknown module item: " << current_token.content << "\n";
            exit(1);
        }
    }
    expect_token_or_fail(TokenType::Keyword, "endmodule");

    return Module{parsed_module_name, std::move(parsed_parameters), std::move(parsed_ports), std::move(parsed_module_items)};
}

Port VerilogParser::parse_port_definition()
{
    PortDirection parsed_port_direction = PortDirection::Input;
    if (match_and_consume_token(TokenType::Keyword, "input"))
        parsed_port_direction = PortDirection::Input;
    else if (match_and_consume_token(TokenType::Keyword, "output"))
        parsed_port_direction = PortDirection::Output;

    bool is_register_type = match_and_consume_token(TokenType::Keyword, "reg");

    std::optional<BitRange> parsed_port_range = std::nullopt;
    if (match_and_consume_token(TokenType::Symbol, "["))
    {
        Expression most_significant_bit_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, ":");
        Expression least_significant_bit_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, "]");
        parsed_port_range = BitRange{most_significant_bit_expression, least_significant_bit_expression};
    }

    std::string_view parsed_port_name = current_token.content;
    expect_token_or_fail(TokenType::Identifier);
    return Port{parsed_port_direction, is_register_type, parsed_port_range, parsed_port_name};
}

AlwaysBlock *VerilogParser::parse_always_block_definition()
{
    expect_token_or_fail(TokenType::Keyword, "always");
    expect_token_or_fail(TokenType::Symbol, "@");
    expect_token_or_fail(TokenType::Symbol, "(");

    // Build the sensitivity list
    auto parsed_sensitivities = std::pmr::vector<Sensitivity>(syntax_tree_arena.get_allocator());
    while (current_token.type != TokenType::Symbol || current_token.content != ")")
    {
        EdgeType parsed_edge_trigger = EdgeType::None;
        if (match_and_consume_token(TokenType::Keyword, "posedge"))
            parsed_edge_trigger = EdgeType::PositiveEdge;
        else if (match_and_consume_token(TokenType::Keyword, "negedge"))
            parsed_edge_trigger = EdgeType::NegativeEdge;

        if (current_token.type == TokenType::Identifier || current_token.type == TokenType::Symbol)
        {
            std::string_view signal_identifier_name = current_token.content;
            advance_to_next_token();
            parsed_sensitivities.push_back(Sensitivity{parsed_edge_trigger, Identifier{signal_identifier_name}});
        }

        match_and_consume_token(TokenType::Keyword, "or");
        match_and_consume_token(TokenType::Symbol, ",");
    }
    expect_token_or_fail(TokenType::Symbol, ")");

    Statement parsed_body_statement = parse_statement_definition();
    return syntax_tree_arena.allocate_node<AlwaysBlock>(AlwaysBlock{std::move(parsed_sensitivities), parsed_body_statement});
}

Statement VerilogParser::parse_statement_definition()
{
    // Begin/End blocks
    if (match_and_consume_token(TokenType::Keyword, "begin"))
    {
        auto parsed_statements = std::pmr::vector<Statement>(syntax_tree_arena.get_allocator());
        while (current_token.type != TokenType::Keyword || current_token.content != "end")
            parsed_statements.push_back(parse_statement_definition());
        expect_token_or_fail(TokenType::Keyword, "end");
        return syntax_tree_arena.allocate_node<BlockStatement>(BlockStatement{std::move(parsed_statements)});
    }

    // If Statements
    if (match_and_consume_token(TokenType::Keyword, "if"))
    {
        expect_token_or_fail(TokenType::Symbol, "(");
        Expression condition_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, ")");
        Statement true_branch_statement = parse_statement_definition();
        std::optional<Statement> false_branch_statement = std::nullopt;
        if (match_and_consume_token(TokenType::Keyword, "else"))
            false_branch_statement = parse_statement_definition();
        return syntax_tree_arena.allocate_node<IfStatement>(IfStatement{condition_expression, true_branch_statement, false_branch_statement});
    }

    // Case Statements
    if (match_and_consume_token(TokenType::Keyword, "case"))
    {
        expect_token_or_fail(TokenType::Symbol, "(");
        Expression condition_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, ")");

        auto parsed_case_branches = std::pmr::vector<std::pair<Expression, Statement>>(syntax_tree_arena.get_allocator());
        std::optional<Statement> default_branch_statement = std::nullopt;

        while (current_token.type != TokenType::Keyword || current_token.content != "endcase")
        {
            if (match_and_consume_token(TokenType::Keyword, "default"))
            {
                expect_token_or_fail(TokenType::Symbol, ":");
                default_branch_statement = parse_statement_definition();
            }
            else
            {
                Expression case_item_expression = parse_expression();
                expect_token_or_fail(TokenType::Symbol, ":");
                parsed_case_branches.push_back({case_item_expression, parse_statement_definition()});
            }
        }
        expect_token_or_fail(TokenType::Keyword, "endcase");
        return syntax_tree_arena.allocate_node<CaseStatement>(CaseStatement{condition_expression, std::move(parsed_case_branches), default_branch_statement});
    }

    // Variable Assignments (<= and =)
    if (current_token.type == TokenType::Identifier)
    {
        Expression left_hand_side_expression = Identifier{current_token.content};
        advance_to_next_token();

        bool is_blocking = false;
        if (match_and_consume_token(TokenType::Symbol, "="))
        {
            is_blocking = true;
        }
        else if (match_and_consume_token(TokenType::Symbol, "<="))
        {
            is_blocking = false;
        }
        else
        {
            std::cerr << "Syntax Error: Expected '=' or '<=' after identifier '" << std::get<Identifier>(left_hand_side_expression).identifier_name << "'\n";
            exit(1);
        }

        Expression right_hand_side_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, ";");
        return syntax_tree_arena.allocate_node<Assignment>(Assignment{left_hand_side_expression, right_hand_side_expression, is_blocking});
    }

    std::cerr << "Syntax Error in Statement: " << current_token.content << "\n";
    exit(1);
}

// ----------------------------------------------------
// Recursive Descent Math/Logical Parsing
// ----------------------------------------------------

Expression VerilogParser::parse_primary()
{
    if (match_and_consume_token(TokenType::Symbol, "("))
    {
        Expression expr = parse_expression();
        expect_token_or_fail(TokenType::Symbol, ")");
        return expr;
    }
    if (current_token.type == TokenType::Identifier)
    {
        Expression expr = Identifier{current_token.content};
        advance_to_next_token();
        return expr;
    }
    if (current_token.type == TokenType::NumberLiteral)
    {
        Expression expr = Number{current_token.content};
        advance_to_next_token();
        return expr;
    }
    std::cerr << "Syntax Error: Expected identifier, number, or '(', got '" << current_token.content << "'\n";
    exit(1);
}

Expression VerilogParser::parse_factor()
{
    Expression left = parse_primary();
    while (current_token.type == TokenType::Symbol &&
           (current_token.content == "*" || current_token.content == "/" ||
            current_token.content == "<<" || current_token.content == ">>"))
    {
        std::string_view op = current_token.content;
        advance_to_next_token();
        Expression right = parse_primary();
        left = syntax_tree_arena.allocate_node<BinaryExpression>(BinaryExpression{op, left, right});
    }
    return left;
}

Expression VerilogParser::parse_term()
{
    Expression left = parse_factor();
    while (current_token.type == TokenType::Symbol &&
           (current_token.content == "+" || current_token.content == "-" ||
            current_token.content == "|" || current_token.content == "&" ||
            current_token.content == "^"))
    {
        std::string_view op = current_token.content;
        advance_to_next_token();
        Expression right = parse_factor();
        left = syntax_tree_arena.allocate_node<BinaryExpression>(BinaryExpression{op, left, right});
    }
    return left;
}

Expression VerilogParser::parse_expression()
{
    Expression left = parse_term();
    while (current_token.type == TokenType::Symbol &&
           (current_token.content == "==" || current_token.content == "!=" ||
            current_token.content == ">=" || current_token.content == "<=" ||
            current_token.content == ">" || current_token.content == "<" ||
            current_token.content == "&&" || current_token.content == "||"))
    {
        std::string_view op = current_token.content;
        advance_to_next_token();
        Expression right = parse_term();
        left = syntax_tree_arena.allocate_node<BinaryExpression>(BinaryExpression{op, left, right});
    }
    return left;
}