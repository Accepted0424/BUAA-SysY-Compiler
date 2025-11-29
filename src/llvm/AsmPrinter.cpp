#include "include/asm/AsmPrinter.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ir/llvmContext.h"
#include "ir/type.h"
#include "ir/value/Argument.h"
#include "ir/value/BasicBlock.h"
#include "ir/value/ConstantArray.h"
#include "ir/value/ConstantInt.h"
#include "ir/value/Function.h"
#include "ir/value/GlobalVariable.h"
#include "ir/value/inst/BinaryInstruction.h"
#include "ir/value/inst/Instruction.h"
#include "ir/value/inst/UnaryInstruction.h"

namespace {
std::string typeToString(const TypePtr &type) {
    if (!type) {
        return "void";
    }
    if (type->is(Type::VoidTyID)) {
        return "void";
    }
    if (type->is(Type::IntegerTyID)) {
        return "i32";
    }
    if (type->is(Type::ArrayTyID)) {
        auto arr = std::static_pointer_cast<ArrayType>(type);
        auto elemStr = typeToString(arr->getElementType());
        const auto elemNum = arr->getElementNum();
        if (elemNum < 0) {
            return elemStr + "*";
        }
        return "[" + std::to_string(elemNum) + " x " + elemStr + "]";
    }
    return "void";
}

std::string binOpToString(const BinaryOpType type) {
    switch (type) {
        case BinaryOpType::ADD: return "add";
        case BinaryOpType::SUB: return "sub";
        case BinaryOpType::MUL: return "mul";
        case BinaryOpType::DIV: return "sdiv";
        case BinaryOpType::MOD: return "srem";
    }
    return "add";
}

std::string cmpOpToString(const CompareOpType type) {
    switch (type) {
        case CompareOpType::EQL: return "eq";
        case CompareOpType::NEQ: return "ne";
        case CompareOpType::LSS: return "slt";
        case CompareOpType::GRE: return "sgt";
        case CompareOpType::LEQ: return "sle";
        case CompareOpType::GEQ: return "sge";
    }
    return "eq";
}

std::string constArrayToString(const ConstantArrayPtr &constArr) {
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto &elem : constArr->getElements()) {
        if (!first) oss << ", ";
        first = false;
        oss << typeToString(elem->getType()) << " " << elem->getValue();
    }
    oss << "]";
    return oss.str();
}

}  // namespace

class AsmPrinterImpl {
public:
    explicit AsmPrinterImpl(Module &module, std::ostream &out)
        : module_(module), out_(out) {}

    void printHeader() const {
        out_ << "declare i32 @getint()\n"
             << "declare void @putint(i32)\n"
             << "declare void @putch(i32)\n"
             << "declare void @putstr(i8*)\n\n";
    }

    void printModule() {
        printHeader();
        for (auto it = module_.globalVarBegin(); it != module_.globalVarEnd(); ++it) {
            printGlobal(*it);
        }
        for (auto it = module_.functionBegin(); it != module_.functionEnd(); ++it) {
            printFunction(*it);
        }
        auto mainFunc = module_.getMainFunction();
        if (mainFunc) {
            const bool printed = printedFuncs_.count(mainFunc.get()) > 0;
            if (!printed) {
                printFunction(mainFunc);
            }
        }
    }

private:
    Module &module_;
    std::ostream &out_;
    std::unordered_map<const Value*, std::string> names_;
    std::unordered_set<const Function*> printedFuncs_;
    int tempId_ = 0;
    int blockId_ = 0;

    std::string nextTemp() {
        return "%t" + std::to_string(tempId_++);
    }

    std::string nextBlockName() {
        return "L" + std::to_string(blockId_++);
    }

    std::string valueName(const ValuePtr &v) {
        if (!v) {
            return "undef";
        }
        const auto *ptr = v.get();
        if (names_.count(ptr)) {
            return names_[ptr];
        }

        switch (v->getValueType()) {
            case ValueType::ConstantIntTy: {
                auto ci = std::static_pointer_cast<ConstantInt>(v);
                return std::to_string(ci->getValue());
            }
            case ValueType::FunctionTy:
            case ValueType::GlobalVariableTy:
                return "@" + v->getName();
            case ValueType::BasicBlockTy: {
                auto hint = v->getName();
                auto name = hint.empty() ? nextBlockName() : hint;
                names_[ptr] = name;
                return name;
            }
            default:
                break;
        }

        auto name = v->getName().empty() ? nextTemp() : "%" + v->getName();
        names_[ptr] = name;
        return name;
    }

    void printGlobal(const GlobalValuePtr &gv) {
        auto typeStr = typeToString(gv->getType());
        if (gv->getValueType() == ValueType::GlobalVariableTy) {
            auto globalVar = std::static_pointer_cast<GlobalVariable>(gv);
            std::string init = "0";
            if (globalVar->value_) {
                if (globalVar->value_->getValueType() == ValueType::ConstantArrayTy) {
                    init = constArrayToString(std::static_pointer_cast<ConstantArray>(globalVar->value_));
                } else {
                    init = valueName(globalVar->value_);
                }
            }
            out_ << "@" << gv->getName() << " = "
                 << (globalVar->isConst_ ? "constant " : "global ")
                 << typeStr << " " << init << "\n";
        } else {
            out_ << "@" << gv->getName() << " = global " << typeStr << " 0\n";
        }
    }

    void printFunction(const FunctionPtr &func) {
        printedFuncs_.insert(func.get());
        out_ << "\n";
        auto retType = typeToString(func->getReturnType());
        out_ << "define " << retType << " @" << func->getName() << "(";
        bool first = true;
        for (const auto &arg : func->getArgs()) {
            if (!first) out_ << ", ";
            first = false;
            out_ << typeToString(arg->getType()) << " " << valueName(arg);
        }
        out_ << ") {\n";

        for (auto bbIt = func->basicBlockBegin(); bbIt != func->basicBlockEnd(); ++bbIt) {
            auto bb = *bbIt;
            out_ << valueName(bb) << ":\n";
            for (auto instIt = bb->InstructionBegin(); instIt != bb->InstructionEnd(); ++instIt) {
                printInstruction(*instIt);
            }
        }
        out_ << "}\n";
    }

    void printInstruction(const InstructionPtr &inst) {
        switch (inst->getValueType()) {
            case ValueType::AllocaInstTy:
                out_ << "  " << valueName(inst) << " = alloca "
                     << typeToString(inst->getType()) << "\n";
                break;
            case ValueType::StoreInstTy: {
                auto storeInst = std::static_pointer_cast<StoreInst>(inst);
                out_ << "  store " << typeToString(storeInst->getValueOperand()->getType())
                     << " " << valueName(storeInst->getValueOperand()) << ", "
                     << typeToString(storeInst->getValueOperand()->getType()) << "* "
                     << valueName(storeInst->getAddressOperand()) << "\n";
                break;
            }
            case ValueType::LoadInstTy: {
                auto loadInst = std::static_pointer_cast<LoadInst>(inst);
                out_ << "  " << valueName(inst) << " = load "
                     << typeToString(loadInst->getType()) << ", "
                     << typeToString(loadInst->getType()) << "* "
                     << valueName(loadInst->getAddressOperand()) << "\n";
                break;
            }
            case ValueType::BinaryOperatorTy: {
                auto bin = std::static_pointer_cast<BinaryOperator>(inst);
                out_ << "  " << valueName(inst) << " = "
                     << binOpToString(bin->OpType()) << " "
                     << typeToString(bin->getType()) << " "
                     << valueName(bin->lhs_) << ", " << valueName(bin->rhs_) << "\n";
                break;
            }
            case ValueType::CompareInstTy: {
                auto cmp = std::static_pointer_cast<CompareOperator>(inst);
                out_ << "  " << valueName(inst) << " = icmp " << cmpOpToString(cmp->OpType())
                     << " " << typeToString(cmp->getType()) << " "
                     << valueName(cmp->getLhs()) << ", " << valueName(cmp->getRhs()) << "\n";
                break;
            }
            case ValueType::LogicalInstTy: {
                auto logi = std::static_pointer_cast<LogicalOperator>(inst);
                const std::string opStr = logi->OpType() == LogicalOpType::AND ? "and" : "or";
                out_ << "  " << valueName(inst) << " = " << opStr << " "
                     << typeToString(logi->getType()) << " "
                     << valueName(logi->getLhs()) << ", " << valueName(logi->getRhs()) << "\n";
                break;
            }
            case ValueType::UnaryOperatorTy: {
                auto un = std::static_pointer_cast<UnaryOperator>(inst);
                switch (un->OpType()) {
                    case UnaryOpType::NEG:
                        out_ << "  " << valueName(inst) << " = sub " << typeToString(un->getType())
                             << " 0, " << valueName(un->getOperand()) << "\n";
                        break;
                    case UnaryOpType::POS:
                        out_ << "  " << valueName(inst) << " = add " << typeToString(un->getType())
                             << " 0, " << valueName(un->getOperand()) << "\n";
                        break;
                    case UnaryOpType::NOT:
                        out_ << "  " << valueName(inst) << " = icmp eq "
                             << typeToString(un->getType()) << " "
                             << valueName(un->getOperand()) << ", 0\n";
                        break;
                }
                break;
            }
            case ValueType::CallInstTy: {
                auto call = std::static_pointer_cast<CallInst>(inst);
                auto func = call->getFunction();
                out_ << "  ";
                const bool hasRet = !func->getReturnType()->is(Type::VoidTyID);
                if (hasRet) {
                    out_ << valueName(inst) << " = ";
                }
                out_ << "call " << typeToString(func->getReturnType()) << " @" << func->getName() << "(";
                bool first = true;
                for (const auto &arg : call->getArgs()) {
                    if (!first) out_ << ", ";
                    first = false;
                    out_ << typeToString(arg->getType()) << " " << valueName(arg);
                }
                out_ << ")\n";
                break;
            }
            case ValueType::GetElementPtrInstTy: {
                auto gep = std::static_pointer_cast<GetElementPtrInst>(inst);
                auto baseType = gep->getAddressOperand()->getType();
                if (baseType && baseType->is(Type::ArrayTyID)) {
                    auto arrType = std::static_pointer_cast<ArrayType>(baseType);
                    if (arrType->getElementNum() < 0) {
                        baseType = arrType->getElementType();
                    }
                }
                out_ << "  " << valueName(inst) << " = getelementptr "
                     << typeToString(baseType) << ", "
                     << typeToString(baseType) << "* " << valueName(gep->getAddressOperand());
                for (const auto &idx : gep->getIndices()) {
                    out_ << ", i32 " << valueName(idx);
                }
                out_ << "\n";
                break;
            }
            case ValueType::ReturnInstTy: {
                auto ret = std::static_pointer_cast<ReturnInst>(inst);
                if (ret->getReturnValue()) {
                    out_ << "  ret " << typeToString(ret->getReturnValue()->getType()) << " "
                         << valueName(ret->getReturnValue()) << "\n";
                } else {
                    out_ << "  ret void\n";
                }
                break;
            }
            case ValueType::JumpInstTy: {
                auto jmp = std::static_pointer_cast<JumpInst>(inst);
                out_ << "  br label %" << valueName(jmp->getTarget()) << "\n";
                break;
            }
            case ValueType::BranchInstTy: {
                auto br = std::static_pointer_cast<BranchInst>(inst);
                out_ << "  br i1 " << valueName(br->getCondition()) << ", label %"
                     << valueName(br->getTrueBlock()) << ", label %" << valueName(br->getFalseBlock()) << "\n";
                break;
            }
            default:
                out_ << "  ; unsupported inst\n";
        }
    }
};

void AsmPrinter::print() const {
    AsmPrinterImpl impl(module_, out_);
    impl.printModule();
}

void AsmPrinter::printHeader() const {
    AsmPrinterImpl impl(module_, out_);
    impl.printHeader();
}

void AsmPrinter::printModule() const {
    AsmPrinterImpl impl(module_, out_);
    impl.printModule();
}
