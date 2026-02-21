#include "Parser.h"

// ── TOKEN STREAM CONTROL ─────────────────────────────────────────────────────

// Ask the lexer for the next token and store it as our one-token lookahead.
// Every parse function reads `current_token` and calls this to move forward.
void VerilogParser::advance_to_next_token()
{
    current_token = lexical_analyzer.get_next_token();
}

// Soft match: if the current token matches the expected type (and content, if provided),
// consume it and return true. Otherwise leave the stream untouched and return false.
// Passing an empty expected_content means "match any content of the given type".
bool VerilogParser::match_and_consume_token(TokenType expected_type, std::string_view expected_content = "")
{
    if (current_token.type == expected_type &&
        (expected_content.empty() || current_token.content == expected_content))
    {
        advance_to_next_token();
        return true;
    }
    return false;
}

// Hard match: same as above but fatal on mismatch.
// Used when the grammar requires a specific token and anything else is a syntax error.
void VerilogParser::expect_token_or_fail(TokenType expected_type, std::string_view expected_content = "")
{
    if (!match_and_consume_token(expected_type, expected_content))
    {
        std::cerr << "Syntax Error: Expected '" << expected_content
                  << "' but got '" << current_token.content << "'\n";
        exit(1);
    }
}

// ── CONSTRUCTOR ──────────────────────────────────────────────────────────────

// Wire up the lexer and arena, then prime the lookahead so `current_token`
// is valid before any parse function is called.
VerilogParser::VerilogParser(std::string_view source_code, AbstractSyntaxTreeArena &syntax_tree_arena)
    : lexical_analyzer(source_code), syntax_tree_arena(syntax_tree_arena)
{
    advance_to_next_token();
}

// ── MODULE ───────────────────────────────────────────────────────────────────

// Top-level rule — parses one complete Verilog module:
//
//   module <name> [#(<parameters>)] (<ports>);
//     [reg/wire declarations]
//     [always blocks ...]
//   endmodule
//
Module VerilogParser::parse_module_definition()
{
    // Every module starts with the 'module' keyword followed by its name.
    expect_token_or_fail(TokenType::Keyword, "module");
    std::string_view parsed_module_name = current_token.content;
    expect_token_or_fail(TokenType::Identifier); // consume the name

    // ── Optional parameter list: #(parameter A = ..., parameter B = ...) ──
    auto parsed_parameters = std::pmr::vector<Parameter>(syntax_tree_arena.get_allocator());
    if (match_and_consume_token(TokenType::Symbol, "#")) // '#' signals a parameter list
    {
        expect_token_or_fail(TokenType::Symbol, "(");

        // Each iteration parses one `parameter <name> = <expr>` declaration.
        while (current_token.type == TokenType::Keyword && current_token.content == "parameter")
        {
            advance_to_next_token(); // consume 'parameter'
            std::string_view parsed_parameter_name = current_token.content;
            expect_token_or_fail(TokenType::Identifier); // consume the parameter name
            expect_token_or_fail(TokenType::Symbol, "=");
            Expression parsed_parameter_value = parse_expression();
            parsed_parameters.push_back(Parameter{parsed_parameter_name, parsed_parameter_value});

            // Parameters are separated by commas; consume it if present.
            if (current_token.content == ",")
                advance_to_next_token();
        }
        expect_token_or_fail(TokenType::Symbol, ")");
    }

    // ── Port list: (<port>, <port>, ...) ──
    auto parsed_ports = std::pmr::vector<Port>(syntax_tree_arena.get_allocator());
    expect_token_or_fail(TokenType::Symbol, "(");

    // Keep parsing ports until we hit the closing ')'.
    while (current_token.type != TokenType::Symbol || current_token.content != ")")
    {
        parsed_ports.push_back(parse_port_definition());

        // Ports are separated by commas; consume it if present.
        if (current_token.content == ",")
            advance_to_next_token();
    }
    expect_token_or_fail(TokenType::Symbol, ")");
    expect_token_or_fail(TokenType::Symbol, ";"); // closing ';' of the port list

    // ── Skip internal reg/wire declarations ──
    // These are internal signal declarations (e.g. `reg [7:0] state;`).
    // The linter tracks widths from port declarations, so we skip these
    // by fast-forwarding to the next ';' for each such declaration.
    while (current_token.type == TokenType::Keyword &&
           (current_token.content == "reg" || current_token.content == "wire"))
    {
        // Advance until we find the terminating ';' or run out of tokens.
        while (!(current_token.type == TokenType::Symbol && current_token.content == ";") &&
               current_token.type != TokenType::EndToken)
            advance_to_next_token();

        advance_to_next_token(); // consume the ';' itself
    }

    // ── Module body: zero or more always blocks ──
    auto parsed_module_items = std::pmr::vector<ModuleItem>(syntax_tree_arena.get_allocator());
    while (current_token.type != TokenType::Keyword || current_token.content != "endmodule")
    {
        if (current_token.content == "always")
            parsed_module_items.push_back(parse_always_block_definition());
        else
        {
            // Anything other than 'always' is unsupported at module scope.
            std::cerr << "Unknown module item: " << current_token.content << "\n";
            exit(1);
        }
    }
    expect_token_or_fail(TokenType::Keyword, "endmodule");

    return Module{parsed_module_name,
                  std::move(parsed_parameters),
                  std::move(parsed_ports),
                  std::move(parsed_module_items)};
}

// ── PORT ─────────────────────────────────────────────────────────────────────

// Parses a single port declaration inside the module port list:
//
//   input | output  [reg]  [MSB:LSB]  <name>
//
Port VerilogParser::parse_port_definition()
{
    // Determine direction; default to Input if neither keyword appears
    // (though well-formed Verilog should always have an explicit direction).
    PortDirection parsed_port_direction = PortDirection::Input;
    if (match_and_consume_token(TokenType::Keyword, "input"))
        parsed_port_direction = PortDirection::Input;
    else if (match_and_consume_token(TokenType::Keyword, "output"))
        parsed_port_direction = PortDirection::Output;

    // The 'reg' qualifier is optional; match_and_consume returns true if found.
    bool is_register_type = match_and_consume_token(TokenType::Keyword, "reg");

    // Optional bit-range: [MSB : LSB]
    std::optional<BitRange> parsed_port_range = std::nullopt;
    if (match_and_consume_token(TokenType::Symbol, "["))
    {
        Expression most_significant_bit_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, ":");
        Expression least_significant_bit_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, "]");
        parsed_port_range = BitRange{most_significant_bit_expression,
                                     least_significant_bit_expression};
    }

    // The port name is the final identifier.
    std::string_view parsed_port_name = current_token.content;
    expect_token_or_fail(TokenType::Identifier);

    return Port{parsed_port_direction, is_register_type, parsed_port_range, parsed_port_name};
}

// ── ALWAYS BLOCK ─────────────────────────────────────────────────────────────

// Parses an always block:
//
//   always @(<sensitivity_list>) <statement>
//
// Sensitivity entries may be:
//   posedge <signal>  |  negedge <signal>  |  <signal>  (level-sensitive)
// Entries are separated by 'or' or ','.
//
AlwaysBlock *VerilogParser::parse_always_block_definition()
{
    expect_token_or_fail(TokenType::Keyword, "always");
    expect_token_or_fail(TokenType::Symbol, "@");
    expect_token_or_fail(TokenType::Symbol, "(");

    auto parsed_sensitivities = std::pmr::vector<Sensitivity>(syntax_tree_arena.get_allocator());

    // Collect sensitivity entries until the closing ')'.
    while (current_token.type != TokenType::Symbol || current_token.content != ")")
    {
        // Check for an optional edge qualifier before the signal name.
        EdgeType parsed_edge_trigger = EdgeType::None;
        if (match_and_consume_token(TokenType::Keyword, "posedge"))
            parsed_edge_trigger = EdgeType::PositiveEdge;
        else if (match_and_consume_token(TokenType::Keyword, "negedge"))
            parsed_edge_trigger = EdgeType::NegativeEdge;

        // The next token should be the signal identifier (or '*' for combinational).
        if (current_token.type == TokenType::Identifier ||
            current_token.type == TokenType::Symbol)
        {
            std::string_view signal_identifier_name = current_token.content;
            advance_to_next_token(); // consume the signal name
            parsed_sensitivities.push_back(
                Sensitivity{parsed_edge_trigger, Identifier{signal_identifier_name}});
        }

        // Sensitivity entries may be separated by 'or' or ',' — consume either if present.
        match_and_consume_token(TokenType::Keyword, "or");
        match_and_consume_token(TokenType::Symbol, ",");
    }
    expect_token_or_fail(TokenType::Symbol, ")");

    // The block body is a single statement (often a begin/end block).
    Statement parsed_body_statement = parse_statement_definition();

    // Allocate the node in the arena and return a pointer to it.
    return syntax_tree_arena.allocate_node<AlwaysBlock>(
        AlwaysBlock{std::move(parsed_sensitivities), parsed_body_statement});
}

// ── STATEMENT ────────────────────────────────────────────────────────────────

// Parses one statement; dispatches to the correct sub-parser based on the
// current keyword or falls through to a non-blocking assignment.
//
Statement VerilogParser::parse_statement_definition()
{
    // ── begin / end block ──────────────────────────────────────────────────
    // A 'begin' opens a sequential block that may contain many statements.
    if (match_and_consume_token(TokenType::Keyword, "begin"))
    {
        auto parsed_statements = std::pmr::vector<Statement>(syntax_tree_arena.get_allocator());

        // Keep parsing statements until the matching 'end'.
        while (current_token.type != TokenType::Keyword || current_token.content != "end")
            parsed_statements.push_back(parse_statement_definition());

        expect_token_or_fail(TokenType::Keyword, "end");
        return syntax_tree_arena.allocate_node<BlockStatement>(
            BlockStatement{std::move(parsed_statements)});
    }

    // ── if / else ──────────────────────────────────────────────────────────
    if (match_and_consume_token(TokenType::Keyword, "if"))
    {
        expect_token_or_fail(TokenType::Symbol, "(");
        Expression condition_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, ")");

        Statement true_branch_statement = parse_statement_definition();

        // The else branch is optional.
        std::optional<Statement> false_branch_statement = std::nullopt;
        if (match_and_consume_token(TokenType::Keyword, "else"))
            false_branch_statement = parse_statement_definition();

        return syntax_tree_arena.allocate_node<IfStatement>(
            IfStatement{condition_expression, true_branch_statement, false_branch_statement});
    }

    // ── case / endcase ────────────────────────────────────────────────────
    if (match_and_consume_token(TokenType::Keyword, "case"))
    {
        expect_token_or_fail(TokenType::Symbol, "(");
        Expression condition_expression = parse_expression(); // the value being switched on
        expect_token_or_fail(TokenType::Symbol, ")");

        auto parsed_case_branches =
            std::pmr::vector<std::pair<Expression, Statement>>(syntax_tree_arena.get_allocator());
        std::optional<Statement> default_branch_statement = std::nullopt;

        // Parse arms until 'endcase'.
        while (current_token.type != TokenType::Keyword || current_token.content != "endcase")
        {
            if (match_and_consume_token(TokenType::Keyword, "default"))
            {
                // 'default:' arm — there can only be one.
                expect_token_or_fail(TokenType::Symbol, ":");
                default_branch_statement = parse_statement_definition();
            }
            else
            {
                // Regular arm: <expr> : <statement>
                Expression case_item_expression = parse_expression();
                expect_token_or_fail(TokenType::Symbol, ":");
                parsed_case_branches.push_back(
                    {case_item_expression, parse_statement_definition()});
            }
        }
        expect_token_or_fail(TokenType::Keyword, "endcase");

        return syntax_tree_arena.allocate_node<CaseStatement>(
            CaseStatement{condition_expression,
                          std::move(parsed_case_branches),
                          default_branch_statement});
    }

    // ── Non-blocking assignment: <lhs> <= <rhs>; ─────────────────────────
    // If none of the keywords above matched, the only remaining valid statement
    // is `identifier <= expression;` (Verilog non-blocking assignment).
    if (current_token.type == TokenType::Identifier)
    {
        // Capture the LHS identifier before advancing.
        Expression left_hand_side_expression = Identifier{current_token.content};
        advance_to_next_token(); // consume the identifier

        expect_token_or_fail(TokenType::Symbol, "<="); // the non-blocking assignment operator
        Expression right_hand_side_expression = parse_expression();
        expect_token_or_fail(TokenType::Symbol, ";"); // every statement ends with ';'

        return syntax_tree_arena.allocate_node<NonBlockingAssignment>(
            NonBlockingAssignment{left_hand_side_expression, right_hand_side_expression});
    }

    // If we reach here the source contains a construct we cannot parse.
    std::cerr << "Syntax Error in Statement: " << current_token.content << "\n";
    exit(1);
}

// ── EXPRESSION ───────────────────────────────────────────────────────────────

// Parses an expression using a simple two-step strategy:
//   1. Parse a primary (identifier or number literal) as the left operand.
//   2. If the next token is a binary operator, consume it and recursively
//      parse the right operand, wrapping both in a BinaryExpression node.
//
// Supported operators: '+', '-', '=='
// Note: this is right-recursive, so `a + b + c` parses as `a + (b + c)`.
//
Expression VerilogParser::parse_expression()
{
    Expression left_side_expression;

    // ── Primary: identifier ──
    if (current_token.type == TokenType::Identifier)
    {
        left_side_expression = Identifier{current_token.content};
        advance_to_next_token();
    }
    // ── Primary: number literal ──
    else if (current_token.type == TokenType::NumberLiteral)
    {
        left_side_expression = Number{current_token.content};
        advance_to_next_token();
    }

    // ── Optional binary operator ──
    // If the token after the primary is one of our supported operators,
    // this is a binary expression; otherwise return the primary as-is.
    if (current_token.type == TokenType::Symbol &&
        (current_token.content == "+" ||
         current_token.content == "-" ||
         current_token.content == "=="))
    {
        std::string_view operator_symbol_text = current_token.content;
        advance_to_next_token(); // consume the operator

        // Recursively parse the right-hand side (right-associative).
        Expression right_side_expression = parse_expression();

        // Allocate a BinaryExpression node in the arena and return a pointer to it
        // (wrapped in the Expression variant).
        return syntax_tree_arena.allocate_node<BinaryExpression>(
            BinaryExpression{operator_symbol_text, left_side_expression, right_side_expression});
    }

    // No operator followed — return the primary expression directly.
    return left_side_expression;
}