# 词法分析说明

本文档描述本项目词法分析（Lexer）的职责、实现流程、Token 定义以及关键边界情况，便于理解与维护。

## 入口与数据流
- 入口类：`Lexer`（`src/frontend/include/lexer.h`，实现见 `src/frontend/lexer.cpp`）。
- 调用方式：外部通过 `Lexer::next(Token &token)` 逐个获取 Token。
- 输入来源：构造时传入 `std::istream`（通常为源码文件流）。
- 行号维护：`getChar()` 遇到 `\n` 时递增 `lineno_`；`ungetChar()` 回退时相应减一。

## Token 类型与关键字
定义文件：`src/frontend/include/token.h`

Token 类型包含：
- 标识符与常量：`IDENFR`、`INTCON`、`STRCON`。
- 关键字：`const`、`int`、`static`、`break`、`continue`、`if`、`main`、`else`、`for`、`return`、`void`、`printf`。
- 运算符与分隔符：`+ - * / %`、`< <= > >= == !=`、`= ! && ||`、`( ) [ ] { } , ;`。
- 结束符：`EOFTK`。

关键字映射：`Lexer::keywords_`（`src/frontend/lexer.cpp`）用于区分标识符与保留字。

## 扫描流程（主函数 `next`）
核心流程位于 `Lexer::next(Token &token)`：
1. 读取一个字符 `ch`。
2. 按字符类别分派：
   - 字母或下划线：识别标识符/关键字（`lexIdentifier`）。
   - 数字：识别整数常量（`lexIntConst`）。
   - 双引号：识别字符串常量（`lexStringConst`）。
   - `&`/`|`：识别 `&&`/`||`（`lexAndExpr` / `lexOrExpr`）。
   - `!`/`=`/`<`/`>`：尝试匹配双字符运算符，否则回退并产生单字符 Token。
   - `/`：先尝试识别注释；不是注释则为除号。
   - 空白（空格、制表）和换行：跳过并递归继续读取下一个 Token。
   - EOF：返回 `EOFTK`。

## 词法子过程说明
- `lexIdentifier`：
  - 读取 `[a-zA-Z_][a-zA-Z0-9_]*`。
  - 若命中关键字表，则返回关键字 Token，否则为 `IDENFR`。
- `lexIntConst`：
  - 读取连续数字序列，返回 `INTCON`。
- `lexStringConst`：
  - 以 `"` 开始，读取直到下一个 `"`。
  - 内容不进行转义处理（原样读取）。
- `lexAndExpr` / `lexOrExpr`：
  - 期望第二个字符分别为 `&` / `|` 以构成 `&&` / `||`。
  - 若不匹配，报错并仍返回对应逻辑运算 Token。
- 注释处理：
  - `//` 单行注释：读到换行或 EOF。
  - `/* ... */` 块注释：读取直到匹配 `*/`；若 EOF 前未闭合，返回失败。

## 行号与错误处理
- 行号：`lineno_` 从 1 开始，`getChar`/`ungetChar` 在换行处维护。
- 错误报告：
  - 非法字符：记录日志（`LOG_ERROR`）。
  - `&&`/`||` 未闭合：记录错误并报 `ERR_ILLEGAL_SYMBOL`。
  - 块注释未闭合：返回失败，最终由上层处理。

## 重要行为与限制
- 仅识别十进制整数；不支持十六进制、浮点数。
- 字符串不处理转义序列，读取到下一个 `"` 即结束。
- `ungetChar` 不支持连续回退（见注释说明）。
- 空白字符会被跳过，不会产生 Token。

## 常见排查点
- 若关键字被误识别为标识符：检查 `keywords_` 表或输入大小写。
- 若注释导致卡死：检查块注释是否缺失闭合 `*/`。
- 行号错误：检查是否存在非法的多次 `ungetChar` 调用。
