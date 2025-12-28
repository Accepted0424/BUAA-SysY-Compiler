# 编译器优化说明

本文档说明当前编译器已实现的优化点、触发位置与适用范围，便于维护与扩展。

## IR 生成阶段优化（frontend visitor）
位置：`src/frontend/visitor.cpp`

### 1) 常量折叠与代数化简
- 触发位置：`visitAddExp` / `visitMulExp` / `visitUnaryExp` / `createCmp` / `toBool`。
- 行为：
  - 纯常量表达式在生成 IR 时直接计算为常量，减少运行期指令数量。
  - 代数恒等式化简：`x + 0`、`x - 0`、`x * 1`、`x / 1` 直接返回 `x`；`x * 0` 直接返回 `0`；`x % 1` 直接返回 `0`。
  - 比较运算两侧均为常量时直接产出常量布尔值，避免生成 `cmp` 指令。
  - 一元操作 `+x` 直接返回 `x`；`-x`、`!x` 在 `x` 为常量时直接计算。
- 对 `testfile.txt` 的典型效果：
  - 嵌套常量表达式（如 `89/2*36-53`、`45*56/85-56+35*56/4-9`）会在 IR 生成期折叠为单个常量。
  - `+-+6` 直接折叠为 `-6`。

### 2) 公共子表达式消除（CSE）
- 触发位置：`reuseBinary` / `reuseCompare` / `reuseUnary` / `reuseZExt` / `reuseGEP`。
- 行为：
  - 在同一基本块内对等价指令进行复用，避免重复计算。
  - 可交换运算（加法/乘法、相等/不等比较）会对操作数排序以提高命中率。
  - GEP、zext、unary、compare 也纳入复用范围。
- 典型收益：
  - 频繁出现的子表达式只保留一份定义，降低指令数量并减少寄存器压力。

### 3) 死代码消除（DCE）
- 触发位置：`runDCE`。
- 行为：
  - 删除从未被 `load` 的局部 `alloca` 及其相关 `store`。
  - 对无副作用且无使用的 IR 指令进行递归移除。
- 典型收益：
  - 清理临时变量与无效存储，减少后端生成的 load/store。

## 代码生成阶段优化（MIPS）
位置：`src/llvm/MipsPrinter.cpp`

### 1) 临时寄存器池
- 通过 `TempRegPool` 维护 `$t0`-`$t9` 的可用集合。
- 优先在寄存器中完成中间计算，减少栈上溢出与回写。

### 2) 基础寄存器规划
- 通过 `RegisterPlan` 记录 `Value -> 寄存器` 的映射。
- 对热点值进行寄存器保存，减少重复加载。
- 对被调用者保存寄存器进行统一管理，降低保存/恢复开销。

## 扩展优化建议（可选）
- 循环优化：为 `for`/`while` 结构加入循环不变式外提与强度削弱。
- 函数内联或常量实参求值：对小函数或纯函数进行内联/解释求值，可大幅提升常量表达式折叠效果。
- 全局 CSE 或 GVN：在基本块之外进一步合并冗余计算，但需要更严谨的数据流分析。

## 使用与验证建议
- 变更前后对比 `llvm_ir.txt`、`mips.txt`，确认指令数量与结构变化。
- 用 `test/` 中的输入样例进行回归，确保语义不变。

## codex-20251228-优化总结
• 已在 IR 构建与 MIPS lowering 两端做了结构性优化：前端加入常量传播与条件分支直出，后端加入 ABI 传参、栈槽复用、分支比较直出和块内值缓存，整体汇编会更接近 -O2 风格且减少 lw/sw 与
CFG 膨胀。

改动要点与位置

- 常量折叠/传播与局部 CSE：src/frontend/visitor.cpp 对 LVal 常量直接返回 ConstantInt；新增块内 load 缓存并在 store/call 处失效，减少重复 Load（也让 i*i 之类局部表达式更易共用）。
  新增结构 loadCaches_ 在 src/frontend/include/visitor.h。
- 条件控制流简化：src/frontend/visitor.cpp 的 if/for 走新 emitCondBranch（短路分支，不再生成 bool SSA/临时 alloca），减少 CFG 和多余 load/store。
- 栈槽压缩与复用：src/llvm/MipsPrinter.cpp 在 buildFrameInfo 做线性扫描 last-use，复用 SSA 临时值栈槽；alloca 仍独立保留，区分地址取用变量与临时值。
- 冗余 load/store 消除：src/llvm/MipsPrinter.cpp 增加 BlockRegCache（块内 value→reg 缓存），loadValue/storeValue 复用缓存并减少重复 lw。
- 公共子表达式/分支直出：src/llvm/MipsPrinter.cpp 新增 computeSkipInsts 与 emitBranchCompare，当分支条件直接来自 compare 时，直接生成 branch，比起先生成 bool SSA 再分支更紧凑。
- MIPS ABI：src/llvm/MipsPrinter.cpp 调用约定改为 $a0-$a3 传前四个参数，剩余参数压栈；返回值使用 $v0；内建函数 putint/putch/putstr 直接使用 $a0。

新增/调整的数据结构与 Pass

- Visitor::loadCaches_：块内 load CSE/常量传播辅助缓存（src/frontend/include/visitor.h）。
- BlockRegCache：块内 value→register 映射，避免重复 lw（src/llvm/MipsPrinter.cpp）。
- computeSkipInsts + emitBranchCompare：分支 compare 直出，跳过只用于分支的 compare 指令（src/llvm/MipsPrinter.cpp）。
- buildFrameInfo 线性扫描复用栈槽（src/llvm/MipsPrinter.cpp）。