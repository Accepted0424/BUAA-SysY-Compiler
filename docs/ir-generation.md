# 中间代码生成说明

本文档描述本项目的 IR（中间代码）生成流程、主要数据结构、以及与语义检查/优化的协作点。IR 生成主要在 AST Visitor 阶段完成（`src/frontend/visitor.cpp`），目标为内部 LLVM-like IR 并最终输出到 `llvm_ir.txt`。

## IR 架构与核心类型
实现位置：`src/llvm/include/ir/`

- `Module`：编译单元容器，持有全局变量与函数。
- `Function`：函数定义，包含基本块列表与参数。
- `BasicBlock`：基本块，包含指令序列。
- `Value` / `Instruction`：IR 的基础节点与指令基类。
- 主要指令类型：
  - 内存：`AllocaInst` / `LoadInst` / `StoreInst` / `GetElementPtrInst`
  - 运算：`BinaryOperator` / `UnaryOperator` / `CompareOperator` / `LogicalOperator`
  - 控制流：`BranchInst` / `JumpInst` / `ReturnInst`
  - 调用：`CallInst`

## IR 生成入口与流程
- 入口：`Visitor::visit(const CompUnit &compUnit)`。
- 步骤：
  1) 注入内建函数符号（`getint/putint/putch/putstr`）。
  2) 处理全局声明（`visitDecl`）。
  3) 处理函数定义（`visitFuncDef`）。
  4) 处理主函数（`visitMainFuncDef`）。

## 作用域与符号到 IR 的映射
- 作用域在 `visitBlock` 与 `visitStmt` 中用 `pushScope/popScope` 管理。
- 标识符解析后得到 `Symbol`，其 `value` 指向 IR Value（如 `AllocaInst`、`GlobalVariable` 等）。
- 常量：局部常量尽量保留为立即数 `ConstantInt`；全局常量使用 `GlobalVariable`。

## 变量与数组的 IR 生成
- 全局变量：
  - 标量：`GlobalVariable`，可带常量初始化。
  - 数组：`ConstantArray` + `GlobalVariable`。
- 局部变量：
  - 标量：`AllocaInst` + 可选 `StoreInst` 初始化。
  - 数组：`AllocaInst` + 逐元素 `GEP + Store` 初始化。
- 数组访问：
  - `getLValAddress` 通过 `GetElementPtrInst` 计算元素地址。

## 表达式与指令生成
位置：`visitUnaryExp` / `visitMulExp` / `visitAddExp` / `visitRelExp` / `visitEqExp`

- 表达式先通过 `visit*` 构建 IR Value。
- `loadIfPointer`：
  - 对 `Alloca/Global/GEP` 等指针类值插入 `LoadInst`。
  - 对数组整体值（非 GEP）保持指针语义。
- 比较与布尔：
  - `createCmp` 生成 `CompareOperator`。
  - `toBool` 将任意值转为布尔语义（比较 `!= 0`）。
- 逻辑与/或：
  - `visitLAndExp` / `visitLOrExp` 通过控制流短路生成。

## 控制流 IR 生成
- `if`：生成 then/else/end 三块，条件使用 `BranchInst`。
- `for`：生成 cond/body/step/end 四块；维护 `breakTargets_`/`continueTargets_`。
- `return`：生成 `ReturnInst`，返回值在需要时 `loadIfPointer`。

## 函数 IR 生成
位置：`visitFuncDef` / `visitMainFuncDef`

- 创建 `Function` 与入口块 `entry_block_`。
- 参数处理：
  - 标量参数：`AllocaInst` + `Store` 入栈，后续作为局部变量使用。
  - 数组参数：直接以指针样式传递，符号表记录为数组类型。
- 函数结束：
  - `void` 函数若未显式返回，插入 `ReturnInst`。
  - `int` 函数缺少返回由语义检查处理。

## 与优化的配合
- IR 生成阶段即执行常量折叠、CSE、DCE（见 `docs/compiler-optimizations.md`）。
- `runDCE` 在每个函数构建完成后执行，清理无用指令与临时变量。

## 常见维护建议
- 新增语法节点时，先补充 AST，再在 Visitor 中补齐 IR 生成路径。
- 新增指令类型时，需同步更新：
  - IR 定义（`src/llvm/include/ir/value/inst/`）
  - 打印/生成逻辑（`src/llvm/AsmPrinter.cpp`/`MipsPrinter.cpp`）
- 若 IR 生成异常，可先检查：
  - 是否遗漏 `loadIfPointer`
  - 是否在 `entry_block_` 中插入 `alloca`
  - 是否正确维护 `cur_block_` 与控制流终止指令
