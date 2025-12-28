# 语义分析说明

本文档说明当前编译器的语义分析职责、符号表设计、类型与作用域规则、以及错误检查点。语义分析主要在 AST Visitor 阶段完成（`src/frontend/visitor.cpp`）。

## 总体职责与阶段
- 在遍历 AST 时完成语义检查，同时生成 IR。
- 维护符号表与作用域栈，确保标识符解析与类型一致性。
- 对常量表达式进行求值（用于常量数组大小、常量初始化与常量折叠）。

## 符号表与作用域
- 实现位置：`src/frontend/include/symtable.h`。
- 作用域模型：`SymbolTable` 形成树状结构，`pushScope`/`popScope` 管理嵌套块。
- 符号类型：`INT / INT_ARRAY / CONST_INT / CONST_INT_ARRAY / STATIC_INT / STATIC_INT_ARRAY / VOID_FUNC / INT_FUNC`。
- 重定义检查：`addSymbol` 在当前作用域内重复定义会报 `ERR_REDEFINED_NAME`。
- 查找规则：`existInSymTable`/`getSymbol` 会向父作用域递归查找。

## 类型系统与值类别
- 语言类型：以 `int` 为核心；数组为 `int[]`；函数返回类型为 `int` 或 `void`。
- 数组与指针行为：数组参数会以“指针样式”传递（`ArrayType` 元素数为负表示指针语义）。
- 常量：常量值以 `ConstantInt` 或常量数组存储，局部常量在 IR 中尽量保留为立即数。

## 关键语义检查点
位置：`src/frontend/visitor.cpp`

### 1) 未定义标识符
- 触发：`getLValAddress`、赋值语句、`for` 语句赋值目标。
- 错误：`ERR_UNDEFINED_NAME`。

### 2) 常量赋值限制
- 常量或常量数组不可作为赋值目标。
- 触发位置：`visitStmt` 的赋值语句、`visitForStmt`。
- 错误：`ERR_CONST_ASSIGNMENT`。

### 3) 函数调用参数检查
- 参数个数：与函数签名比较，不一致报 `ERR_FUNC_ARG_COUNT_MISMATCH`。
- 参数类型：标量/数组维度不匹配时报 `ERR_FUNC_ARG_TYPE_MISMATCH`。
- 数组实参处理：当形参为指针型数组（元素数 < 0）时，实参数组自动 decay（GEP 取首元素地址）。

### 4) return 语句检查
- void 函数返回表达式时报 `ERR_VOID_FUNC_RETURN_MISMATCH`。
- 非 void 函数末尾缺少 return：
  - 在函数块结束处检查，报 `ERR_NONVOID_FUNC_MISSING_RETURN`。

### 5) printf 参数一致性
- 统计格式串中的 `%d` 个数，与表达式个数比较。
- 不一致时报 `ERR_PRINTF_ARG_MISMATCH`。

### 6) break/continue 位置约束
- 非循环上下文使用 `break`/`continue`，报 `ERR_BREAK_CONTINUE_OUTSIDE_LOOP`。
- 由 `inForLoop_` 与 `breakTargets_`/`continueTargets_` 维护。

## 常量求值与语义辅助
- 常量表达式求值：`evalConstExpValue` / `evalConstConstExp` / `evalConstAddWithLVal`。
- 常量数组索引读取：`constValueOfLVal` 支持 `const int a[]` 在编译期取值。
- 用途：
  - 常量数组大小与初始化。
  - 表达式常量折叠与 IR 简化（优化详见 `docs/compiler-optimizations.md`）。

## 内建函数与全局环境
- 在 `Visitor::visit` 中注入内建函数：
  - `getint() -> int`
  - `putint(int) -> void`
  - `putch(int) -> void`
  - `putstr(int[]) -> void`
- 这些函数进入全局符号表，可被语义检查与调用解析使用。

## 常见维护建议
- 新增类型或语义规则时，先扩展 `SymbolType` 与 `Symbol` 结构，再补齐检查点。
- 变更函数签名或内建函数时，需同步调整参数检查与 decay 逻辑。
- 新增错误类型时，更新 `error.h` 并在语义路径中明确触发条件。
