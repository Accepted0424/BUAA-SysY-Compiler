# Repository Guidelines

## Project Structure & Module Organization
- `src/main.cpp` is the compiler entry point.
- `src/frontend/` contains the lexer, parser, AST, and visitor-based IR generation.
- `src/llvm/` contains IR types/values and MIPS/ASM emitters.
- `test/` holds sample inputs and grading corpora; `test/myTestcase/` includes helper scripts.
- `docs/` stores grammar notes.
- Top-level artifacts like `llvm_ir.txt`, `mips.txt`, and `ans.txt` are generated outputs.

## Build, Test, and Development Commands
- Configure and build:
  - `cmake -S . -B cmake-build-debug`
  - `cmake --build cmake-build-debug`
  - Output binary: `cmake-build-debug/Compiler`
- Run the bundled IR runner:
  - `./myCompiler.sh run` (builds `io.c` into `libio.so` and runs `llvm_ir.txt` with `lli`)
- Lexer regression helper:
  - `bash test/myTestcase/lexer-test.sh` (runs local `testfile*.txt` with `input*.txt`)

## Coding Style & Naming Conventions
- C++17, follow existing formatting (4-space indentation, braces on the same line).
- Types/classes use `PascalCase`; functions and variables use `camelCase`.
- Keep IR/AST value types aligned with existing `ValueType` and `Type` conventions.
- No formatter or linter is configured; keep diffs minimal and consistent.

## Testing Guidelines
- No unit test framework is configured; rely on input/output fixtures in `test/`.
- For lexer-related checks, use `test/myTestcase/lexer-test.sh`.
- When changing codegen or IR, compare regenerated `llvm_ir.txt`/`mips.txt` manually.

## Commit & Pull Request Guidelines
- Commit messages follow a short prefix and summary pattern (e.g., `optimize: ...`, `hw5: ...`).
- PRs should include:
  - A brief description of the change and rationale.
  - Testing notes (commands run and results).
  - Links to related tasks/issues if applicable.

## Configuration & Assets
- `config.json` controls compiler options for some test harnesses.
- `io.c` provides runtime I/O helpers used by `myCompiler.sh run`.
