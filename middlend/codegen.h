#ifndef COMPILER_CODEGEN_H
#define COMPILER_CODEGEN_H

#include "../parser/ASTNodes.h"
#include "../error/error.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <unordered_map>
#include <iostream>
#include <unordered_set>

class CodeGenerator {
public:
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder;
    std::unique_ptr<llvm::Module> module;
    std::unordered_map<std::string, llvm::AllocaInst*> named_values;
    llvm::Function* printf_func = nullptr;
    llvm::Function* malloc_func = nullptr;
    llvm::Function* free_func = nullptr;
    ErrorReporter error_reporter;
    bool has_errors = false;
    llvm::BasicBlock* currentReturnBB = nullptr;

    // Struct Type & Member Tracking
    std::unordered_map<std::string, llvm::StructType*> struct_types;
    std::unordered_map<std::string, std::unordered_map<std::string, unsigned>> struct_field_indices;
    std::unordered_map<std::string, std::string> var_struct_types;

    CodeGenerator(ErrorReporter error_reporter) : builder(context), error_reporter(error_reporter) {
        module = std::make_unique<llvm::Module>("my_compiler_module", context);
        setupRuntime();
    }

    llvm::AllocaInst* lookup(const std::string& name) {
        auto it = named_values.find(name);
        if (it != named_values.end()) {
            return it->second;
        }
        return nullptr;
    }

    void insert(const std::string& name, llvm::AllocaInst* alloca) {
        named_values[name] = alloca;
    }

    void dumpIR() {
        module->print(llvm::errs(), nullptr);
    }

    void registerHeaderInclude(const std::string& header) {
        if (unique_headers.find(header) == unique_headers.end()) {
            unique_headers.insert(header);
            registered_headers.push_back(header);
        }
    }

    const std::vector<std::string>& getRegisteredHeaders() const {
        return registered_headers;
    }

private:
    std::vector<std::string> registered_headers;
    std::unordered_set<std::string> unique_headers;

    void setupRuntime() {
        // printf runtime
        llvm::FunctionType* printfType = llvm::FunctionType::get(
            builder.getInt32Ty(),
            llvm::PointerType::getUnqual(context),
            true
        );
        printf_func = llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", module.get());

        // malloc runtime
        llvm::FunctionType* mallocType = llvm::FunctionType::get(
            builder.getPtrTy(),
            {builder.getInt64Ty()},
            false
        );
        malloc_func = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());

        // free runtime
        llvm::FunctionType* freeType = llvm::FunctionType::get(
            builder.getVoidTy(),
            {builder.getPtrTy()},
            false
        );
        free_func = llvm::Function::Create(freeType, llvm::Function::ExternalLinkage, "free", module.get());
    }
};

inline llvm::Type* getLLVMType(CodeGenerator& g, const std::string& typeName, size_t line = 0, size_t column = 0) {
    // 1. Pointer Prefixes: >, %>, $>, *, @
    if (typeName.rfind(">", 0) == 0 || typeName.rfind("%>", 0) == 0 ||
        typeName.rfind("$>", 0) == 0 || typeName.rfind("*", 0) == 0 ||
        typeName.rfind("@", 0) == 0) {
        return g.builder.getPtrTy();
    }

    // 2. Multi-dimensional Array Types (e.g. "int[2][2]")
    size_t lastBracketPos = typeName.rfind('[');
    if (lastBracketPos != std::string::npos) {
        std::string baseTypeStr = typeName.substr(0, lastBracketPos);
        size_t closeBracketPos = typeName.find(']', lastBracketPos);
        if (closeBracketPos == std::string::npos) {
            g.error_reporter.emitError(line, column, "Type Error", "Malformed array type syntax: " + typeName);
            g.has_errors = true;
            return nullptr;
        }

        std::string sizeStr = typeName.substr(lastBracketPos + 1, closeBracketPos - lastBracketPos - 1);
        if (sizeStr.empty()) {
            g.error_reporter.emitError(line, column, "Type Error", "Array type requires explicit size: " + typeName);
            g.has_errors = true;
            return nullptr;
        }

        uint64_t arraySize = std::stoull(sizeStr);
        llvm::Type* elemType = getLLVMType(g, baseTypeStr, line, column);
        if (!elemType) return nullptr;

        return llvm::ArrayType::get(elemType, arraySize);
    }

    // 3. Primitive Types
    if (typeName == "int")     return g.builder.getInt32Ty();
    if (typeName == "long")    return g.builder.getInt64Ty();
    if (typeName == "float")   return g.builder.getFloatTy();
    if (typeName == "double")  return g.builder.getDoubleTy();
    if (typeName == "bool")    return g.builder.getInt1Ty();
    if (typeName == "charray") return g.builder.getPtrTy();
    if (typeName == "char")    return g.builder.getInt8Ty();
    if (typeName == "void")    return g.builder.getVoidTy();

    // 4. Custom Struct Types
    if (g.struct_types.find(typeName) != g.struct_types.end()) {
        return g.struct_types[typeName];
    }

    g.error_reporter.emitError(line, column, "Type Error", "Unknown type name: " + typeName);
    g.has_errors = true;
    return nullptr;
}

inline llvm::Value* castToType(CodeGenerator& g, llvm::Value* val, llvm::Type* targetType, size_t line = 0, size_t column = 0) {
    if (!val || val->getType() == targetType) return val;

    llvm::Type* srcType = val->getType();

    if (srcType->isPointerTy() && targetType->isPointerTy()) {
        return val;
    }

    if (srcType->isPointerTy() && targetType->isArrayTy()) {
        return g.builder.CreateLoad(targetType, val, "loaded_array");
    }

    if (srcType->isIntegerTy() && targetType->isFloatingPointTy()) {
        return g.builder.CreateSIToFP(val, targetType, "cast_int_to_fp");
    }

    if (srcType->isFloatingPointTy() && targetType->isIntegerTy()) {
        return g.builder.CreateFPToSI(val, targetType, "cast_fp_to_int");
    }

    if (srcType->isIntegerTy() && targetType->isIntegerTy()) {
        if (srcType->getIntegerBitWidth() < targetType->getIntegerBitWidth()) {
            return g.builder.CreateSExt(val, targetType, "cast_int_sext");
        } else {
            return g.builder.CreateTrunc(val, targetType, "cast_int_trunc");
        }
    }

    return val;
}

inline std::string getStructTypeName(CodeGenerator& g, ASTNode* node) {
    if (auto* varNode = dynamic_cast<VariableNode*>(node)) {
        auto it = g.var_struct_types.find(varNode->name);
        if (it != g.var_struct_types.end()) {
            return it->second;
        }
    } else if (auto* unaryNode = dynamic_cast<UnaryOpNode*>(node)) {
        if (unaryNode->op == "<") {
            return getStructTypeName(g, unaryNode->operand.get());
        }
    }
    return "";
}

inline llvm::Value* StructDefNode::codegen(CodeGenerator& g) {
    std::vector<llvm::Type*> elementTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    for (unsigned i = 0; i < fields.size(); ++i) {
        llvm::Type* memberTy = getLLVMType(g, fields[i].type, line, column);
        if (!memberTy) return nullptr;

        elementTypes.push_back(memberTy);
        fieldIndices[fields[i].name] = i;
    }

    llvm::StructType* structTy = llvm::StructType::create(g.context, elementTypes, name);

    // ONLY store in struct maps, NEVER in g.named_values
    g.struct_types[name] = structTy;
    g.struct_field_indices[name] = fieldIndices;

    return nullptr;
}

inline llvm::Value* MemberAccessNode::codegenAddress(CodeGenerator& g) {
    llvm::Value* basePtr = base->codegenAddress(g);
    if (!basePtr) return nullptr;

    std::string structTypeName = getStructTypeName(g, base.get());

    if (structTypeName.empty() || g.struct_field_indices[structTypeName].find(field_name) == g.struct_field_indices[structTypeName].end()) {
        g.error_reporter.emitError(line, column, "Member Error", "Invalid struct member access '" + field_name + "'");
        g.has_errors = true;
        return nullptr;
    }

    unsigned fieldIdx = g.struct_field_indices[structTypeName][field_name];
    llvm::StructType* structTy = g.struct_types[structTypeName];

    return g.builder.CreateStructGEP(structTy, basePtr, fieldIdx, field_name + "_ptr");
}

inline llvm::Value* MemberAccessNode::codegen(CodeGenerator& g) {
    llvm::Value* fieldPtr = codegenAddress(g);
    if (!fieldPtr) return nullptr;

    std::string structTypeName = getStructTypeName(g, base.get());
    unsigned fieldIdx = g.struct_field_indices[structTypeName][field_name];
    llvm::StructType* structTy = g.struct_types[structTypeName];
    llvm::Type* fieldTy = structTy->getElementType(fieldIdx);

    return g.builder.CreateLoad(fieldTy, fieldPtr, field_name + "_val");
}

inline llvm::Value* MemberAssignNode::codegen(CodeGenerator& g) {
    llvm::AllocaInst* alloc = g.lookup(struct_var_name);
    if (!alloc) {
        g.error_reporter.emitError(line, column, "Scope Error", "Undefined struct variable '" + struct_var_name + "'");
        g.has_errors = true;
        return nullptr;
    }

    std::string structTypeName = g.var_struct_types[struct_var_name];
    unsigned fieldIdx = g.struct_field_indices[structTypeName][field_name];
    llvm::StructType* structTy = g.struct_types[structTypeName];

    llvm::Value* val = value->codegen(g);
    if (!val) return nullptr;

    llvm::Value* fieldPtr = g.builder.CreateStructGEP(structTy, alloc, fieldIdx, field_name + "_ptr");

    g.builder.CreateStore(val, fieldPtr);
    return val;
}

inline llvm::Value* UnaryOpNode::codegenAddress(CodeGenerator& g) {
    if (op == "@") {
        return operand->codegenAddress(g);
    }
    if (op == "<") {
        return operand->codegen(g);
    }
    return nullptr;
}

inline llvm::Value* UnaryOpNode::codegen(CodeGenerator& g) {
    if (op == "-") {
        llvm::Value* val = operand->codegen(g);
        if (!val) return nullptr;
        if (val->getType()->isFloatingPointTy()) {
            return g.builder.CreateFNeg(val, "negtmp");
        } else if (val->getType()->isIntegerTy()) {
            return g.builder.CreateNeg(val, "negtmp");
        } else {
            g.error_reporter.emitError(line, column, "Type Error", "Cannot apply unary '-' to non-numeric type.");
            g.has_errors = true;
            return nullptr;
        }
    }

    if (op == "@") {
        llvm::Value* addr = operand->codegenAddress(g);
        if (!addr) {
            g.error_reporter.emitError(this->line, this->column, "Code Generation Error", "Cannot obtain address of expression.");
            g.has_errors = true;
            return nullptr;
        }
        return addr;
    }

    if (op == "*") {
        llvm::Value* val = operand->codegen(g);
        if (!val) return nullptr;

        llvm::Type* valType = val->getType();
        uint64_t allocSize = g.module->getDataLayout().getTypeAllocSize(valType);

        llvm::Value* rawMem = g.builder.CreateCall(g.malloc_func, {g.builder.getInt64(allocSize)}, "heap_alloc");
        g.builder.CreateStore(val, rawMem);

        return rawMem;
    }

    if (op == "<") {
        llvm::Value* ptrVal = operand->codegen(g);
        if (!ptrVal || !ptrVal->getType()->isPointerTy()) {
            g.error_reporter.emitError(this->line, this->column, "Type Error", "Cannot dereference a non-pointer expression.");
            g.has_errors = true;
            return nullptr;
        }

        return g.builder.CreateLoad(g.builder.getInt32Ty(), ptrVal, "deref_val");
    }

    g.error_reporter.emitError(this->line, this->column, "Code Generation Error", "Unsupported unary operator: " + op);
    g.has_errors = true;
    return nullptr;
}

inline llvm::Value* FreeNode::codegen(CodeGenerator& g) {
    llvm::Value* ptrVal = expression->codegen(g);
    if (!ptrVal || !ptrVal->getType()->isPointerTy()) {
        g.error_reporter.emitError(this->line, this->column, "Type Error", "'free' statement requires a valid pointer target.");
        g.has_errors = true;
        return nullptr;
    }

    return g.builder.CreateCall(g.free_func, {ptrVal});
}

inline llvm::Value* ProgramNode::codegen(CodeGenerator& g) {
    std::vector<ASTNode*> structDecls;
    std::vector<ASTNode*> funcDecls;
    std::vector<ASTNode*> mainStmts;

    for (auto& stmt : statements) {
        if (dynamic_cast<StructDefNode*>(stmt.get())) {
            structDecls.push_back(stmt.get());
        } else if (dynamic_cast<FunctionNode*>(stmt.get())) {
            funcDecls.push_back(stmt.get());
        } else {
            mainStmts.push_back(stmt.get());
        }
    }

    // 1. Process Struct Definitions First
    for (auto* st : structDecls) {
        st->codegen(g);
    }

    // 2. Process Functions Next
    for (auto* fn : funcDecls) {
        fn->codegen(g);
    }

    // 3. Process Main Body Statements
    llvm::FunctionType* mainType = llvm::FunctionType::get(g.builder.getInt32Ty(), false);
    llvm::Function* mainFunc = llvm::Function::Create(
        mainType, llvm::Function::ExternalLinkage, "main", g.module.get());

    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(g.context, "entry", mainFunc);
    g.builder.SetInsertPoint(entryBlock);

    for (auto* stmt : mainStmts) {
        stmt->codegen(g);
    }

    if (!g.builder.GetInsertBlock()->getTerminator()) {
        g.builder.CreateRet(g.builder.getInt32(0));
    }

    llvm::verifyFunction(*mainFunc);
    return mainFunc;
}

inline llvm::Value* NumberNode::codegen(CodeGenerator& g) {
    if (is_float) {
        return llvm::ConstantFP::get(g.context, llvm::APFloat(std::stod(value)));
    }
    return llvm::ConstantInt::get(g.context, llvm::APInt(32, std::stoi(value), true));
}

inline llvm::Value* CharrayNode::codegen(CodeGenerator& g) {
    return g.builder.CreateGlobalString(value, "str_literal");
}

inline llvm::Value* CharNode::codegen(CodeGenerator& g) {
    return g.builder.getInt8(static_cast<uint8_t>(value));
}

inline llvm::Value* VariableNode::codegen(CodeGenerator& g) {
    llvm::AllocaInst* alloca = g.lookup(name);
    if (!alloca) {
        g.error_reporter.emitError(
            this->line, this->column,
            "Scope Error: Undeclared Variable",
            "Variable '" + name + "' is referenced before declaration or outside its scope.",
            "Declare variable with 'var " + name + ": <type>' within active scope."
        );
        g.has_errors = true;
        return nullptr;
    }
    return g.builder.CreateLoad(alloca->getAllocatedType(), alloca, name.c_str());
}

inline llvm::Value* VariableNode::codegenAddress(CodeGenerator& g) {
    llvm::AllocaInst* alloc = g.lookup(name);
    if (!alloc) {
        g.error_reporter.emitError(line, column, "Scope Error", "Undefined variable '" + name + "'");
        g.has_errors = true;
        return nullptr;
    }
    return alloc;
}

inline llvm::Value* VarDeclNode::codegen(CodeGenerator& g) {

    if (g.lookup(name)) {
        g.error_reporter.emitError(
            this->line, this->column,
            "Scope Error: Variable Redeclaration",
            "Variable '" + name + "' is already declared in scope."
        );
        g.has_errors = true;
        return nullptr;
    }

    llvm::Type* varLLVMType = getLLVMType(g, type, line, column);
    if (!varLLVMType) return nullptr;

    llvm::AllocaInst* alloca = g.builder.CreateAlloca(varLLVMType, nullptr, name);

    // Clean type string by stripping pointer prefixes (@, *, >, %)
    std::string cleanType = type;
    while (!cleanType.empty() && (cleanType[0] == '@' || cleanType[0] == '*' || cleanType[0] == '>' || cleanType[0] == '%')) {
        cleanType = cleanType.substr(1);
    }

    // Register struct type mapping if the base type is a known struct
    if (g.struct_types.count(cleanType)) {
        g.var_struct_types[name] = cleanType;
    }

    // Insert into symbol table
    g.insert(name, alloca);

    // Handle initializers
    if (initializer) {
        llvm::Value* initVal = initializer->codegen(g);
        if (!initVal) return nullptr;

        if (varLLVMType->isArrayTy()) {
            if (initVal->getType()->isPointerTy()) {
                llvm::Value* loadedArr = g.builder.CreateLoad(varLLVMType, initVal, "init_arr_val");
                g.builder.CreateStore(loadedArr, alloca);
            } else {
                g.builder.CreateStore(initVal, alloca);
            }
        } else {
            initVal = castToType(g, initVal, varLLVMType, this->line, this->column);
            if (!initVal) return nullptr;
            g.builder.CreateStore(initVal, alloca);
        }
    }

    return alloca;
}

inline llvm::Value* AssignmentNode::codegen(CodeGenerator& g) {
    llvm::Value* destPtr = target->codegenAddress(g);
    llvm::Value* val = expression->codegen(g);

    if (!destPtr || !val) return nullptr;

    llvm::Type* destElemTy = val->getType();

    if (op != "=") {
        llvm::Value* currentVal = g.builder.CreateLoad(destElemTy, destPtr, "lhs_val");
        val = castToType(g, val, currentVal->getType(), line, column);

        bool isFloat = currentVal->getType()->isFloatingPointTy();

        if (op == "+=" || op == "+") {
            val = isFloat ? g.builder.CreateFAdd(currentVal, val, "addtmp")
                          : g.builder.CreateAdd(currentVal, val, "addtmp");
        } else if (op == "-=" || op == "-") {
            val = isFloat ? g.builder.CreateFSub(currentVal, val, "subtmp")
                          : g.builder.CreateSub(currentVal, val, "subtmp");
        } else if (op == "*=" || op == "*") {
            val = isFloat ? g.builder.CreateFMul(currentVal, val, "multmp")
                          : g.builder.CreateMul(currentVal, val, "multmp");
        } else if (op == "/=" || op == "/") {
            val = isFloat ? g.builder.CreateFDiv(currentVal, val, "divtmp")
                          : g.builder.CreateSDiv(currentVal, val, "divtmp");
        }
    } else {
        val = castToType(g, val, destElemTy, line, column);
    }

    g.builder.CreateStore(val, destPtr);
    return val;
}

inline llvm::Value* ArrayLiteralNode::codegen(CodeGenerator& g) {
    uint64_t size = elements.size();
    if (size == 0) {
        g.error_reporter.emitError(this->line, this->column, "Type Error", "Empty array literal is not supported.");
        g.has_errors = true;
        return nullptr;
    }

    llvm::Value* firstVal = elements[0]->codegen(g);
    if (!firstVal) return nullptr;

    llvm::Type* elemTy = nullptr;
    if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(firstVal)) {
        elemTy = alloca->getAllocatedType();
    } else {
        elemTy = firstVal->getType();
    }

    llvm::ArrayType* arrayTy = llvm::ArrayType::get(elemTy, size);
    llvm::AllocaInst* alloc = g.builder.CreateAlloca(arrayTy, nullptr, "arrayliteral");

    for (uint64_t i = 0; i < size; ++i) {
        llvm::Value* val = (i == 0) ? firstVal : elements[i]->codegen(g);
        if (!val) return nullptr;

        llvm::Value* elemPtr = g.builder.CreateInBoundsGEP(
            arrayTy,
            alloc,
            {g.builder.getInt32(0), g.builder.getInt32(i)},
            "elemptr"
        );

        if (elemTy->isArrayTy() && val->getType()->isPointerTy()) {
            llvm::Value* loadedSubArray = g.builder.CreateLoad(elemTy, val, "sub_array_val");
            g.builder.CreateStore(loadedSubArray, elemPtr);
        } else {
            val = castToType(g, val, elemTy, this->line, this->column);
            if (!val) return nullptr;
            g.builder.CreateStore(val, elemPtr);
        }
    }

    return alloc;
}

inline llvm::Value* IndexNode::codegenAddress(CodeGenerator& g) {
    llvm::Value* arrPtr = array->codegenAddress(g);
    llvm::Value* idxVal = index->codegen(g);

    if (!arrPtr || !idxVal) return nullptr;

    idxVal = castToType(g, idxVal, g.builder.getInt32Ty(), this->line, this->column);
    if (!idxVal) return nullptr;

    llvm::Type* allocType = nullptr;

    if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(arrPtr)) {
        allocType = alloca->getAllocatedType();
    } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(arrPtr)) {
        allocType = gep->getResultElementType();
    } else {
        g.error_reporter.emitError(
            this->line, this->column,
            "Type Error: Invalid Subscript Target",
            "Cannot determine target array memory layout for index operator."
        );
        g.has_errors = true;
        return nullptr;
    }

    if (allocType->isArrayTy()) {
        return g.builder.CreateInBoundsGEP(
            allocType,
            arrPtr,
            {g.builder.getInt32(0), idxVal},
            "elem_ptr"
        );
    } else if (allocType->isPointerTy()) {
        return g.builder.CreateInBoundsGEP(
            allocType,
            arrPtr,
            idxVal,
            "elem_ptr"
        );
    }

    g.error_reporter.emitError(
        this->line, this->column,
        "Type Error",
        "Attempted to index into a non-array/non-pointer type."
    );
    g.has_errors = true;
    return nullptr;
}

inline llvm::Value* IndexNode::codegen(CodeGenerator& g) {
    llvm::Value* elemPtr = codegenAddress(g);
    if (!elemPtr) return nullptr;

    if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(elemPtr)) {
        llvm::Type* elemType = gep->getResultElementType();

        if (elemType->isArrayTy()) {
            return elemPtr;
        }

        return g.builder.CreateLoad(elemType, elemPtr, "element_val");
    }

    return nullptr;
}

inline llvm::Value* BinaryOpNode::codegen(CodeGenerator& g) {
    llvm::Value* L = left->codegen(g);
    llvm::Value* R = right->codegen(g);

    if (!L || !R) return nullptr;

    llvm::Type* lTy = L->getType();
    llvm::Type* rTy = R->getType();

    llvm::Type* targetTy = lTy;

    if (lTy->isDoubleTy() || rTy->isDoubleTy()) {
        targetTy = g.builder.getDoubleTy();
    } else if (lTy->isFloatTy() || rTy->isFloatTy()) {
        targetTy = g.builder.getFloatTy();
    } else if (lTy->isIntegerTy(64) || rTy->isIntegerTy(64)) {
        targetTy = g.builder.getInt64Ty();
    } else {
        targetTy = g.builder.getInt32Ty();
    }

    L = castToType(g, L, targetTy, this->line, this->column);
    R = castToType(g, R, targetTy, this->line, this->column);

    if (!L || !R) return nullptr;

    if (targetTy->isFloatingPointTy()) {
        if (op == "+") return g.builder.CreateFAdd(L, R, "addtmp");
        if (op == "-") return g.builder.CreateFSub(L, R, "subtmp");
        if (op == "*") return g.builder.CreateFMul(L, R, "multmp");
        if (op == "/") return g.builder.CreateFDiv(L, R, "divtmp");
        if (op == "<") return g.builder.CreateFCmpOLT(L, R, "cmptmp");
        if (op == "<=") return g.builder.CreateFCmpOLE(L, R, "cmptmp");
        if (op == ">") return g.builder.CreateFCmpOGT(L, R, "cmptmp");
        if (op == ">=") return g.builder.CreateFCmpOGE(L, R, "cmptmp");
        if (op == "==") return g.builder.CreateFCmpOEQ(L, R, "cmptmp");
        if (op == "!=") return g.builder.CreateFCmpONE(L, R, "cmptmp");
    } else {
        if (op == "+") return g.builder.CreateAdd(L, R, "addtmp");
        if (op == "-") return g.builder.CreateSub(L, R, "subtmp");
        if (op == "*") return g.builder.CreateMul(L, R, "multmp");
        if (op == "/") return g.builder.CreateSDiv(L, R, "divtmp");
        if (op == "<") return g.builder.CreateICmpSLT(L, R, "cmptmp");
        if (op == "<=") return g.builder.CreateICmpSLE(L, R, "cmptmp");
        if (op == ">") return g.builder.CreateICmpSGT(L, R, "cmptmp");
        if (op == ">=") return g.builder.CreateICmpSGE(L, R, "cmptmp");
        if (op == "==") return g.builder.CreateICmpEQ(L, R, "cmptmp");
        if (op == "!=") return g.builder.CreateICmpNE(L, R, "cmptmp");
    }

    g.error_reporter.emitError(this->line, this->column, "Syntax Error", "Unsupported binary operator: " + op);
    g.has_errors = true;
    return nullptr;
}

inline llvm::Value* NullNode::codegen(CodeGenerator& g) {
    // Generates an i8* null pointer (or generic null pointer in LLVM IR)
    return llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(g.context));
}


inline llvm::Value* WhileNode::codegen(CodeGenerator& g) {
    if (init) {
        init->codegen(g);
    }

    llvm::Function* parentFunc = g.builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(g.context, "while.cond", parentFunc);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(g.context, "while.body", parentFunc);
    llvm::BasicBlock* updateBB = update ? llvm::BasicBlock::Create(g.context, "while.update", parentFunc) : nullptr;
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(g.context, "while.end", parentFunc);

    g.builder.CreateBr(condBB);

    g.builder.SetInsertPoint(condBB);
    llvm::Value* condVal = condition->codegen(g);
    if (!condVal) return nullptr;

    if (condVal->getType()->isFloatingPointTy()) {
        condVal = g.builder.CreateFCmpONE(
            condVal, llvm::ConstantFP::get(condVal->getType(), 0.0), "cond_bool");
    } else if (condVal->getType()->isIntegerTy() && !condVal->getType()->isIntegerTy(1)) {
        condVal = g.builder.CreateICmpNE(
            condVal, llvm::ConstantInt::get(condVal->getType(), 0), "cond_bool");
    }

    g.builder.CreateCondBr(condVal, loopBB, afterBB);

    g.builder.SetInsertPoint(loopBB);
    body->codegen(g);

    if (!g.builder.GetInsertBlock()->getTerminator()) {
        g.builder.CreateBr(updateBB ? updateBB : condBB);
    }

    if (updateBB) {
        g.builder.SetInsertPoint(updateBB);
        update->codegen(g);
        if (!g.builder.GetInsertBlock()->getTerminator()) {
            g.builder.CreateBr(condBB);
        }
    }

    g.builder.SetInsertPoint(afterBB);
    return nullptr;
}

inline llvm::Value* BlockNode::codegen(CodeGenerator& g) {
    llvm::Value* last = nullptr;
    for (auto& stmt : statements) {
        last = stmt->codegen(g);
    }
    return last;
}

inline llvm::Value* ReturnNode::codegen(CodeGenerator& g) {
    if (expression) {
        llvm::Value* val = expression->codegen(g);
        if (!val) return nullptr;

        llvm::AllocaInst* returnAlloca = g.lookup("#return");
        if (returnAlloca) {
            llvm::Type* expectedType = returnAlloca->getAllocatedType();

            // If returning an array pointer (e.g. from ArrayLiteral or array var)
            // load the array aggregate value so it matches returnAlloca's element type
            if (val->getType()->isPointerTy() && expectedType->isArrayTy()) {
                val = g.builder.CreateLoad(expectedType, val, "ret_arr_val");
            } else {
                val = castToType(g, val, expectedType, line, column);
            }

            g.builder.CreateStore(val, returnAlloca);
        }
    }

    if (g.currentReturnBB) {
        g.builder.CreateBr(g.currentReturnBB);

        llvm::Function* currentFunc = g.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(g.context, "after_return", currentFunc);
        g.builder.SetInsertPoint(deadBB);
    }

    return nullptr;
}

inline llvm::Value* FunctionNode::codegen(CodeGenerator& g) {
    auto parentBlock = g.builder.GetInsertBlock();
    auto oldNamedValues = g.named_values;
    auto oldReturnBB = g.currentReturnBB;
    g.named_values.clear();

    llvm::Type* retLLVMType = getLLVMType(g, return_type, this->line, this->column);
    if (!retLLVMType) return nullptr;

    bool isVoid = retLLVMType->isVoidTy();

    std::vector<llvm::Type*> paramTypes;
    for (const auto& p : params) {
        llvm::Type* pType = getLLVMType(g, p.type, this->line, this->column);
        if (!pType) return nullptr;
        paramTypes.push_back(pType);
    }

    llvm::FunctionType* ft = llvm::FunctionType::get(retLLVMType, paramTypes, is_variadic);
    llvm::Function* f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, g.module.get());
    f->setCallingConv(llvm::CallingConv::C);

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(g.context, "entry", f);
    llvm::BasicBlock* returnBB = llvm::BasicBlock::Create(g.context, "return_block", f);
    g.currentReturnBB = returnBB;

    g.builder.SetInsertPoint(entryBB);

    llvm::AllocaInst* returnAlloca = nullptr;
    if (!isVoid) {
        returnAlloca = g.builder.CreateAlloca(retLLVMType, nullptr, "#return");
        // Zero-initialize any type, including arrays
        g.builder.CreateStore(llvm::Constant::getNullValue(retLLVMType), returnAlloca);
        g.named_values["#return"] = returnAlloca;
    }

    size_t idx = 0;
    for (auto& arg : f->args()) {
        if (idx >= params.size()) break;
        std::string argName = params[idx++].name;
        arg.setName(argName);
        llvm::AllocaInst* alloca = g.builder.CreateAlloca(arg.getType(), nullptr, argName);
        g.builder.CreateStore(&arg, alloca);
        g.named_values[argName] = alloca;
    }

    body->codegen(g);

    if (!g.builder.GetInsertBlock()->getTerminator()) {
        g.builder.CreateBr(returnBB);
    }

    g.builder.SetInsertPoint(returnBB);
    if (isVoid) {
        g.builder.CreateRetVoid();
    } else {
        llvm::Value* rawRetVal = g.builder.CreateLoad(
            returnAlloca->getAllocatedType(),
            returnAlloca,
            "return_val"
        );
        llvm::Value* castedRetVal = castToType(g, rawRetVal, retLLVMType, this->line, this->column);
        g.builder.CreateRet(castedRetVal);
    }

    llvm::verifyFunction(*f);

    g.named_values = oldNamedValues;
    g.currentReturnBB = oldReturnBB;
    if (parentBlock) {
        g.builder.SetInsertPoint(parentBlock);
    }

    return f;
}

inline llvm::Value* CallNode::codegen(CodeGenerator& g) {
    llvm::Function* callee = g.module->getFunction(name);

    if (!callee) {
        std::vector<llvm::Value*> argsV;
        std::vector<llvm::Type*> paramTypes;

        bool isVariadicFunc = (name == "printf" || name == "scanf" || name == "fscanf" || name == "sprintf");

        for (size_t i = 0; i < arguments.size(); ++i) {
            llvm::Value* val = arguments[i]->codegen(g);
            if (!val) return nullptr;

            llvm::Type* valType = val->getType();

            if (valType->isFloatTy()) {
                val = g.builder.CreateFPExt(val, g.builder.getDoubleTy(), "promoted_double");
            } else if (valType->isIntegerTy() && valType->getIntegerBitWidth() < 32) {
                val = g.builder.CreateSExt(val, g.builder.getInt32Ty(), "promoted_int");
            }

            argsV.push_back(val);

            if (!isVariadicFunc) {
                paramTypes.push_back(val->getType());
            }
        }

        llvm::Type* defaultRetTy = g.builder.getDoubleTy();
        if (name == "printf" || name == "scanf" || name == "puts" || name == "abs" || name == "system") {
            defaultRetTy = g.builder.getInt32Ty();
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(defaultRetTy, paramTypes, isVariadicFunc);
        callee = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, g.module.get());
        callee->setCallingConv(llvm::CallingConv::C);

        if (defaultRetTy->isVoidTy()) {
            return g.builder.CreateCall(callee, argsV);
        }
        return g.builder.CreateCall(callee, argsV, "calltmp");
    }

    bool isVarArg = callee->isVarArg();
    size_t minArgs = callee->arg_size();

    if ((!isVarArg && arguments.size() != minArgs) || (isVarArg && arguments.size() < minArgs)) {
        g.error_reporter.emitError(
            this->line, this->column,
            "Type Error: Argument Count Mismatch",
            "Incorrect number of arguments passed to function '" + name + "'."
        );
        g.has_errors = true;
        return nullptr;
    }

    std::vector<llvm::Value*> argsV;
    for (size_t i = 0; i < arguments.size(); ++i) {
        llvm::Value* val = arguments[i]->codegen(g);
        if (!val) return nullptr;

        if (i < minArgs) {
            llvm::Type* expectedType = callee->getFunctionType()->getParamType(i);
            argsV.push_back(castToType(g, val, expectedType, this->line, this->column));
        } else {
            llvm::Type* valType = val->getType();
            if (valType->isFloatTy()) {
                val = g.builder.CreateFPExt(val, g.builder.getDoubleTy(), "promoted_double");
            } else if (valType->isIntegerTy() && valType->getIntegerBitWidth() < 32) {
                val = g.builder.CreateSExt(val, g.builder.getInt32Ty(), "promoted_int");
            }
            argsV.push_back(val);
        }
    }

    if (callee->getReturnType()->isVoidTy()) {
        return g.builder.CreateCall(callee, argsV);
    }

    return g.builder.CreateCall(callee, argsV, "calltmp");
}

inline llvm::Value* HeaderImportNode::codegen(CodeGenerator& generator) {
    generator.registerHeaderInclude(header_path);
    return nullptr;
}

inline llvm::Value* ConditionNode::codegen(CodeGenerator& g) {
    if (branches.empty()) return nullptr;

    llvm::Function* currentFunc = g.builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(g.context, "if.cont");

    for (size_t i = 0; i < branches.size(); ++i) {
        llvm::Value* condVal = branches[i].condition->codegen(g);
        if (!condVal) return nullptr;

        if (condVal->getType()->isDoubleTy()) {
            condVal = g.builder.CreateFCmpONE(
                condVal, llvm::ConstantFP::get(g.context, llvm::APFloat(0.0)), "ifcond");
        } else if (condVal->getType()->isIntegerTy() && condVal->getType()->getIntegerBitWidth() != 1) {
            condVal = g.builder.CreateICmpNE(
                condVal, llvm::ConstantInt::get(condVal->getType(), 0), "ifcond");
        }

        llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(g.context, "then", currentFunc);
        llvm::BasicBlock* nextBB = nullptr;

        if (i + 1 < branches.size() || else_body) {
            nextBB = llvm::BasicBlock::Create(g.context, "else", currentFunc);
            g.builder.CreateCondBr(condVal, thenBB, nextBB);
        } else {
            g.builder.CreateCondBr(condVal, thenBB, mergeBB);
        }

        g.builder.SetInsertPoint(thenBB);
        if (branches[i].body) {
            branches[i].body->codegen(g);
        }
        if (!g.builder.GetInsertBlock()->getTerminator()) {
            g.builder.CreateBr(mergeBB);
        }

        if (nextBB) {
            g.builder.SetInsertPoint(nextBB);
        }
    }

    if (else_body) {
        else_body->codegen(g);
        if (!g.builder.GetInsertBlock()->getTerminator()) {
            g.builder.CreateBr(mergeBB);
        }
    }

    currentFunc->insert(currentFunc->end(), mergeBB);
    g.builder.SetInsertPoint(mergeBB);

    return nullptr;
}

inline llvm::Value* ExternNode::codegen(CodeGenerator& g) {
    if (g.module->getFunction(name)) {
        return g.module->getFunction(name);
    }

    llvm::Type* retLLVMType = getLLVMType(g, return_type, this->line, this->column);
    if (!retLLVMType) return nullptr;

    std::vector<llvm::Type*> paramTypes;
    for (const auto& p : params) {
        llvm::Type* pType = getLLVMType(g, p.type, this->line, this->column);
        if (!pType) return nullptr;
        paramTypes.push_back(pType);
    }

    llvm::FunctionType* ft = llvm::FunctionType::get(retLLVMType, paramTypes, is_variadic);
    return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, g.module.get());
}

#endif // COMPILER_CODEGEN_H