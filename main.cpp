#include <iostream>
#include "Lexer.h"

/* Helper function for printing */
const char *toString(TokenType t)
{
    switch (t)
    {
    case TokenType::Keyword:
        return "Keyword";
    case TokenType::Identifier:
        return "Identifier";
    case TokenType::Number:
        return "Number";
    case TokenType::Symbol:
        return "Symbol";
    case TokenType::EndToken:
        return "End";
    default:
        return "Unknown";
    }
}

int main()
{

    /* Verilog Code: */
    std::string verilog = R"(module counter #(parameter WIDTH = 8) (
            input clk,
            input rst,
            output reg [WIDTH-1:0] count
        );
            // This is a line comment
            always @(posedge clk or posedge rst) begin
                if (rst) 
                    count <= 8'h00; /* reset count */
                else 
                    count <= count + 1; // Increment
            end
        endmodule
    )";

    /* Initialize the Lexer*/
    Lexer lexer(verilog);

    while (true)
    {
        Token token = lexer.next();

        if (token.type == TokenType::EndToken)
            break;

        std::cout
            << toString(token.type)
            << " -> \""
            << token.content
            << "\"\n";
    }
}
