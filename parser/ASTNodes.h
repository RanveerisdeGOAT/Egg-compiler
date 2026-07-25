#ifndef COMPILER_ASTNODES_H
#define COMPILER_ASTNODES_H

#include <memory>
#include <string>
#include <vector>

class CodeGenerator; // Forward declaration
namespace llvm { class Value; }

// Base class for all AST Nodes
class ASTNode {
public:
    size_t line = 0;
    size_t column = 0;

    ASTNode(size_t line = 0, size_t column = 0)
        : line(line), column(column) {}

    virtual ~ASTNode() = default;
    virtual llvm::Value* codegen(CodeGenerator& g) = 0;
    virtual llvm::Value* codegenAddress(CodeGenerator& g) { return nullptr; }
};

struct ProgramNode : public ASTNode {
    std::vector<std::unique_ptr<ASTNode>> statements;

    explicit ProgramNode(std::vector<std::unique_ptr<ASTNode>> stmts, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), statements(std::move(stmts)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct NumberNode : public ASTNode {
    std::string value;
    bool is_float;

    NumberNode(std::string val, bool is_flt, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), value(std::move(val)), is_float(is_flt) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct CharNode : public ASTNode {
    char value;

    explicit CharNode(char v, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), value(v) {}

    llvm::Value* codegen(CodeGenerator& g) override;
};

struct CharrayNode : public ASTNode {
    std::string value;

    explicit CharrayNode(std::string val, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), value(std::move(val)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct NullNode : public ASTNode {
    NullNode(size_t l, size_t c) : ASTNode(l, c) {}
    llvm::Value* codegen(CodeGenerator& g) override;
};

struct ArrayLiteralNode : public ASTNode {
    std::string element_type;
    std::vector<std::unique_ptr<ASTNode>> elements;

    ArrayLiteralNode(std::string type, std::vector<std::unique_ptr<ASTNode>> elms, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), element_type(std::move(type)), elements(std::move(elms)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct IndexNode : public ASTNode {
    std::unique_ptr<ASTNode> array;
    std::unique_ptr<ASTNode> index;

    IndexNode(std::unique_ptr<ASTNode> arr, std::unique_ptr<ASTNode> idx, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), array(std::move(arr)), index(std::move(idx)) {}

    llvm::Value* codegen(CodeGenerator& g) override;
    llvm::Value* codegenAddress(CodeGenerator& g) override;
};

struct VariableNode : public ASTNode {
    std::string name;

    explicit VariableNode(std::string n, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), name(std::move(n)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
    llvm::Value* codegenAddress(CodeGenerator& generator) override;
};

struct VarDeclNode : public ASTNode {
    std::string type;
    std::string name;
    std::unique_ptr<ASTNode> initializer;

    VarDeclNode(std::string t, std::string n, std::unique_ptr<ASTNode> init, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), type(std::move(t)), name(std::move(n)), initializer(std::move(init)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct AssignmentNode : public ASTNode {
    std::string op; // "=", "+=", "-=", "*=", "/="
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> expression;

    AssignmentNode(std::unique_ptr<ASTNode> target, std::string op, std::unique_ptr<ASTNode> expr, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), op(std::move(op)), target(std::move(target)), expression(std::move(expr)) {}

    // Backwards-compatible constructor for standard '='
    AssignmentNode(std::unique_ptr<ASTNode> target, std::unique_ptr<ASTNode> expr, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), op("="), target(std::move(target)), expression(std::move(expr)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct UnaryOpNode : public ASTNode {
    std::string op; // "@" for address-of, "*" for heap allocation
    std::unique_ptr<ASTNode> operand;

    UnaryOpNode(std::string op, std::unique_ptr<ASTNode> operand, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), op(std::move(op)), operand(std::move(operand)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
    llvm::Value* codegenAddress(CodeGenerator& generator) override;
};

struct FreeNode : public ASTNode {
    std::unique_ptr<ASTNode> expression;

    FreeNode(std::unique_ptr<ASTNode> expr, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), expression(std::move(expr)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct BlockNode : public ASTNode {
    std::vector<std::unique_ptr<ASTNode>> statements;

    explicit BlockNode(std::vector<std::unique_ptr<ASTNode>> stmts, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), statements(std::move(stmts)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};


struct ReturnNode : public ASTNode {
    std::unique_ptr<ASTNode> expression;

    ReturnNode(std::unique_ptr<ASTNode> expr, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), expression(std::move(expr)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct Param {
    std::string type;
    std::string name;
};

struct FunctionNode : public ASTNode {
    std::string name;
    std::vector<Param> params;
    std::string return_type;
    std::unique_ptr<BlockNode> body;
    bool is_variadic; // Flag for variadic function definition (...)

    FunctionNode(std::string name, std::vector<Param> params, std::string ret_type,
                 std::unique_ptr<BlockNode> body, bool is_variadic = false,
                 size_t line = 0, size_t column = 0)
        : ASTNode(line, column), name(std::move(name)), params(std::move(params)),
          return_type(std::move(ret_type)), body(std::move(body)),
          is_variadic(is_variadic) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct CallNode : public ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> arguments;

    CallNode(std::string name, std::vector<std::unique_ptr<ASTNode>> arguments, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), name(std::move(name)), arguments(std::move(arguments)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct ExternNode : public ASTNode {
    std::string name;
    std::string return_type;
    std::vector<Param> params;
    bool is_variadic; // Added flag for '...'

    ExternNode(std::string name, std::string ret_type, std::vector<Param> params, bool is_variadic = false, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), name(std::move(name)), return_type(std::move(ret_type)), params(std::move(params)), is_variadic(is_variadic) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct HeaderImportNode : public ASTNode {
    std::string header_path; // e.g. "<stdio.h>" or "\"my_lib.h\""

    HeaderImportNode(std::string path, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), header_path(std::move(path)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct BinaryOpNode : public ASTNode {
    std::string op;
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;

    BinaryOpNode(std::string op, std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), op(std::move(op)), left(std::move(l)), right(std::move(r)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct WhileNode : public ASTNode {
    std::unique_ptr<ASTNode> init;
    std::unique_ptr<ASTNode> update;
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<BlockNode> body;

    WhileNode(std::unique_ptr<ASTNode> init,
              std::unique_ptr<ASTNode> update,
              std::unique_ptr<ASTNode> condition,
              std::unique_ptr<BlockNode> body,
              size_t line = 0, size_t column = 0)
        : ASTNode(line, column),
          init(std::move(init)),
          update(std::move(update)),
          condition(std::move(condition)),
          body(std::move(body)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct Branch {
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<BlockNode> body;
};

struct ConditionNode : public ASTNode {
    std::vector<Branch> branches;
    std::unique_ptr<BlockNode> else_body;

    ConditionNode(std::vector<Branch> branches, std::unique_ptr<BlockNode> else_body = nullptr, size_t line = 0, size_t column = 0)
        : ASTNode(line, column), branches(std::move(branches)), else_body(std::move(else_body)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

// 1. Struct Definition Node (e.g., struct Point { x: int, y: int })
struct StructField {
    std::string name;
    std::string type;
};

struct StructDefNode : public ASTNode {
    std::string name;
    std::vector<StructField> fields;

    StructDefNode(std::string name, std::vector<StructField> fields, size_t line = 0, size_t col = 0)
        : ASTNode(line, col), name(std::move(name)), fields(std::move(fields)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

struct MemberAccessNode : public ASTNode {
    std::unique_ptr<ASTNode> base;
    std::string field_name;


    MemberAccessNode(std::unique_ptr<ASTNode> base_expr, std::string field, size_t line = 0, size_t col = 0)
        : ASTNode(line, col), base(std::move(base_expr)), field_name(std::move(field)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
    llvm::Value* codegenAddress(CodeGenerator& generator) override;
};

struct MemberAssignNode : public ASTNode {
    std::string struct_var_name;
    std::string field_name;
    std::unique_ptr<ASTNode> value;

    MemberAssignNode(std::string var_name, std::string field, std::unique_ptr<ASTNode> val, size_t line = 0, size_t col = 0)
        : ASTNode(line, col), struct_var_name(std::move(var_name)), field_name(std::move(field)), value(std::move(val)) {}

    llvm::Value* codegen(CodeGenerator& generator) override;
};

#endif // COMPILER_ASTNODES_H