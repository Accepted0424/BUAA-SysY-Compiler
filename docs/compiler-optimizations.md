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
- 通过 `TempRegPool` 维护 `$t0`-`$t6` 的可用集合。
- `$t7` 用作 for 循环 induction 常驻寄存器，`$t8/$t9` 用作块内值缓存。
- 优先在寄存器中完成中间计算，减少栈上溢出与回写。

### 2) 基础寄存器规划
- 通过 `RegisterPlan` 记录 `Value -> 寄存器` 的映射。
- 对热点值进行寄存器保存，减少重复加载。
- 对被调用者保存寄存器进行统一管理，降低保存/恢复开销。

### 3) 栈槽压缩与复用
- 在 `buildFrameInfo` 中进行线性扫描，复用 SSA 临时值的栈槽。
- 只为需要落栈的值分配槽位，减小栈帧体积。

### 4) ABI 参数传递与返回值
- 前四个整型参数使用 `$a0-$a3`，其余参数按约定压栈。
- 返回值使用 `$v0`。
- 内建 `putint/putch/putstr` 直接使用 `$a0`。

### 5) 分支与比较优化
- 分支条件直接来自 compare 时，生成直接 branch 指令，避免多余的布尔 SSA。
- compare 仅被分支使用时跳过 compare 指令的独立发射。

### 6) 块内 load/store 消除
- 维护 value->register 的块内缓存，避免重复 `lw`。
- 产生值后优先回填缓存寄存器，减少回读。

### 7) Copy Coalescing
- 对 basic block 内的 Unary POS 拷贝链进行 coalescing。
- 当活跃区间不冲突时将两个 SSA value 分配到同一物理寄存器，避免生成 `move`。

### 8) 模式级 loop 优化
- 连续 `a[k] += t` 序列识别并合并为紧凑循环。
- for-loop induction 变量常驻寄存器，减少循环内 lw/sw。

### 9) Frame Pointer Omission (FPO)
- 当参数只读、无 address-taken 局部变量、无变长栈对象时省略 `$fp`。
- 仅在存在 call 时保存 `$ra`。

## IR PassManager 优化
位置：`src/llvm/PassManager.cpp`, `src/llvm/include/ir/pass/PassManager.h`

### 1) 常量折叠 / 传播
- 对算术、比较、逻辑与 zext 的常量操作进行折叠与简化。

### 2) DCE
- 移除无使用的 SSA 指令。
- 清理死临时与无效 store/alloca。

### 3) CFG 简化
- 常量分支直接替换为 jump。
- 删除不可达块与空跳转块。

## 扩展优化建议（可选）
- 循环优化：为 `for`/`while` 结构加入循环不变式外提与强度削弱。
- 函数内联或常量实参求值：对小函数或纯函数进行内联/解释求值，可大幅提升常量表达式折叠效果。
- 全局 CSE 或 GVN：在基本块之外进一步合并冗余计算，但需要更严谨的数据流分析。

## 使用与验证建议
- 变更前后对比 `llvm_ir.txt`、`mips.txt`，确认指令数量与结构变化。
- 用 `test/` 中的输入样例进行回归，确保语义不变。

## 已提交优化（近期开启）
### frontend / IR 构建
- 块内 load 缓存与失效：在 `store/call` 处失效缓存，减少重复 load。
- 条件分支构建优化：if/for 条件走短路分支生成路径，减少 CFG 膨胀与多余 load/store。

### IR 基础设施
- 补齐 def-use 替换接口：增加 `replaceOperandValue` 与 `replaceAllUsesWith`，便于后续 IR 级优化 pass。

### MIPS CodeGen
- 模式级 loop 识别：连续 `a[k] += t` 序列合并为紧凑循环。
- for-loop induction 常驻寄存器：在 loop cond/body/step 中保持归纳变量在寄存器中，减少反复 lw/sw。
