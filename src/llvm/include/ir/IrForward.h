#pragma once

#include <list>
#include <memory>

class Value;
class Type;
class Constant;
class ConstantData;
class ConstantInt;
class ConstantArray;
class GlobalValue;
class GlobalVariable;
class Function;
class Module;
class BasicBlock;
class Instruction;
class AllocaInst;
class StoreInst;
class LoadInst;
class CallInst;
class BinaryInst;
class ReturnInst;
class BinaryOperator;
class CompareOperator;
class LogicalOperator;
class UnaryOperator;
class Argument;
class Use;

using ValuePtr = std::shared_ptr<Value>;
using TypePtr = std::shared_ptr<Type>;
using ArgumentPtr = std::shared_ptr<Argument>;
using ConstantPtr = std::shared_ptr<Constant>;
using ConstantDataPtr = std::shared_ptr<ConstantData>;
using ConstantIntPtr = std::shared_ptr<ConstantInt>;
using ConstantArrayPtr = std::shared_ptr<Constant>;
using GlobalValuePtr = std::shared_ptr<GlobalValue>;
using GlobalVariablePtr = std::shared_ptr<GlobalVariable>;
using FunctionPtr = std::shared_ptr<Function>;
using BasicBlockPtr = std::shared_ptr<BasicBlock>;
using InstructionPtr = std::shared_ptr<Instruction>;
using AllocaInstPtr = std::shared_ptr<AllocaInst>;
using StoreInstPtr = std::shared_ptr<StoreInst>;
using LoadInstPtr = std::shared_ptr<LoadInst>;
using CallInstPtr = std::shared_ptr<CallInst>;
using BinaryInstPtr = std::shared_ptr<BinaryInst>;
using ReturnInstPtr = std::shared_ptr<ReturnInst>;
using BinaryOperatorPtr = std::shared_ptr<BinaryOperator>;
using CompareOperatorPtr = std::shared_ptr<CompareOperator>;
using LogicalOperatorPtr = std::shared_ptr<LogicalOperator>;
using UnaryOperatorPtr = std::shared_ptr<UnaryOperator>;
using UsePtr = std::shared_ptr<Use>;
using UseList = std::list<UsePtr>;
using use_iterator = UseList::iterator;
