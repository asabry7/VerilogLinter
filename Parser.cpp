#include "Parser.h"

// ==========================================
// 1. TOKEN MANAGEMENT HELPERS
// ==========================================

void Parser::advance()
{
    current_token = lexer.next();
}

bool Parser::match(TokenType type, std::string_view content = "")
{
    if (current_token.type == type && (content.empty() || current_token.content == content))
    {
        advance();
        return true;
    }
    return false;
}

void Parser::expect(TokenType type, std::string_view content = "")
{
    if (!match(type, content))
    {
        // Abort parsing completely if a syntax rule is violated
        std::cerr << "Syntax Error: Expected '" << content
                  << "' but got '" << current_token.content << "'\n";
        exit(1);
    }
}

// ==========================================
// 2. CONSTRUCTOR
// ==========================================

Parser::Parser(std::string_view src, AstArena &arena)
    : lexer(src), arena(arena)
{
    advance(); // Prime the lexer by loading the very first token into current_token
}

// ==========================================
// 3. TOP-LEVEL MODULE PARSING
// ==========================================

Module Parser::parseModule()
{
    // 1. Module Keyword and Name
    expect(TokenType::Keyword, "module");
    std::string_view mod_name = current_token.content;
    expect(TokenType::Identifier);

    // 2. Parse Parameters (e.g., #(parameter WIDTH = 8) )
    auto params = std::pmr::vector<Parameter>(arena.allocator());
    if (match(TokenType::Symbol, "#"))
    {
        expect(TokenType::Symbol, "(");

        while (current_token.type == TokenType::Keyword && current_token.content == "parameter")
        {
            advance(); // Consume 'parameter'

            std::string_view p_name = current_token.content;
            expect(TokenType::Identifier);
            expect(TokenType::Symbol, "=");

            Expression p_val = parseExpression();
            params.push_back(Parameter{p_name, p_val});

            // Allow trailing or separating commas
            if (current_token.content == ",")
                advance();
        }
        expect(TokenType::Symbol, ")");
    }

    // 3. Parse Ports List (e.g., (input clk, output reg [7:0] data) )
    auto ports = std::pmr::vector<Port>(arena.allocator());
    expect(TokenType::Symbol, "(");
    while (current_token.type != TokenType::Symbol || current_token.content != ")")
    {
        ports.push_back(parsePort());

        // Skip commas between ports
        if (current_token.content == ",")
            advance();
    }
    expect(TokenType::Symbol, ")");
    expect(TokenType::Symbol, ";"); // End of module signature

    // 4. Skip internal reg/wire declarations
    // This is a simplified behavior that skips internal structural declarations.
    // It consumes everything up to the semicolon for 'reg' and 'wire' lines.
    while (current_token.type == TokenType::Keyword &&
           (current_token.content == "reg" || current_token.content == "wire"))
    {
        while (!(current_token.type == TokenType::Symbol && current_token.content == ";") &&
               current_token.type != TokenType::EndToken)
        {
            advance();
        }
        advance(); // Consume the terminating ';'
    }

    // 5. Parse Module Items (Currently only handles 'always' blocks)
    auto items = std::pmr::vector<ModuleItem>(arena.allocator());
    while (current_token.type != TokenType::Keyword || current_token.content != "endmodule")
    {
        if (current_token.content == "always")
        {
            items.push_back(parseAlwaysBlock());
        }
        else
        {
            std::cerr << "Syntax Error: Unknown module item: " << current_token.content << "\n";
            exit(1);
        }
    }
    expect(TokenType::Keyword, "endmodule");

    // Move semantics are used here to transfer the pmr::vectors directly into the AST node
    return Module{mod_name, std::move(params), std::move(ports), std::move(items)};
}

// ==========================================
// 4. HARDWARE BLOCK PARSING
// ==========================================

Port Parser::parsePort()
{
    // Expected Grammar: <direction> [reg] [ [msb:lsb] ] <name>

    PortDir dir = PortDir::Input; // Default fallback direction
    if (match(TokenType::Keyword, "input"))
        dir = PortDir::Input;
    else if (match(TokenType::Keyword, "output"))
        dir = PortDir::Output;

    bool is_reg = match(TokenType::Keyword, "reg");

    std::optional<Range> range = std::nullopt;
    if (match(TokenType::Symbol, "["))
    {
        Expression msb = parseExpression();
        expect(TokenType::Symbol, ":");
        Expression lsb = parseExpression();
        expect(TokenType::Symbol, "]");
        range = Range{msb, lsb};
    }

    std::string_view name = current_token.content;
    expect(TokenType::Identifier);

    return Port{dir, is_reg, range, name};
}

AlwaysBlock *Parser::parseAlwaysBlock()
{
    // Expected Grammar: always @(posedge clk or negedge rst) <body>

    expect(TokenType::Keyword, "always");
    expect(TokenType::Symbol, "@");
    expect(TokenType::Symbol, "(");

    auto sens = std::pmr::vector<Sensitivity>(arena.allocator());

    // Parse sensitivity list
    while (current_token.type != TokenType::Symbol || current_token.content != ")")
    {
        Edge edge = Edge::None;
        if (match(TokenType::Keyword, "posedge"))
            edge = Edge::Posedge;
        else if (match(TokenType::Keyword, "negedge"))
            edge = Edge::Negedge;

        // Extract signal name (handles plain identifiers or symbols like '*')
        if (current_token.type == TokenType::Identifier || current_token.type == TokenType::Symbol)
        {
            std::string_view sig = current_token.content;
            advance();
            sens.push_back(Sensitivity{edge, Identifier{sig}});
        }

        // Consume optional Verilog separators ("or" keyword or comma)
        match(TokenType::Keyword, "or");
        match(TokenType::Symbol, ",");
    }
    expect(TokenType::Symbol, ")");

    Statement body = parseStatement();

    // Allocate the AlwaysBlock directly inside the memory arena and return its pointer
    return arena.alloc<AlwaysBlock>(AlwaysBlock{std::move(sens), body});
}

// ==========================================
// 5. STATEMENT PARSING
// ==========================================

Statement Parser::parseStatement()
{
    // 1. Block Statement (begin ... end)
    if (match(TokenType::Keyword, "begin"))
    {
        auto stmts = std::pmr::vector<Statement>(arena.allocator());
        while (current_token.type != TokenType::Keyword || current_token.content != "end")
        {
            stmts.push_back(parseStatement()); // Recursively parse inner statements
        }
        expect(TokenType::Keyword, "end");

        return arena.alloc<BlockStmt>(BlockStmt{std::move(stmts)});
    }

    // 2. If-Else Statement
    if (match(TokenType::Keyword, "if"))
    {
        expect(TokenType::Symbol, "(");
        Expression cond = parseExpression();
        expect(TokenType::Symbol, ")");

        Statement true_b = parseStatement();
        std::optional<Statement> false_b = std::nullopt;

        // Dangling-else resolution: eagerly grabs the first 'else' it sees
        if (match(TokenType::Keyword, "else"))
            false_b = parseStatement();

        return arena.alloc<IfStmt>(IfStmt{cond, true_b, false_b});
    }

    // 3. Case Statement
    if (match(TokenType::Keyword, "case"))
    {
        expect(TokenType::Symbol, "(");
        Expression cond = parseExpression();
        expect(TokenType::Symbol, ")");

        auto branches = std::pmr::vector<std::pair<Expression, Statement>>(arena.allocator());
        std::optional<Statement> def_branch = std::nullopt;

        while (current_token.type != TokenType::Keyword || current_token.content != "endcase")
        {
            if (match(TokenType::Keyword, "default"))
            {
                expect(TokenType::Symbol, ":");
                def_branch = parseStatement();
            }
            else
            {
                Expression item = parseExpression();
                expect(TokenType::Symbol, ":");
                branches.push_back({item, parseStatement()});
            }
        }
        expect(TokenType::Keyword, "endcase");

        return arena.alloc<CaseStmt>(CaseStmt{cond, std::move(branches), def_branch});
    }

    // 4. Non-Blocking Assignment (lhs <= rhs ;)
    if (current_token.type == TokenType::Identifier)
    {
        Expression lhs = Identifier{current_token.content};
        advance();

        expect(TokenType::Symbol, "<="); // Note: currently strictly enforces non-blocking

        Expression rhs = parseExpression();
        expect(TokenType::Symbol, ";");

        return arena.alloc<NonBlockingAssign>(NonBlockingAssign{lhs, rhs});
    }

    std::cerr << "Syntax Error in Statement: Unexpected token '" << current_token.content << "'\n";
    exit(1);
}

// ==========================================
// 6. EXPRESSION PARSING
// ==========================================

Expression Parser::parseExpression()
{
    Expression left;

    // Evaluate the atomic Left-Hand Side (LHS)
    if (current_token.type == TokenType::Identifier)
    {
        left = Identifier{current_token.content};
        advance();
    }
    else if (current_token.type == TokenType::Number)
    {
        left = Number{current_token.content};
        advance();
    }

    // Lookahead for a binary operator
    // Currently implemented using a simple right-recursive approach.
    // Note: This does not enforce standard operator precedence (e.g., * before +).
    if (current_token.type == TokenType::Symbol &&
        (current_token.content == "+" || current_token.content == "-" || current_token.content == "=="))
    {
        std::string_view op = current_token.content;
        advance();

        // Recursively evaluate the Right-Hand Side (RHS)
        Expression right = parseExpression();

        return arena.alloc<BinaryExpr>(BinaryExpr{op, left, right});
    }

    return left;
}