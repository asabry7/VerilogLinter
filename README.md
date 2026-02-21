# 🧠 Modern C++ Verilog Static Analyzer (EDA Showcase)

A lightweight **Verilog parser and static linter** implemented in modern
C++17/20 style.

This project demonstrates modern C++ capabilities applied to a
compiler-style EDA toolchain, including lexical analysis, recursive
descent parsing, arena-based AST construction, and static semantic
analysis.

------------------------------------------------------------------------

# 📑 Table of Contents

1.  [Project Overview](#project-overview)\
2.  [How to Compile and Run](#how-to-compile-and-run)\
3.  [Performance](#performance)\
4.  [Architecture Overview](#architecture-overview)
    -   [Lexer](#lexer)\
    -   [Parser](#parser)\
    -   [Linter (Static Analyzer)](#linter-static-analyzer)\
5.  [Main Features](#main-features)\
6.  [Limitations](#limitations)\
7.  [Modern C++ Features Used](#modern-c-features-used)\
8.  [EDA Industry Relevance](#eda-industry-relevance)

------------------------------------------------------------------------

# Project Overview

This project implements a simplified Verilog Linter consisting of:

-   A **Lexical Analyzer**
-   A **Parser**
-   An **Arena-allocated Abstract Syntax Tree (AST)**
-   A **Static Linter for hardware rule checking**

![alt text](images/image.png)
------------------------------------------------------------------------

# How to Compile and Run

```bash
cd build
cmake ..
make

./VerilogLinter verilogFile.v
```


------------------------------------------------------------------------
# Main Features

-   Zero-copy lexical analysis
-   Arena-based AST allocation
-   Recursive descent parsing
-   Static constant folding
-   Bit-width propagation analysis
-   Structural HDL lint rules
-   Clear separation of compilation phases
-   Clean modern C++ design
-   No external dependencies

------------------------------------------------------------------------

# Limitations

-   Subset of Verilog only (not full IEEE spec)
-   Limited operator support (`+`, `-`, `==`)
-   No blocking assignments (`=`)
-   No generate blocks
-   No hierarchical modules
-   No elaboration phase
-   No full type system
-   Error handling uses immediate termination (`exit(1)`)

This project prioritizes architectural clarity and modern C++ showcase
over full language coverage.

------------------------------------------------------------------------

# Modern C++ Features Used

### Memory Management

-   `std::pmr::monotonic_buffer_resource`
-   `std::pmr::polymorphic_allocator`
-   Arena allocation for AST nodes

### Type Safety & Expressiveness

-   `std::variant` for tagged unions
-   `std::optional` for nullable semantics
-   `std::visit` with overloaded pattern
-   `enum class` for strong typing
-   Structured bindings

### Performance-Oriented Design

-   `std::string_view` for zero-copy tokens
-   `std::from_chars` for fast numeric parsing
-   Custom constant evaluation
-   Deterministic allocation strategy

### STL Usage

-   `std::unordered_map`
-   `std::unordered_set`
-   `std::pmr::vector`
-   `std::filesystem`

The project reflects production-level modern C++ design suitable for
compilers, static analyzers, and EDA tools.


------------------------------------------------------------------------


# Architecture Overview

## Lexer

The `LexicalAnalyzer` converts raw Verilog source into tokens.

Key characteristics:

-   Zero-copy tokenization using `std::string_view`
-   Manual character classification (no regex)
-   Support for:
    -   Identifiers
    -   Keywords
    -   Number literals (including Verilog formats like `8'hFF`)
    -   Symbols
    -   Comment skipping (`//` and `/* */`)

Design goals: - Deterministic performance - No dynamic allocation during
tokenization - Clear separation between lexical and syntactic stages

------------------------------------------------------------------------

## Parser

The `VerilogParser` implements a **recursive descent parser**.

It builds a structured AST using:

-   `std::variant` for polymorphic node types
-   Arena allocation via `std::pmr::monotonic_buffer_resource`
-   Strong ownership model (no raw heap fragmentation)

Supported constructs:

-   Modules
-   Parameters
-   Ports with bit ranges
-   Always blocks
-   Non-blocking assignments (`<=`)
-   If / Else
-   Case statements

Design philosophy:

-   Compiler-style architecture
-   Clear grammar mapping to parsing functions
-   Memory locality via arena allocation

------------------------------------------------------------------------

## Linter (Static Analyzer)

The `VerilogStaticLinter` performs semantic and structural analysis on
the AST.

It implements:

-   Constant expression evaluation
-   Bit-width inference
-   Structural rule validation

Detected violations include:

-   Multi-driven registers
-   Width mismatches / carry overflow
-   Latch inference in combinational blocks
-   Missing default in case statements
-   Unreachable FSM states
-   Uninitialized registers
-   Constant math overflow

The linter demonstrates static reasoning similar to industrial HDL
analyzers.

------------------------------------------------------------------------

# Performance
![alt text](images/image-1.png)

Benchmarked with `hyperfine` over 930 runs (10 warmup runs to fill filesystem cache).

The linter completes in `0.6ms` at best and `2.1ms` on average — the variance comes entirely from OS scheduler noise and kernel interrupts, not the linter itself. 

At this timescale, process startup and dynamic linking dominate over actual lexing, parsing, and analysis time.
Performance is driven by two architectural decisions: the **PMR monotonic arena allocator**, which bump-allocates all AST nodes into a single contiguous buffer (eliminating `heap fragmentation` and improving `cache locality`), and `std::string_view` throughout the lexer and AST, which avoids copying the source file entirely — every identifier, keyword, and number in the AST is a zero-cost reference into the original input buffer.

------------------------------------------------------------------------

------------------------------------------------------------------------

