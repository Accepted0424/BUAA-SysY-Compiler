# 语法分析说明

本文档描述本项目的语法分析（Parser）实现方式、核心流程、文法结构与错误恢复策略。

## 入口与总体流程
- 入口类：`Parser`（`src/frontend/include/parser.h`，实现见 `src/frontend/parser.cpp`）。
- 入口方法：`Parser::parse()`，内部调用 `parseCompUnit()`。
- 产出：构建 AST（见 `src/frontend/include/ast.h`），由后续 visitor 生成 IR。

## Token 流与前瞻机制
- 使用 `Lexer::next()` 获取 Token（`cur_`、`lookahead_`、`last_` 三个指针）。
- `getToken()`：推进 Token 流，同时维护调试日志。
- `ungetToken()`：仅允许回退一次（用于判断歧义场景）。
- `match(expected)`：匹配当前 Token 并推进；若不匹配，会触发错误上报。

## 关键文法结构（简化）
- 编译单元：`CompUnit -> {Decl} {FuncDef} MainFuncDef`
- 声明：`Decl -> ConstDecl | VarDecl`
- 常量声明：`ConstDecl -> 'const' BType ConstDef { ',' ConstDef } ';'`
- 变量声明：`VarDecl -> ['static'] BType VarDef { ',' VarDef } ';'`
- 表达式：`Exp -> AddExp`（递归下降链路：AddExp -> MulExp -> UnaryExp -> PrimaryExp）
- 语句：支持赋值、表达式、块、if/else、for、break/continue、return、printf。
- 函数：`FuncDef -> FuncType Ident '(' [FuncFParams] ')' Block`
- 主函数：`MainFuncDef -> 'int' 'main' '(' ')' Block`

## 递归下降解析结构
- `parseCompUnit`：顶层循环解析 Decl/FuncDef/MainFuncDef，使用前瞻区分变量声明与函数定义。
- `parseDecl` / `parseConstDecl` / `parseVarDecl`：处理常量与变量声明，支持多项逗号分隔。
- `parseStmt`：处理全部语句类型，包含赋值、表达式、块、控制流与 printf。
- `parseExp` 及其链路：保持运算符优先级与左结合。

## 关键歧义处理
- 赋值语句 vs 表达式语句：
  - 两者都可能以 `IDENFR` 开始。
  - 实现：先尝试 `parseLValSilent()`，若后续遇到 `=` 则视为赋值，否则退回解析表达式。
- 变量声明 vs 函数定义：
  - `int ident` 之后若出现 `(` 则解析函数，否则解析变量声明。
  - 实现：`parseCompUnit` 中使用 `getToken` + `ungetToken` 做一次前瞻判断。

## 错误处理与恢复
- `match` 对缺失符号进行定点报错：
  - 缺 `;` 报 `ERR_MISSING_SEMICOLON`
  - 缺 `)` 报 `ERR_MISSING_RPARENT`
  - 缺 `]` 报 `ERR_MISSING_RBRACK`
- `skipUntilSemicn()`：在部分错误场景下跳过到下一个分号，避免级联错误。
- `parseCompUnit` 若未找到 `MainFuncDef` 会报错并返回空指针。

## 语句解析细节
- `if`：解析条件后分别解析 then/else 语句，else 为可选。
- `for`：支持 `forStmtFirst ; cond ; forStmtSecond` 三段可选结构。
- `return`：支持有无返回表达式两种形式。
- `printf`：要求首参为 `StringConst`，后续可跟 `, Exp` 列表。

## 维护建议
- 新增语法时优先补齐 AST 节点定义，再扩展对应 `parse*` 方法。
- 若引入新的 Token，请同步更新 `token.h` 与 `lexer.cpp`。
- 若语法导致新的歧义，优先使用轻量级前瞻与局部回退处理。
