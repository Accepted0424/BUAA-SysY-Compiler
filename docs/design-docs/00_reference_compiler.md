# 参考编译器架构与设计（以 LLVM 为代表）

## 一、总体架构设计

LLVM 采用高度模块化、分层清晰的编译器架构，其整体编译流程可概括为：

**前端（Front-end） → 统一中间表示（LLVM IR） → 中间层优化（Pass Pipeline） → 后端（目标架构代码生成）**

其中，Clang 作为典型前端，负责将源语言（如 C/C++）解析并转换为 LLVM IR；后续所有分析与优化均围绕 IR 展开，最终由目标相关后端将 IR 映射为具体硬件架构的机器代码。

LLVM 的核心设计思想在于：

* **统一中间表示（LLVM IR）**
  LLVM IR 作为前后端之间的稳定接口，使编译器能够实现“多前端 × 多后端”的自由组合，大幅提升系统的可扩展性与可维护性。
* **明确的层次划分与职责分离**
  前端仅负责语义分析与 IR 构建；中间层专注于与目标无关的优化；后端处理与具体架构强相关的代码生成问题。

在实现层面，LLVM IR 采用面向对象的层次化建模，其关键实体包括：

* `LLVMContext`：全局上下文，负责管理类型、常量等全局唯一对象；
* `Module`：编译单元级别的容器，对应一个目标文件或程序模块；
* `Function`：函数级 IR 表示；
* `BasicBlock`：基本块，构成控制流图（CFG）的节点；
* `Instruction`：最小计算单元，所有操作均以 SSA 形式表达。

---

## 二、接口与框架设计

### 1. IR 构建接口

LLVM 提供了一套高度抽象且类型安全的 IR 构建接口，用于屏蔽底层指令细节：

* **`IRBuilder`**
  作为 IR 构建的核心工具类，`IRBuilder` 封装了指令创建逻辑，提供如 `CreateAdd`、`CreateSub`、`CreateBr` 等高层 API，使前端在生成 IR 时无需直接操作底层 `Instruction` 对象，从而显著降低构建复杂度。
* **类型系统与值体系分离**
  LLVM 将 `Type` 与 `Value` 明确解耦：

    * `Type` 描述静态类型信息；
    * `Value` 作为所有可计算实体的统一抽象，既可以是指令结果，也可以是常量或函数参数。
      该设计使 SSA 形式下的数据依赖关系表达更加自然和统一。

### 2. Pass 与优化框架

LLVM 的中间优化层基于通用的 Pass 框架实现：

* **PassManager**
  负责对不同粒度的 Pass 进行统一调度，支持：

    * Module-level Pass
    * Function-level Pass
    * Loop-level Pass
* **分析与变换分离**
  Pass 被划分为分析型（Analysis Pass）与变换型（Transformation Pass），前者提供程序属性信息，后者基于分析结果对 IR 进行重写。
* **可组合的优化流水线（Pipeline）**
  多个 Pass 可以按顺序组合形成优化流水线，例如：

    * 常量折叠（Constant Folding）
    * 死代码消除（DCE）
    * 控制流图简化（CFG Simplify）
    * 稀疏条件常量传播（SCCP）
      PassManager 支持多轮迭代执行，直到优化结果收敛。

### 3. 后端接口设计

LLVM 后端采用分阶段的代码生成策略：

* **指令选择（Instruction Selection）**
  通过 SelectionDAG 或 GlobalISel，将目标无关的 IR 映射为目标相关的指令表示。
* **目标相关优化**
  包括寄存器分配、指令调度、指令重排等，充分考虑具体硬件架构特性。
* **汇编与目标代码输出**
  最终生成汇编代码或二进制目标文件，供后续链接使用。

---

## 三、源码组织结构（高层视角）

从源码结构上看，LLVM 按功能模块进行严格划分：

* `llvm/include/llvm/IR/`
  IR 核心数据结构与接口定义（如 `Module`、`Function`、`Instruction` 等）。
* `llvm/include/llvm/IR/IRBuilder.h`
  IR 构建器接口定义。
* `llvm/lib/IR/`
  IR 相关核心功能的具体实现。
* `llvm/include/llvm/Passes/` 与 `llvm/lib/Transforms/`
  Pass 框架及各类优化 Pass 的实现。
* `llvm/include/llvm/CodeGen/` 与 `llvm/lib/CodeGen/`
  代码生成通用框架。
* `llvm/Target/`
  各目标体系结构的后端实现（如 X86、ARM 等）。

该组织方式保证了不同子系统之间的边界清晰，便于独立演进与维护。

---

## 四、对本项目的可借鉴经验

LLVM 的设计为本项目提供了多方面的参考价值：

1. **以 IR 作为编译各阶段之间的稳定中间接口**
   通过统一的中间表示解耦前端语义分析与后端代码生成，有助于提升系统整体扩展性。
2. **统一的 PassManager 优化调度机制**
   将优化逻辑模块化为独立 Pass，支持灵活组合与多轮迭代，有利于逐步引入更复杂的优化策略。
3. **IRBuilder 风格的指令构建接口**
   在前端 Visitor 或语法树遍历过程中引入类似 IRBuilder 的抽象层，可显著简化 IR 生成代码，降低实现复杂度并提高可读性。
