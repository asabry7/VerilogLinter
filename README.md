# 🧠 Modern C++ Verilog Static Analyzer

A lightweight **Verilog parser and static linter** built in **C++17**, demonstrating compiler-style EDA toolchain design: zero-copy lexing, recursive-descent parsing, arena-based AST construction, and static semantic analysis — with no external dependencies.

---

## 📑 Table of Contents

1. [Architecture](#architecture)
2. [Lint Checks](#lint-checks)
3. [Performance](#performance)
4. [Modern C++ Design](#modern-c-design)
5. [Build & Run](#build--run)
6. [Limitations](#limitations)

---

## Architecture

The pipeline has three stages, each designed around allocation efficiency and zero-copy data flow:

**Lexer** — Zero-copy tokenization via `std::string_view`. Manual character classification (no regex), no heap allocation during the lex pass. Handles identifiers, keywords, Verilog number literals (`8'hFF`), and `//` / `/* */` comments.

**Parser** — Recursive-descent parser that builds an AST entirely within a PMR monotonic arena. Nodes are represented with `std::variant`. Supports modules, parameters, ports, `always` blocks, non-blocking assignments, `if/else`, and `case` statements.

**Linter** — Semantic and structural analysis pass over the AST, performing constant folding and bit-width propagation to power the checks below.

---

## Lint Checks

- Multi-driven registers
- Bit-width mismatches and overflow
- Latch inference
- Missing `default` in `case`
- Uninitialized registers
- Unreachable FSM states
- Constant expression evaluation

---

## Performance

Benchmarked on a **700K-line, 25 MB** Verilog file with [`hyperfine`](https://github.com/sharkdp/hyperfine) (10 warm-up runs):

| Metric | Result |
|--------|--------|
| Mean | **682.7 ms ± 4.8 ms** |
| Min / Max | 676.5 ms / 690.9 ms |

Performance is driven by two architectural decisions: **PMR arena allocation** eliminates per-node `malloc` overhead throughout the parse phase, and the **zero-copy lexer** avoids any string materialization — tokens are `string_view` slices into the original source buffer. The result is that nearly all runtime is I/O and process startup; the analysis itself is negligible.

Hyperfine usage:
```bash
asabry@asabry-pc:~/Cpp/Linter/build$ hyperfine --warmup 10 './VerilogLinter ../huge_adder.v'
Benchmark 1: ./VerilogLinter ../huge_adder.v
  Time (mean ± σ):     682.7 ms ±   4.8 ms    [User: 667.5 ms, System: 15.0 ms]
  Range (min … max):   676.5 ms … 690.9 ms    10 runs

```

---

## Modern C++ Design

| Area | Technique |
|------|-----------|
| **Zero-copy lexing** | `std::string_view` throughout the token stream |
| **Arena allocation** | `std::pmr::monotonic_buffer_resource` + `std::pmr::polymorphic_allocator` |
| **Type-safe AST nodes** | `std::variant` + `std::visit` |
| **Fast number parsing** | `std::from_chars` (no locale, no allocation) |
| **Null safety** | `std::optional` for fallible lookups |
| **Scoped enumerations** | `enum class` for token and node kinds |

---

## Build & Run

```bash
mkdir build && cd build
cmake ..
make

./VerilogLinter file.v
```

---

## Limitations

This tool targets a practical subset of Verilog, not the full IEEE spec:

- Operators limited to `+`, `-`, `==`
- No blocking assignments (`=`), generate blocks, or hierarchical modules
- No elaboration phase
- Simplified error handling (`exit(1)`)