## 1. 项目目标与实现范围

### 1.1 目标语言（SysY 子集）

本项目实现一个面向教学与实验验证的编译器原型，目标语言为 **SysY 语言子集**，覆盖典型的过程式语言要素：

* 基本类型：`int`
* 复合对象：一维/多维数组（含局部与全局数组初始化）
* 程序结构：函数定义、函数调用、主函数
* 控制流：`if/else`、`for/while`、`break/continue`、`return`
* I/O 与内建：`getint`、`printf`（并在全局环境注入 `putint/putch/putstr`）

### 1.2 输出目标

编译器支持双通道输出，兼顾可观测性与可执行性：

1. **LLVM 风格 IR 文本**（内部 LLVM-like IR 的序列化输出，便于调试与验证）
2. **MIPS 汇编**（面向实验环境/模拟器的目标代码）

---

## 2. 参考架构与总体设计理念（对标 LLVM）

### 2.1 分层结构

参考 LLVM 的经典分层模型，整体流程划分为：

**前端（Lexer/Parser/Semantic） → 统一中间表示（LLVM-like IR） → 中间层优化（Pass Pipeline） → 后端（MIPS 代码生成与输出）**

该结构的关键收益在于：以 IR 作为稳定契约解耦前后端，实现“多前端/多后端可组合”的工程能力，同时便于在 IR 层集中实现目标无关优化。

### 2.2 IR 核心实体

IR 基础设施对齐 LLVM 的核心建模，主要对象包括：

* `LLVMContext`：上下文与全局唯一对象管理
* `Module`：编译单元容器（函数/全局变量）
* `Function`：函数级表示（参数、基本块集合）
* `BasicBlock`：基本块（控制流图节点）
* `Instruction`：指令（SSA 值定义）
* `Type` / `Value`：类型系统与值体系分离，Value 统一抽象“指令结果/常量/参数/全局符号”等

---

## 3. 编译流程与模块划分

### 3.1 总体流水线

编译器采用分阶段流水线组织：

1. **Lexer**：字符流 → Token 流
2. **Parser**：Token 流 → AST
3. **Semantic**：AST + SymbolTable → 语义检查 + 常量求值（并与 IR 生成协同）
4. **IRGen**：AST → 自定义 LLVM-like IR（Module/Function/BasicBlock/Instruction）
5. **Optimize**：IR → 轻量优化（常量折叠、DCE、CFG 化简），多轮迭代至稳定
6. **CodeGen**：IR → MIPS 汇编（可选输出 IR 文本）

### 3.2 工程目录与职责边界

* `src/main.cpp`：阶段驱动、参数与输出管理（统一入口）
* `src/frontend/`：Lexer / Parser / AST / SymbolTable / Error
* `src/llvm/`：IR 基础设施、Pass 框架、AsmWriter、MipsPrinter
* `test/`：回归测试输入与验证

---

## 4. 词法分析（Lexer）设计

### 4.1 设计目标与 Token 模型

Lexer 负责识别并产出 Token 流，覆盖：

* 关键字、标识符
* 整数常量、字符串常量
* 运算符与分隔符
* 忽略空白与注释

Token 结构包含：**类型、原始文本（lexeme）、行号**，满足错误定位与后续阶段的语义诊断需求。

### 4.2 扫描策略与优先级

采用**流式扫描**（提供 `peek/get/unget`），识别优先级如下：

空白/注释 → 关键字/标识符 → 数字 → 字符串 → 复合运算符 → 单字符符号

并在字符读取层维护行号：`getChar` 遇 `\n` 行号递增，`ungetChar` 回退时同步维护，保证定位一致性。

### 4.3 注释与复合运算符处理

* 注释：支持 `//` 单行与 `/* ... */` 块注释，统一在 `Lexer::next` 内跳过
* 复合运算符：对 `&&/||/!=/==/<=/>=` 采用专用分支函数处理，降低主流程复杂度
* 非法字符：记录 `ERR_ILLEGAL_SYMBOL` 并保持扫描推进（避免级联阻塞）

### 4.4 对外接口

统一拉取式接口：

* `Lexer::next(Token &token)`：逐个生成 Token（EOF 返回 `EOFTK`）

---

## 5. 语法分析（Parser）设计

### 5.1 解析策略

采用**递归下降（Recursive Descent）+ 单符号前瞻**，AST 节点与文法产生式保持“强对应”，便于 Visitor 遍历与后续 IR 生成。

入口接口：

* `Parser::parse()` → 返回根节点 `CompUnit`

### 5.2 歧义处理：lookahead + 回退

为解决 SysY 典型歧义：

* `LVal` 与 `FuncCall`（均以标识符开头）
* 赋值语句与表达式语句（均可能以 `IDENFR` 开头）
* 声明与函数定义（如 `int ident ...` 后续是否出现 `(`）

实现引入：

* `lookahead`：预读判断
* `ungetToken`：允许一次回退（确保解析状态可控）

并提供 `parseXxx(lval)` 重载路径：在不重复读取 Token 的前提下复用递归结构，减少状态分叉成本。

### 5.3 错误恢复机制

Parser 具备基础错误恢复能力：

* 缺少 `;`、`)`、`]`：定点报错（分别对应 `ERR_MISSING_SEMICOLON / ERR_MISSING_RPARENT / ERR_MISSING_RBRACK`）
* `skipUntilSemicn()`：跳过到同步点（通常为 `;`），避免错误级联放大
* 可选语法树标签输出：通过 `out_` 打印节点标签满足课程输出要求

---

## 6. 语义分析（Semantic）设计

> 语义分析主要在 AST Visitor 阶段完成，并与 IR 生成紧密协同（`src/frontend/visitor.cpp`）。

### 6.1 语义职责范围

* 符号定义/使用检查：重定义、未定义使用
* 类型与参数匹配：函数调用实参/形参数量与类型一致性
* 返回语句检查：`void` 返回表达式、`int` 函数缺失返回等
* 常量表达式求值：用于数组维度、常量初始化与编译期折叠
* 控制流约束：`break/continue` 仅允许出现在循环上下文

### 6.2 符号表（SymbolTable）与作用域

* 支持嵌套作用域：`pushScope/popScope` 维护块结构
* 查找规则：当前作用域未命中则向父作用域递归
* 符号类型覆盖：`INT / INT_ARRAY / CONST_INT / CONST_INT_ARRAY / STATIC_INT / STATIC_INT_ARRAY / VOID_FUNC / INT_FUNC`
* 为课程输出扩展：增加 **scope id** 与 **有序输出** 能力

### 6.3 关键语义检查点

* 函数参数：数量与类型一致，否则报错
* const 赋值：禁止对 `const` 对象再次赋值
* `break/continue`：采用循环栈/目标栈（`breakTargets_ / continueTargets_`）精确约束位置合法性
* `printf`：统计格式串 `%d` 数量与表达式参数数量一致性
* `return`：

    * `void` 返回值不匹配：`ERR_VOID_FUNC_RETURN_MISMATCH`
    * 非 void 函数缺少 return：`ERR_NONVOID_FUNC_MISSING_RETURN`

### 6.4 常量求值与语义辅助

Visitor 内提供常量求值函数族，如 `evalConstExpValue` 等，用于：

* 常量数组大小与初始化
* 常量数组索引编译期读取（`constValueOfLVal`）
* 直接服务于 IR 生成阶段的折叠与简化

### 6.5 统一错误收集

所有语义错误均通过 `ErrorReporter::error` 统一记录、排序与输出，保证错误格式一致与可验证性。

---

## 7. LLVM-like IR 基础设施与生成（IRGen）

### 7.1 IR 架构与指令集合

IR 实现位于 `src/llvm/include/ir/`，核心对象为 `Module/Function/BasicBlock/Value/Instruction/Type`，并覆盖如下指令族：

* 内存类：`AllocaInst` / `LoadInst` / `StoreInst` / `GetElementPtrInst`
* 计算类：`BinaryOperator` / `UnaryOperator` / `CompareOperator` / `LogicalOperator`
* 控制流：`BranchInst` / `JumpInst` / `ReturnInst`
* 调用：`CallInst`

此外，Value 体系结构按类继承组织（`docs/llvm-value.md` 给出 mermaid 类图），体现 LLVM 的 “Value-User-Instruction” 分层建模。

### 7.2 符号到 IR 的映射策略

语义解析后得到 `Symbol`，其 `value` 字段直接指向对应 IR Value：

* 局部标量：`AllocaInst`（地址） + `Load/Store`
* 全局变量：`GlobalVariable`
* 常量：尽量使用 `ConstantInt`（局部立即数）；全局常量采用 `GlobalVariable` 初始化

作用域管理与符号表同步推进：`visitBlock/visitStmt` 内 `pushScope/popScope`。

### 7.3 变量、数组与地址计算

* 全局标量：`GlobalVariable`（可带常量初始化）
* 全局数组：`ConstantArray` + `GlobalVariable`
* 局部标量：`AllocaInst` +（可选）`Store` 初始化
* 局部数组：`AllocaInst` + 逐元素 `GEP + Store` 初始化
* 数组访问：通过 `getLValAddress` 计算元素地址，使用 `GEP` 表达偏移

数组参数采用“指针样式”传递：在类型系统中以负元素数等方式体现 decay 语义（满足 SysY 形参数组的指针化行为）。

### 7.4 表达式与布尔语义

* 以 SSA 风格生成表达式值
* `toBool` 将任意值规范化为布尔语义（例如通过 `!= 0` 的比较）
* 短路逻辑：`visitLAndExp / visitLOrExp` 通过控制流生成短路求值（避免不必要计算与副作用风险）

### 7.5 控制流结构生成

* `if`：then / else / end 三块，条件用 `BranchInst`
* `for`：cond / body / step / end 四块，并维护 `breakTargets_ / continueTargets_`
* `return`：生成 `ReturnInst`，必要时 `loadIfPointer` 取值

### 7.6 函数生成与参数处理

* 在 `visitFuncDef / visitMainFuncDef` 中创建 `Function` 与 `entry_block_`
* 参数策略：

    * 标量参数：`AllocaInst` + `Store` 入栈，作为局部变量统一处理
    * 数组参数：按指针语义传递并记录数组类型
* 结束处理：

    * `void` 函数无显式 return 时补 `ReturnInst`
    * `int` 函数缺失 return 由语义阶段报错

---

## 8. 优化设计（Pass Framework 与已实现优化）

### 8.1 设计原则与目标

优化目标定位为**轻量、可实现、可验证**的 IR 优化，在确保语义正确的前提下减少 IR 膨胀并改善 CFG 结构，为后端生成提供更规整的输入。

### 8.2 PassManager 与调度策略

* PassManager 按 **Function 粒度**执行
* 采用**多轮迭代直到不再变化（fixpoint）**的策略，保证不同 Pass 间的协同收益

默认优化通过 `addDefaultPasses` 统一启用，确保编译入口一致、行为可复现。

### 8.3 已实现 Pass 列表

1. **ConstantFoldPass**

    * 常量运算折叠
    * 代数化简（如 `x+0`、`x*1`、`x*0`、`x%1` 等）

2. **DcePass**

    * 删除无用 `alloca/store`
    * 删除无副作用且结果未使用的指令
    * 配合构建期 CSE/缓存减少临时值规模

3. **CfgSimplifyPass**

    * 常量分支静态转跳
    * 不可达块删除
    * 空跳转块合并，降低 CFG 节点数量

### 8.4 IR 生成阶段的前置优化（Visitor 内建）

除 Pass 管线外，Visitor 内同时实现若干“构建期优化”，在生成 IR 时直接减少冗余：

* 常量折叠与代数化简（对纯常量表达式直接出 `ConstantInt`）
* 局部 CSE（公共子表达式消除）+ CSE 缓存表
* 块内 load cache（并在 `store/call` 处失效），减少重复 `load`
* 在函数构建完成后执行 `runDCE` 清理无用指令

---

## 9. 后端代码生成（MIPS）设计与工程化优化

### 9.1 总体策略

* IR 文本输出：`AsmWriter` 负责序列化 IR
* MIPS 输出：`MipsPrinter` 直接遍历 IR 的 `BasicBlock/Instruction` 生成汇编
* 不引入额外 Machine IR 层：减少后端复杂度，保持 IR→汇编映射直观可追踪

### 9.2 寄存器与栈帧策略（工程优化）

在后端实现中引入若干务实的代码生成优化与规划机制（见 `docs/compiler-optimizations.md`）：

* 寄存器规划：`RegisterPlan` 维护 `Value -> Reg` 映射，提升热点值复用
* 栈槽压缩与复用：`buildFrameInfo` 线性扫描复用 SSA 临时值栈槽，仅为需要落栈者分配
* 减少回写与重复加载：块内缓存寄存器（value->reg），降低 `lw/sw` 频率

### 9.3 ABI 约定与调用规范

* 前四个整型参数：`$a0-$a3`，其余参数按约定压栈
* 返回值：`$v0`
* 内建 `putint/putch/putstr`：直接使用 `$a0` 传参

### 9.4 分支与比较优化

* 分支条件直接来自 compare 时：生成直接 branch，避免多余布尔 SSA
* compare 仅被分支使用时：可跳过 compare 的独立发射（减少中间指令）

### 9.5 Copy Coalescing 与块内简化

* 对 basic block 内的 Unary POS 拷贝链进行 coalescing
* 活跃区间不冲突时合并 SSA 值到同一物理寄存器，减少 `move`

### 9.6 算术强度削弱（2 的幂）

* 检测 `x * 2^n` 并生成 `sll` 替代 `mul`，降低指令成本

---

## 10. 测试、验证与维护建议

### 10.1 回归测试

* 使用 `test/` 目录输入样例进行回归验证
* 建议对比变更前后：

    * `llvm_ir.txt`：IR 规模、CFG 结构、指令数量变化
    * `mips.txt`：汇编指令序列与运行行为一致性

### 10.2 常见排查路径（工程实践）

* Lexer：关键字误判、注释未闭合导致阻塞、行号错乱（多次回退）
* Parser：歧义分支回退逻辑、缺符号错误恢复的同步点选择
* Semantic：作用域 push/pop 是否对齐语法块、循环栈维护、printf `%d` 计数
* IR/MIPS：新增指令时需同步更新 IR 定义与 Printer（Asm/Mips）

---

## 11. 可扩展性与后续演进方向

在当前“轻量优化 + 直接 IR→MIPS”架构下，系统具备良好的可扩展空间，典型演进包括：

* 更强的全局优化：GVN / 全局 CSE、跨块数据流分析
* 循环优化：不变式外提、强度削弱（文档中已列为模式级 loop 优化方向）
* 内联与常量实参传播：提升构建期折叠命中率
* 若需进一步逼近工业后端：引入 Machine IR 层、寄存器分配与指令调度模块化

---

## 附录 A：SysY 文法摘录（用于实现对照）

* `CompUnit → {Decl} {FuncDef} MainFuncDef`
* `Decl → ConstDecl | VarDecl`
* `ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'`
* `VarDecl → [ 'static' ] BType VarDef { ',' VarDef } ';'`
* `LVal → Ident ['[' Exp ']']`
* `Exp → AddExp`，`AddExp/MulExp/UnaryExp/PrimaryExp` 递归定义
* FIRST 集合（示例）：

    * `FIRST(Decl) = {'const', 'static', 'int'}`
    * `FIRST(FuncDef) = {'void', 'int'}`
    * `FIRST(Stmt) = {';', 'if', 'while', 'break', 'continue', 'return', '{', Ident, Number, '(', '+', '-', '!'}`

---

## 附录 B：Value 体系结构（概念对齐）

Value 体系按 `Value → User → Instruction` 的层次建模，并区分 `Constant` 与 `GlobalValue(Function/GlobalVariable)` 等分支，确保 IR 在表达“常量、全局符号、指令结果”时具备统一抽象与类型安全边界。
