# 🧠 Modern C++ Verilog Static Analyzer

A lightweight **Verilog parser and static linter** built in **C++17**.\
It demonstrates modern C++ applied to a compiler-style EDA toolchain:
lexical analysis, recursive-descent parsing, arena-based AST
construction, and static semantic analysis.

------------------------------------------------------------------------


## 📑 Table of Contents

1. [Overview](#overview)
2. [Build & Run](#build--run)
3. [Architecture](#architecture)
   - [Lexer](#lexer)
   - [Parser](#parser)
   - [Linter](#linter)
4. [Features](#features)
5. [Limitations](#limitations)
6. [Modern C++ Techniques](#modern-c-techniques)
7. [Performance](#performance)

------------------------------------------------------------------------

## Overview

This project implements a simplified Verilog static analyzer consisting
of:

-   **Lexer** -- zero-copy tokenization\
-   **Parser** -- recursive descent AST builder\
-   **Arena-allocated AST** -- cache-friendly memory model\
-   **Static Linter** -- semantic and structural rule checks

The focus is architectural clarity and performance rather than full IEEE
Verilog coverage.

------------------------------------------------------------------------

## Build & Run

``` bash
mkdir build
cd build
cmake ..
make

./VerilogLinter file.v
```

------------------------------------------------------------------------

## Architecture

### Lexer

-   Zero-copy tokenization using `std::string_view`
-   Manual character classification (no regex)
-   Supports identifiers, keywords, number literals (e.g., `8'hFF`),
    symbols
-   Skips `//` and `/* */` comments
-   No dynamic allocation during lexing

### Parser

-   Recursive-descent parsing
-   Arena allocation using PMR
-   Supports modules, parameters, ports, always blocks, non-blocking
    assignments, if/else, and case statements
-   Uses `std::variant` for AST node representation

### Linter

Performs static semantic and structural checks: - Constant expression
evaluation - Bit-width inference - Multi-driven registers - Width
mismatches / overflow - Latch inference - Missing `default` in case -
Uninitialized registers - Unreachable FSM states

------------------------------------------------------------------------

## Features

-   Zero-copy lexer
-   Arena-based AST allocation
-   Recursive-descent parsing
-   Constant folding
-   Bit-width propagation
-   Structural HDL lint rules
-   No external dependencies

------------------------------------------------------------------------

## Limitations

-   Subset of Verilog (not full IEEE spec)
-   Limited operator support (`+`, `-`, `==`)
-   No blocking assignments (`=`)
-   No generate blocks
-   No hierarchical modules
-   No elaboration phase
-   Simplified error handling (`exit(1)`)

------------------------------------------------------------------------

## Modern C++ Techniques

### Memory

-   `std::pmr::monotonic_buffer_resource`
-   `std::pmr::polymorphic_allocator`

### Type Safety

-   `std::variant`
-   `std::optional`
-   `std::visit`
-   `enum class`

### Performance

-   `std::string_view`
-   `std::from_chars`
-   Deterministic allocation strategy

------------------------------------------------------------------------

## Performance

Benchmarked with `hyperfine` (930 runs, 10 warmups):

-   Best: 0.6 ms\
-   Average: 2.1 ms

Runtime is dominated by process startup and dynamic linking.\
Performance gains come from PMR arena allocation and zero-copy design.