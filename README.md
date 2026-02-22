# 🧠 Modern C++ Verilog Static Analyzer

A lightweight **Verilog parser and static linter** built in **C++17**, demonstrating compiler-style EDA toolchain design: zero-copy lexing, parsing, arena-based AST construction, and static semantic analysis — with no external dependencies.

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

---


## Modern C++ Design

| Area | Technique | Effect |
|------|-----------|--------|
| **Zero-copy lexing** | `std::string_view` throughout the token stream | No heap allocation during tokenization; tokens are slices into the source buffer |
| **Arena allocation** | `std::pmr::monotonic_buffer_resource` + `std::pmr::polymorphic_allocator` | Eliminates per-node `malloc`; sequential bump allocation improves cache locality |
| **Type-safe AST nodes** | `std::variant` + `std::visit` | Stack-allocated tagged union; avoids vtable dispatch and pointer indirection |
| **Fast number parsing** | `std::from_chars` (no locale, no allocation) | Faster than `stoi`/`strtol`; no locale lookup, no string copy |
| **Null safety** | `std::optional` for fallible lookups | Zero overhead vs. raw pointer; communicates absence without heap allocation |
| **Scoped enumerations** | `enum class` for token and node kinds | No implicit conversion overhead; compiler-enforced type safety at zero runtime cost |

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