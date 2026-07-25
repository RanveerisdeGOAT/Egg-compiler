#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H

#include "../lexer/lexer.h"
#include "ASTNodes.h"
#include "../error/error.h"
#include <memory>
#include <stdexcept>
#include <iostream>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, ErrorReporter error_reporter)
        : tokens_(std::move(tokens)), error_reporter(std::move(error_reporter)) {}

    std::unique_ptr<ASTNode> parse() {
        std::vector<std::unique_ptr<ASTNode>> statements;
        skipNewlines();

        size_t startLine = peek().line;
        size_t startCol = peek().column;

        while (!isAtEnd()) {
            try {
                auto stmt = parseStatement();
                if (stmt) {
                    statements.push_back(std::move(stmt));
                }

                if (isAtEnd() || check(TokenTypes::NewLine)) {
                    skipNewlines();
                } else {
                    error_reporter.emitError(
                        peek().line, peek().column,
                        "Syntax Error: Expected newline",
                        "Statements must be separated by newlines or semicolons.",
                        "Add a newline after the statement or check for missing closing brackets."
                    );
                    synchronize();
                }
            } catch (const std::exception& e) {
                synchronize();
            }
        }

        return std::make_unique<ProgramNode>(std::move(statements), startLine, startCol);
    }

private:
    std::vector<Token> tokens_;
    size_t current_ = 0;
    ErrorReporter error_reporter;

    const Token& peek() const { return tokens_[current_]; }

    const Token& previous() const {
        return current_ > 0 ? tokens_[current_ - 1] : tokens_[0];
    }

    bool isAtEnd() const { return peek().type == TokenTypes::EndOfFile; }

    Token advance() {
        if (!isAtEnd()) current_++;
        return tokens_[current_ - 1];
    }

    bool check(TokenTypes type) const {
        if (isAtEnd()) return false;
        return peek().type == type;
    }

    bool match(const std::vector<TokenTypes>& types) {
        for (TokenTypes type : types) {
            if (check(type)) {
                advance();
                return true;
            }
        }
        return false;
    }

    Token consume(TokenTypes type, const std::string& title, const std::string& explanation, const std::string& hint = "") {
        if (check(type)) return advance();

        Token errTok = peek();
        error_reporter.emitError(
            errTok.line, errTok.column,
            title,
            explanation + " Found '" + (errTok.value.empty() ? tokenTypeToString(errTok.type) : errTok.value) + "' instead.",
            hint
        );
        throw std::runtime_error("Parsing error triggered synchronization.");
    }

    void synchronize() {
        advance();

        while (!isAtEnd()) {
            if (previous().type == TokenTypes::NewLine || previous().type == TokenTypes::Semicolon) return;

            switch (peek().type) {
                case TokenTypes::Func:
                case TokenTypes::Var:
                case TokenTypes::For:
                case TokenTypes::If:
                case TokenTypes::While:
                case TokenTypes::Return:
                case TokenTypes::Free:
                    return;
                default:
                    break;
            }

            advance();
        }
    }

    void skipNewlines() {
        while (match({TokenTypes::NewLine})) {}
    }

    std::unique_ptr<ASTNode> parseStatement() {
        if (check(TokenTypes::Func))   return parseFunction();
        if (check(TokenTypes::While))  return parseWhile();
        if (check(TokenTypes::If))     return parseIf();
        if (check(TokenTypes::Var))    return parseVarDecl();
        if (check(TokenTypes::Extern)) return parseExtern();
        if (check(TokenTypes::Struct)) return parseStruct();


        if (check(TokenTypes::Free)) {
            Token freeTok = advance();
            auto expr = parseExpression();
            return std::make_unique<FreeNode>(std::move(expr), freeTok.line, freeTok.column);
        }

        if (check(TokenTypes::Return)) {
            Token retTok = advance();
            auto expr = parseExpression();
            return std::make_unique<ReturnNode>(std::move(expr), retTok.line, retTok.column);
        }

        // 1. Parse LHS expression target (e.g. ptr, <ptr, arr[0])
        auto expr = parseExpression();

        // 2. Delegate assignment handling if assignment operator is matched
        if (match({TokenTypes::Equals, TokenTypes::Increment, TokenTypes::Decrement, TokenTypes::Multiply, TokenTypes::Divide})) {
            return parseAssignment(std::move(expr));
        }

        return expr;
    }

    std::unique_ptr<ASTNode> parseIf() {
        Token ifTok = consume(TokenTypes::If, "Syntax Error", "Expected 'if' keyword to start conditional block.");
        skipNewlines();

        std::vector<Branch> branches;

        auto if_cond = parseExpression();
        skipNewlines();

        consume(TokenTypes::Question, "Syntax Error: Missing '?'",
                "Expected '?' after 'if' condition expression.",
                "Add '?' before the body block '{ ... }'.");

        skipNewlines();
        auto if_body = parseBlock(); // Do NOT call skipNewlines() here!

        branches.push_back({std::move(if_cond), std::move(if_body)});

        // Check if an 'else' follows on the same or next line
        while (check(TokenTypes::NewLine)) {
            // If the next token after newline is 'else', advance across newline
            if (current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == TokenTypes::Else) {
                advance();
            } else {
                break;
            }
        }

        while (check(TokenTypes::Else)) {
            advance();
            skipNewlines();

            if (match({TokenTypes::If})) {
                skipNewlines();
                auto cond = parseExpression();
                skipNewlines();

                consume(TokenTypes::Question, "Syntax Error: Missing '?'",
                        "Expected '?' after 'else if' condition.",
                        "Add '?' before the body block.");

                skipNewlines();
                auto body = parseBlock(); // Do NOT call skipNewlines() here!

                branches.push_back({std::move(cond), std::move(body)});
            } else {
                auto else_body = parseBlock();
                return std::make_unique<ConditionNode>(std::move(branches), std::move(else_body), ifTok.line, ifTok.column);
            }
        }

        return std::make_unique<ConditionNode>(std::move(branches), nullptr, ifTok.line, ifTok.column);
    }

    // Syntax: struct Point { x: int, y: float }
    std::unique_ptr<ASTNode> parseStruct() {
        consume(TokenTypes::Struct, "Syntax Error", "Expected 'struct' keyword.");
        Token nameTok = consume(TokenTypes::Identifier, "Syntax Error", "Expected struct name.");

        consume(TokenTypes::LeftBrace, "Syntax Error", "Expected '{' after struct name.");

        skipNewlines();

        std::vector<StructField> fields;
        while (!check(TokenTypes::RightBrace) && !isAtEnd()) {
            skipNewlines();
            Token fName = consume(TokenTypes::Identifier, "Syntax Error", "Expected field name.");
            consume(TokenTypes::Colon, "Syntax Error", "Expected ':' after field name.");
            std::string fType = parseType();

            fields.push_back({fName.value, fType});

            if (check(TokenTypes::Comma)) advance();
            skipNewlines();
        }

        skipNewlines();

        consume(TokenTypes::RightBrace, "Syntax Error", "Expected '}' after struct fields.");

        return std::make_unique<StructDefNode>(nameTok.value, fields, nameTok.line, nameTok.column);
    }

    std::unique_ptr<ASTNode> parseAssignment(std::unique_ptr<ASTNode> target) {
        Token opToken = previous(); // Holds '=', '+=', '-=', '*=', or '/='
        auto value = parseExpression();

        return std::make_unique<AssignmentNode>(
            std::move(target),
            opToken.value,
            std::move(value),
            opToken.line,
            opToken.column
        );
    }

    std::unique_ptr<ASTNode> parseWhile() {
        Token whileTok = consume(TokenTypes::While, "Syntax Error", "Expected 'while' keyword.");

        std::unique_ptr<ASTNode> init = nullptr;
        std::unique_ptr<ASTNode> update = nullptr;
        std::unique_ptr<ASTNode> condition = nullptr;

        auto first = parseStatement();
        skipNewlines();

        if (match({TokenTypes::Question})) {
            condition = std::move(first);
        } else if (match({TokenTypes::Semicolon})) {
            init = std::move(first);
            skipNewlines();

            if (check(TokenTypes::LeftBrace)) {
                update = parseBlock();
            } else {
                update = parseStatement();
            }
            skipNewlines();

            consume(TokenTypes::Semicolon, "Syntax Error: Invalid while loop header",
                    "Expected ';' after loop update statement.",
                    "Check while header format: while init; update; condition ? { ... }");

            skipNewlines();
            condition = parseExpression();
            skipNewlines();

            consume(TokenTypes::Question, "Syntax Error: Invalid while condition",
                    "Expected '?' after while loop condition.",
                    "Add '?' before loop body block '{ ... }'.");
        } else {
            Token errTok = peek();
            error_reporter.emitError(
                errTok.line, errTok.column,
                "Syntax Error: Invalid while loop syntax",
                "Expected '?' or ';' in while loop header.",
                "Syntax should be either 'while cond ? { ... }' or 'while init; update; cond ? { ... }'."
            );
            throw std::runtime_error("Invalid while loop header.");
        }

        skipNewlines();
        auto body = parseBlock();

        return std::make_unique<WhileNode>(
            std::move(init),
            std::move(update),
            std::move(condition),
            std::move(body),
            whileTok.line, whileTok.column
        );
    }

    std::unique_ptr<ASTNode> parseVarDecl() {
        Token varTok = consume(TokenTypes::Var, "Syntax Error", "Expected 'var' keyword before declaration.");

        Token nameToken = consume(TokenTypes::Identifier, "Syntax Error: Missing variable name",
                                  "Expected a valid identifier after 'var'.");

        consume(TokenTypes::Colon, "Syntax Error: Missing type separator",
                "Expected ':' after variable name in declaration.",
                "Example format: 'var x: int = 10'.");

        std::string fullType = parseType();

        std::unique_ptr<ASTNode> initExpr = nullptr;
        if (match({TokenTypes::Equals})) {
            initExpr = parseExpression();
        }

        return std::make_unique<VarDeclNode>(fullType, nameToken.value, std::move(initExpr), varTok.line, varTok.column);
    }

    std::unique_ptr<ASTNode> parseFunction() {
        Token funcTok = consume(TokenTypes::Func, "Syntax Error", "Expected 'func' keyword.");
        Token nameTok = consume(TokenTypes::Identifier, "Syntax Error", "Expected function name.");

        consume(TokenTypes::LeftParen, "Syntax Error", "Expected '(' after function name.");

        std::vector<Param> params;
        bool isVariadic = false;

        if (!check(TokenTypes::RightParen)) {
            do {
                // Check for variadic token '...'
                if (check(TokenTypes::Ellipsis)) {
                    if (check(TokenTypes::Ellipsis)) {
                        advance();
                    } else {
                        advance(); advance(); advance(); // Consume 3 dots
                    }
                    isVariadic = true;
                    break; // '...' must be the final parameter
                }

                Token pName = consume(TokenTypes::Identifier, "Syntax Error", "Expected parameter name.");
                consume(TokenTypes::Colon, "Syntax Error", "Expected ':' after parameter name.");
                params.push_back({parseType(), pName.value});
            } while (match({TokenTypes::Comma}));
        }

        consume(TokenTypes::RightParen, "Syntax Error", "Expected ')' after parameters.");

        // Return type parsing
        std::string retType = "void";
        if (match({TokenTypes::Colon})) {
            retType = parseType();
        }

        skipNewlines();
        auto body = parseBlock();

        return std::make_unique<FunctionNode>(
            nameTok.value, std::move(params), retType, std::move(body), isVariadic, funcTok.line, funcTok.column
        );
    }

    std::unique_ptr<ASTNode> parseExtern() {
        consume(TokenTypes::Extern, "Syntax Error", "Expected 'extern' keyword.");

        // Check if the next token is a header import (e.g., "stdio.h" or <stdio.h>)
        if (check(TokenTypes::Less) || check(TokenTypes::Charray)) {
            std::string headerPath;

            if (match({TokenTypes::Less})) {
                Token nameTok = consume(TokenTypes::Identifier, "Syntax Error", "Expected header name.");
                std::string ext = "";
                if (match({TokenTypes::Dot})) {
                    Token extTok = consume(TokenTypes::Identifier, "Syntax Error", "Expected extension.");
                    ext = "." + extTok.value;
                }
                consume(TokenTypes::Greater, "Syntax Error", "Expected '>' closing header path.");
                headerPath = "<" + nameTok.value + ext + ">";
            } else {
                Token pathTok = advance();
                headerPath = "\"" + pathTok.value + "\"";
            }

            return std::make_unique<HeaderImportNode>(headerPath, previous().line, previous().column);
        }

        Token nameTok = consume(TokenTypes::Identifier, "Syntax Error", "Expected function name.");

        consume(TokenTypes::LeftParen, "Syntax Error", "Expected '(' after function name.");

        std::vector<Param> params;
        bool isVariadic = false;

        if (!check(TokenTypes::RightParen)) {
            do {
                if (check(TokenTypes::Ellipsis) || check(TokenTypes::Dot)) {
                    if (check(TokenTypes::Ellipsis)) advance();
                    else { advance(); advance(); advance(); }
                    isVariadic = true;
                    break;
                }

                Token pName = consume(TokenTypes::Identifier, "Syntax Error", "Expected parameter name or '...'.");
                consume(TokenTypes::Colon, "Syntax Error", "Expected ':' after parameter name.");
                std::string pType = parseType();

                // FIX: Store as {type, name} to match FunctionNode param ordering
                params.push_back({pType, pName.value});
            } while (match({TokenTypes::Comma}));
        }

        consume(TokenTypes::RightParen, "Syntax Error", "Expected ')' after parameters.");
        consume(TokenTypes::Colon, "Syntax Error", "Expected ':' before return type.");
        std::string retType = parseType();

        return std::make_unique<ExternNode>(
            nameTok.value, retType, std::move(params), isVariadic, nameTok.line, nameTok.column
        );
    }

    std::unique_ptr<BlockNode> parseBlock() {
        Token braceTok = consume(TokenTypes::LeftBrace, "Syntax Error: Missing '{'",
                                 "Expected '{' to begin block execution body.");

        skipNewlines();
        std::vector<std::unique_ptr<ASTNode>> statements;

        while (!check(TokenTypes::RightBrace) && !isAtEnd()) {
            try {
                auto stmt = parseStatement();
                if (stmt) {
                    statements.push_back(std::move(stmt));
                }

                if (check(TokenTypes::RightBrace)) break;

                if (check(TokenTypes::NewLine)) {
                    skipNewlines();
                } else {
                    error_reporter.emitError(
                        peek().line, peek().column,
                        "Syntax Error: Missing statement separator",
                        "Expected newline between statements inside block.",
                        "Separate statements onto new lines."
                    );
                    synchronize();
                }
            } catch (const std::exception& e) {
                synchronize();
            }
        }

        consume(TokenTypes::RightBrace, "Syntax Error: Missing '}'",
                "Expected '}' to close block.");

        return std::make_unique<BlockNode>(std::move(statements), braceTok.line, braceTok.column);
    }

    std::unique_ptr<ASTNode> parseIdentifier() {
        Token nameToken = consume(TokenTypes::Identifier, "Syntax Error", "Expected identifier name.");

        // Function call: foo(a, b)
        if (match({TokenTypes::LeftParen})) {
            std::vector<std::unique_ptr<ASTNode>> args;
            if (!check(TokenTypes::RightParen)) {
                do {
                    args.push_back(parseExpression());
                } while (match({TokenTypes::Comma}));
            }

            consume(TokenTypes::RightParen, "Syntax Error: Missing ')'",
                    "Expected ')' after function arguments list.");

            return std::make_unique<CallNode>(nameToken.value, std::move(args), nameToken.line, nameToken.column);
        }

        // Base variable expression
        std::unique_ptr<ASTNode> currentExpr = std::make_unique<VariableNode>(nameToken.value, nameToken.line, nameToken.column);

        // Loop to handle chained dot access (.) and array indexing ([])
        while (check(TokenTypes::LeftBracket) || check(TokenTypes::Dot)) {

            // 1. Array indexing: expr[index]
            if (match({TokenTypes::LeftBracket})) {
                Token bracketTok = previous();
                auto indexExpr = parseExpression();

                consume(TokenTypes::RightBracket, "Syntax Error: Missing ']'",
                        "Expected ']' after array index expression.",
                        "Example format: 'arr[0]'.");

                currentExpr = std::make_unique<IndexNode>(
                    std::move(currentExpr),
                    std::move(indexExpr),
                    bracketTok.line,
                    bracketTok.column
                );
            }
            // 2. Dot notation / Member access: expr.field
            else if (match({TokenTypes::Dot})) {
                Token dotTok = previous();
                Token fieldToken = consume(
                    TokenTypes::Identifier,
                    "Syntax Error: Invalid field access",
                    "Expected field name after '.'.",
                    "Example format: 'object.property'."
                );

                // Expects MemberAccessNode(baseExpr, fieldName, line, col)
                currentExpr = std::make_unique<MemberAccessNode>(
                    std::move(currentExpr),
                    fieldToken.value,
                    dotTok.line,
                    dotTok.column
                );
            }
        }

        return currentExpr;
    }

    std::unique_ptr<ASTNode> parseExpression() {
        auto expr = parseTerm();

        while (match({TokenTypes::EqualEqual, TokenTypes::BangEqual, TokenTypes::LessEqual,
                      TokenTypes::GreaterEqual, TokenTypes::Less, TokenTypes::Greater})) {
            Token opTok = previous();
            std::string op = opTok.value;
            auto right = parseTerm();
            expr = std::make_unique<BinaryOpNode>(op, std::move(expr), std::move(right), opTok.line, opTok.column);
        }

        return expr;
    }

    std::unique_ptr<ASTNode> parseTerm() {
        auto expr = parseFactor();

        while (match({TokenTypes::Plus, TokenTypes::Minus})) {
            Token opTok = previous();
            std::string op = opTok.value;
            auto right = parseFactor();
            expr = std::make_unique<BinaryOpNode>(op, std::move(expr), std::move(right), opTok.line, opTok.column);
        }

        return expr;
    }

    std::string parseType() {
        std::string typeModifier = "";

        // Supports prefix pointer modifiers: @ (address), > (pointer), %> (unique ptr), $> (shared ptr), * (heap)
        while (true) {
            if (match({TokenTypes::PercentGreater}))     { typeModifier += "%>"; }
            else if (match({TokenTypes::DollarGreater})) { typeModifier += "$>"; }
            else if (match({TokenTypes::Greater}))       { typeModifier += ">";  }
            else if (match({TokenTypes::Star}))          { typeModifier += "*";  }
            else if (match({TokenTypes::At}))            { typeModifier += "@";  }
            else { break; }
        }

        // Accept both TokenTypes::Type and TokenTypes::Identifier
        if (check(TokenTypes::Type) || check(TokenTypes::Identifier)) {
            typeModifier += advance().value;
        } else {
            Token errTok = peek();
            error_reporter.emitError(
                errTok.line, errTok.column,
                "Syntax Error: Missing type name",
                "Expected base type name (e.g. int, float, charray). Found '" +
                (errTok.value.empty() ? tokenTypeToString(errTok.type) : errTok.value) + "' instead.",
                "Example: 'var x: int = 5'."
            );
            throw std::runtime_error("Parsing error in type definition.");
        }

        while (match({TokenTypes::LeftBracket})) {
            std::string sizeStr = "";

            if (check(TokenTypes::Integer)) {
                Token sizeToken = advance();
                sizeStr = sizeToken.value;
            }

            consume(TokenTypes::RightBracket, "Syntax Error: Missing ']'",
                    "Expected ']' after type array dimension.",
                    "Example: 'int[2][2]'.");

            typeModifier += "[" + sizeStr + "]";
        }

        return typeModifier;
    }

    std::unique_ptr<ASTNode> parseFactor() {
        // Prefix Unary Operators: @ (Address-of), * (Heap Alloc), < (Dereference), and - (Negation)
        if (match({TokenTypes::At, TokenTypes::Star, TokenTypes::Less, TokenTypes::Minus})) {
            Token opTok = previous();
            std::string opStr;
            if (opTok.type == TokenTypes::At) opStr = "@";
            else if (opTok.type == TokenTypes::Star) opStr = "*";
            else if (opTok.type == TokenTypes::Less) opStr = "<";
            else if (opTok.type == TokenTypes::Minus) opStr = "-";

            auto operand = parseFactor();
            return std::make_unique<UnaryOpNode>(opStr, std::move(operand), opTok.line, opTok.column);
        }

        auto expr = parsePrimary();

        while (match({TokenTypes::Star, TokenTypes::Slash})) {
            Token opTok = previous();
            std::string op = opTok.value;
            auto right = parsePrimary();
            expr = std::make_unique<BinaryOpNode>(op, std::move(expr), std::move(right), opTok.line, opTok.column);
        }

        return expr;
    }

    inline std::string inferNodeType(const ASTNode* node) {
        if (dynamic_cast<const NumberNode*>(node)) {
            auto num = dynamic_cast<const NumberNode*>(node);
            return num->is_float ? "float" : "int";
        }
        if (dynamic_cast<const CharNode*>(node))     return "char";
        if (dynamic_cast<const CharrayNode*>(node))  return "charray";
        return "";
    }

    std::unique_ptr<ASTNode> parseArrayLiteral() {
        Token bracketTok = previous();
        std::vector<std::unique_ptr<ASTNode>> elements;
        std::string inferredType = "int";

        if (!check(TokenTypes::RightBracket)) {
            auto firstElem = parseExpression();
            std::string firstType = inferNodeType(firstElem.get());

            if (!firstType.empty()) {
                inferredType = firstType;
            }

            elements.push_back(std::move(firstElem));

            while (match({TokenTypes::Comma})) {
                auto nextElem = parseExpression();
                std::string nextType = inferNodeType(nextElem.get());

                if (!nextType.empty() && !inferredType.empty() && nextType != inferredType) {
                    error_reporter.emitError(
                        peek().line, peek().column,
                        "Type Mismatch in Array Literal",
                        "Array element type mismatch: Expected '" + inferredType + "', but found '" + nextType + "'.",
                        "Ensure all elements inside the array literal match the same type."
                    );
                }

                elements.push_back(std::move(nextElem));
            }
        }

        consume(TokenTypes::RightBracket, "Syntax Error: Missing ']'",
                "Expected ']' at end of array literal expression.");

        return std::make_unique<ArrayLiteralNode>(inferredType, std::move(elements), bracketTok.line, bracketTok.column);
    }

    std::unique_ptr<ASTNode> parsePrimary() {
        std::unique_ptr<ASTNode> expr = nullptr;

        if (match({TokenTypes::Integer})) {
            Token tok = previous();
            expr = std::make_unique<NumberNode>(tok.value, false, tok.line, tok.column);
        } else if (match({TokenTypes::Float})) {
            Token tok = previous();
            expr = std::make_unique<NumberNode>(tok.value, true, tok.line, tok.column);
        } else if (match({TokenTypes::Character})) {
            Token tok = previous();
            expr = std::make_unique<CharNode>(static_cast<char>(tok.value.c_str()[0]), tok.line, tok.column);
        } else if (match({TokenTypes::Charray})) {
            Token tok = previous();
            expr = std::make_unique<CharrayNode>(tok.value, tok.line, tok.column);
        } else if (match({TokenTypes::True})) {
            Token tok = previous();
            expr = std::make_unique<NumberNode>("1", false, tok.line, tok.column);
        } else if (match({TokenTypes::False})) {
            Token tok = previous();
            expr = std::make_unique<NumberNode>("0", false, tok.line, tok.column);
        } else if (check(TokenTypes::Identifier)) {
            expr = parseIdentifier();
        } else if (match({TokenTypes::Null})) { // FIX: Changed from check() to match()
            Token tok = previous();
            expr = std::make_unique<NullNode>(tok.line, tok.column);
        } else if (match({TokenTypes::LeftBracket})) {
            expr = parseArrayLiteral();
        } else if (match({TokenTypes::LeftParen})) {
            expr = parseExpression();
            consume(TokenTypes::RightParen, "Syntax Error: Missing ')'",
                    "Expected ')' after enclosed sub-expression.");
        } else {
            Token errTok = peek();
            error_reporter.emitError(
                errTok.line, errTok.column,
                "Syntax Error: Unexpected Token",
                "Unexpected token '" + (errTok.value.empty() ? tokenTypeToString(errTok.type) : errTok.value) + "' encountered in expression.",
                "Check for missing operators, operands, or unmatched parentheses."
            );
            throw std::runtime_error("Unexpected token in primary expression.");
        }

        // Postfix operator loop: Allows member access (.) and indexing ([]) on ANY primary expression!
        while (check(TokenTypes::LeftBracket) || check(TokenTypes::Dot)) {
            if (match({TokenTypes::LeftBracket})) {
                Token bracketTok = previous();
                auto indexExpr = parseExpression();

                consume(TokenTypes::RightBracket, "Syntax Error: Missing ']'",
                        "Expected ']' after array index expression.",
                        "Example format: 'arr[0]'.");

                expr = std::make_unique<IndexNode>(
                    std::move(expr),
                    std::move(indexExpr),
                    bracketTok.line,
                    bracketTok.column
                );
            } else if (match({TokenTypes::Dot})) {
                Token dotTok = previous();
                Token fieldToken = consume(
                    TokenTypes::Identifier,
                    "Syntax Error: Invalid field access",
                    "Expected field name after '.'.",
                    "Example format: 'object.property'."
                );

                expr = std::make_unique<MemberAccessNode>(
                    std::move(expr),
                    fieldToken.value,
                    dotTok.line,
                    dotTok.column
                );
            }
        }

        return expr;
    }
};

inline void printAST(const ASTNode* node, int depth = 0) {
    if (!node) return;
    std::string indent(depth * 2, ' ');
    std::string pos = " [" + std::to_string(node->line) + ":" + std::to_string(node->column) + "]";

    if (auto num = dynamic_cast<const NumberNode*>(node)) {
        std::cout << indent << "Number(" << num->value << ")" << pos << "\n";
    } else if (auto ch = dynamic_cast<const CharNode*>(node)) {
        std::cout << indent << "Char(" << ch->value << ")" << pos << "\n";
    } else if (auto str = dynamic_cast<const CharrayNode*>(node)) {
        std::cout << indent << "Charray(" << str->value << ")" << pos << "\n";
    } else if (auto un = dynamic_cast<const UnaryOpNode*>(node)) {
        std::cout << indent << "UnaryOp(" << un->op << ")" << pos << "\n";
        printAST(un->operand.get(), depth + 1);
    } else if (auto fr = dynamic_cast<const FreeNode*>(node)) {
        std::cout << indent << "Free" << pos << "\n";
        printAST(fr->expression.get(), depth + 1);
    } else if (auto ret = dynamic_cast<const ReturnNode*>(node)) {
        std::cout << indent << "Return" << pos << "\n";
        printAST(ret->expression.get(), depth + 1);
    } else if (auto arrLit = dynamic_cast<const ArrayLiteralNode*>(node)) {
        std::cout << indent << "ArrayLiteral(" << arrLit->element_type << ")" << pos << "\n";
        for (const auto& elem : arrLit->elements) {
            printAST(elem.get(), depth + 1);
        }
    } else if (auto idx = dynamic_cast<const IndexNode*>(node)) {
        std::cout << indent << "Index" << pos << "\n";
        printAST(idx->array.get(), depth + 1);
        printAST(idx->index.get(), depth + 1);
    } else if (auto decl = dynamic_cast<const VarDeclNode*>(node)) {
        std::cout << indent << "VarDecl(" << decl->type << " " << decl->name << ")" << pos << "\n";
        if (decl->initializer) {
            printAST(decl->initializer.get(), depth + 1);
        }
    } else if (auto var = dynamic_cast<const VariableNode*>(node)) {
        std::cout << indent << "Variable(" << var->name << ")" << pos << "\n";
    } else if (auto bin = dynamic_cast<const BinaryOpNode*>(node)) {
        std::cout << indent << "BinaryOp(" << bin->op << ")" << pos << "\n";
        printAST(bin->left.get(), depth + 1);
        printAST(bin->right.get(), depth + 1);
    } else if (auto assign = dynamic_cast<const AssignmentNode*>(node)) {
        std::cout << indent << "Assignment" << pos << "\n";
        printAST(assign->target.get(), depth + 1);
        printAST(assign->expression.get(), depth + 1);
    } else if (auto memAcc = dynamic_cast<const MemberAccessNode*>(node)) {
        std::cout << indent << "MemberAccess(." << memAcc->field_name << ")" << pos << "\n";
        printAST(memAcc->base.get(), depth + 1);
    } else if (auto memAssign = dynamic_cast<const MemberAssignNode*>(node)) {
        std::cout << indent << "MemberAssign(" << memAssign->struct_var_name << "." << memAssign->field_name << ")" << pos << "\n";
        printAST(memAssign->value.get(), depth + 1);
    } else if (auto structDef = dynamic_cast<const StructDefNode*>(node)) {
        std::cout << indent << "StructDef(" << structDef->name << " { ";
        for (size_t i = 0; i < structDef->fields.size(); ++i) {
            std::cout << structDef->fields[i].name << ": " << structDef->fields[i].type
                      << (i + 1 < structDef->fields.size() ? ", " : "");
        }
        std::cout << " })" << pos << "\n";
    } else if (auto hdr = dynamic_cast<const HeaderImportNode*>(node)) {
        std::cout << indent << "HeaderImport(" << hdr->header_path << ")" << pos << "\n";
    } else if (auto fn = dynamic_cast<const FunctionNode*>(node)) {
        std::cout << indent << "FunctionDecl(" << fn->name << "(";
        for (size_t i = 0; i < fn->params.size(); ++i) {
            std::cout << fn->params[i].type << " " << fn->params[i].name << (i + 1 < fn->params.size() ? ", " : "");
        }
        std::cout << "): " << fn->return_type << ")" << pos << "\n";
        printAST(fn->body.get(), depth + 1);
    } else if (auto call = dynamic_cast<const CallNode*>(node)) {
        std::cout << indent << "Call(" << call->name << ")" << pos << "\n";
        for (const auto& arg : call->arguments) {
            printAST(arg.get(), depth + 1);
        }
    } else if (auto whileLoop = dynamic_cast<const WhileNode*>(node)) {
        std::cout << indent << "WhileLoop" << pos << "\n";
        if (whileLoop->init)      printAST(whileLoop->init.get(), depth + 1);
        if (whileLoop->condition) printAST(whileLoop->condition.get(), depth + 1);
        if (whileLoop->update)    printAST(whileLoop->update.get(), depth + 1);
        if (whileLoop->body)      printAST(whileLoop->body.get(), depth + 1);
    } else if (auto cond = dynamic_cast<const ConditionNode*>(node)) {
        std::cout << indent << "Condition" << pos << "\n";
        for (const auto& branch : cond->branches) {
            std::cout << indent << "  BranchIf\n";
            printAST(branch.condition.get(), depth + 2);
            printAST(branch.body.get(), depth + 2);
        }
        if (cond->else_body) {
            std::cout << indent << "  Else\n";
            printAST(cond->else_body.get(), depth + 2);
        }
    } else if (auto block = dynamic_cast<const BlockNode*>(node)) {
        std::cout << indent << "Block" << pos << "\n";
        for (const auto& stmt : block->statements) {
            printAST(stmt.get(), depth + 1);
        }
    } else if (auto prog = dynamic_cast<const ProgramNode*>(node)) {
        std::cout << indent << "Program" << pos << "\n";
        for (const auto& stmt : prog->statements) {
            printAST(stmt.get(), depth + 1);
        }
    } else if (auto ext = dynamic_cast<const ExternNode*>(node)) {
        std::cout << indent << "ExternDecl(" << ext->name << "(";
        for (size_t i = 0; i < ext->params.size(); ++i) {
            std::cout << ext->params[i].type << " " << ext->params[i].name << (i + 1 < ext->params.size() ? ", " : "");
        }
        std::cout << "): " << ext->return_type << ")" << pos << "\n";
    }
}

#endif // COMPILER_PARSER_H