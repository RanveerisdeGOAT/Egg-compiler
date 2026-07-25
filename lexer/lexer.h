#ifndef COMPILER_LEXER_H
#define COMPILER_LEXER_H

#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <unordered_map>
#include <algorithm>

enum class TokenTypes {
    // Literals
    Integer,
    Float,
    Identifier,
    String,
    Charray,
    Character,
    True,
    False,
    Null,

    // Keywords
    If,
    Else,
    While,
    For,
    Return,
    Func,
    Var,
    Free,
    Extern,
    Struct,

    Type,

    // Single-character Operators
    Plus,
    Minus,
    Star,
    Slash,
    Equals,
    Bang,
    Less,
    Greater,
    LeftParen,
    RightParen,
    RightBrace,
    LeftBrace,
    RightBracket,
    LeftBracket,
    Comma,
    And,
    Or,
    Xor,
    Dollar,
    HashTag,
    Semicolon,
    Colon,
    Question,
    Percent,
    At,
    Dot,

    // Multi-character / Boolean Operators
    EqualEqual,   // ==
    BangEqual,    // !=
    LessEqual,    // <=
    GreaterEqual, // >=
    RightArrow,   // ->
    Increment,
    Decrement,
    Multiply,
    Divide,
    PercentGreater,
    DollarGreater,
    Ellipsis,

    // Structural
    EndOfFile,
    NewLine,
    Tab,
    Unknown,
};

inline std::string tokenTypeToString(TokenTypes type) {
    switch (type) {
        case TokenTypes::Integer:        return "INTEGER";
        case TokenTypes::Float:          return "FLOAT";
        case TokenTypes::Identifier:     return "IDENTIFIER";
        case TokenTypes::String:         return "STRING";
        case TokenTypes::Charray:        return "CHARRAY";
        case TokenTypes::Character:      return "CHARACTER";
        case TokenTypes::Type:           return "TYPE";
        case TokenTypes::True:           return "TRUE";
        case TokenTypes::False:          return "FALSE";
        case TokenTypes::If:             return "IF";
        case TokenTypes::Else:           return "ELSE";
        case TokenTypes::While:          return "WHILE";
        case TokenTypes::For:            return "FOR";
        case TokenTypes::Return:         return "RETURN";
        case TokenTypes::Func:           return "FUNC";
        case TokenTypes::Var:            return "VAR";
        case TokenTypes::Free:           return "FREE";
        case TokenTypes::Null:           return "NULL";
        case TokenTypes::Extern:         return "EXTERN";
        case TokenTypes::Plus:           return "PLUS";
        case TokenTypes::Minus:          return "MINUS";
        case TokenTypes::Star:           return "STAR";
        case TokenTypes::Slash:          return "SLASH";
        case TokenTypes::Equals:         return "EQUALS";
        case TokenTypes::Bang:           return "BANG";
        case TokenTypes::Increment:      return "INCREMENT";
        case TokenTypes::Decrement:      return "DECREMENT";
        case TokenTypes::Multiply:       return "MULTIPLY";
        case TokenTypes::Divide:         return "DIVIDE";
        case TokenTypes::Less:           return "LESS";
        case TokenTypes::Greater:        return "GREATER";
        case TokenTypes::Comma:          return "COMMA";
        case TokenTypes::And:            return "AND";
        case TokenTypes::Percent:        return "PERCENT";
        case TokenTypes::Or:             return "OR";
        case TokenTypes::Xor:            return "XOR";
        case TokenTypes::Dot:            return "DOT";
        case TokenTypes::At:             return "AT";
        case TokenTypes::Dollar:         return "DOLLAR";
        case TokenTypes::HashTag:        return "HASHTAG";
        case TokenTypes::Semicolon:      return "SEMICOLON";
        case TokenTypes::Question:       return "QUESTION";
        case TokenTypes::EqualEqual:     return "EQUAL_EQUAL";
        case TokenTypes::BangEqual:      return "BANG_EQUAL";
        case TokenTypes::LessEqual:      return "LESS_EQUAL";
        case TokenTypes::GreaterEqual:   return "GREATER_EQUAL";
        case TokenTypes::PercentGreater: return "PERCENT_GREATER";
        case TokenTypes::DollarGreater:  return "DOLLAR_GREATER";
        case TokenTypes::RightArrow:     return "RIGHT_ARROW";
        case TokenTypes::Ellipsis:       return "ELLIPSIS";
        case TokenTypes::LeftParen:      return "LPAREN";
        case TokenTypes::RightParen:     return "RPAREN";
        case TokenTypes::LeftBrace:      return "LBRACE";
        case TokenTypes::LeftBracket:    return "LBRACKET";
        case TokenTypes::RightBrace:     return "RBRACE";
        case TokenTypes::RightBracket:   return "RBRACKET";
        case TokenTypes::Colon:          return "COLON";
        case TokenTypes::EndOfFile:      return "EOF";
        case TokenTypes::NewLine:        return "NEWLINE";
        case TokenTypes::Tab:            return "TAB";
        case TokenTypes::Struct:         return "STRUCT";
        default:                         return "UNKNOWN";
    }
}

struct Token {
    TokenTypes type;
    std::string value;
    size_t line;
    size_t column;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;

        while (!isAtEnd()) {
            char c = peek();
            size_t startCol = column_; // Capture token starting column

            if (c == '\n') {
                tokens.push_back({TokenTypes::NewLine, "\n", line_, startCol});
                advance(); // Handles line_++ and column_ = 1
                continue;
            }

            if (std::isspace(c)) {
                advance();
                continue;
            }

            if (c == '/' && peekNext() == '/') {
                while (!isAtEnd() && peek() != '\n') {
                    advance();
                }
                continue;
            }

            if (c == '"') {
                tokens.push_back(readString());
                continue;
            }

            if (peek() == '\'') {
                advance(); // Consume opening '
                char ch = advance(); // Read character
                advance(); // Consume closing '
                tokens.push_back({TokenTypes::Character, std::string(1, ch), line_, startCol});
                continue;
            }

            if (std::isdigit(c)) {
                tokens.push_back(readNumber());
                continue;
            }

            if (std::isalpha(c) || c == '_') {
                tokens.push_back(readIdentifierTypeOrKeyword());
                continue;
            }

            switch (c) {
                case '=':
                    advance();
                    if (match('=')) tokens.push_back({TokenTypes::EqualEqual, "==", line_, startCol});
                    else tokens.push_back({TokenTypes::Equals, "=", line_, startCol});
                    break;
                case '!':
                    advance();
                    if (match('=')) tokens.push_back({TokenTypes::BangEqual, "!=", line_, startCol});
                    else tokens.push_back({TokenTypes::Bang, "!", line_, startCol});
                    break;
                case '<':
                    advance();
                    if (match('=')) tokens.push_back({TokenTypes::LessEqual, "<=", line_, startCol});
                    else tokens.push_back({TokenTypes::Less, "<", line_, startCol});
                    break;
                case '>':
                    advance();
                    if (match('=')) tokens.push_back({TokenTypes::GreaterEqual, ">=", line_, startCol});
                    else tokens.push_back({TokenTypes::Greater, ">", line_, startCol});
                    break;
                case '-':
                    advance();
                    if (match('>')) tokens.push_back({TokenTypes::RightArrow, "->", line_, startCol});
                    else if (match('=')) tokens.push_back({TokenTypes::Decrement, "-=", line_, startCol});
                    else tokens.push_back({TokenTypes::Minus, "-", line_, startCol});
                    break;
                case '+':
                    advance();
                    if (match('=')) tokens.push_back({TokenTypes::Increment, "+=", line_, startCol});
                    else tokens.push_back({TokenTypes::Plus, "+", line_, startCol});
                    break;
                case '*':
                    advance();
                    if (match('=')) tokens.push_back({TokenTypes::Multiply, "*=", line_, startCol});
                    else tokens.push_back({TokenTypes::Star, "*", line_, startCol});
                    break;
                case '/':
                    advance();
                    if (match('=')) tokens.push_back({TokenTypes::Divide, "/=", line_, startCol});
                    else tokens.push_back({TokenTypes::Slash, "/", line_, startCol});
                    break;
                case '$':
                    advance();
                    if (match('>')) tokens.push_back({TokenTypes::DollarGreater, "$>", line_, startCol});
                    else tokens.push_back({TokenTypes::Dollar, "$", line_, startCol});
                    break;
                case '%':
                    advance();
                    if (match('>')) tokens.push_back({TokenTypes::PercentGreater, "%>", line_, startCol});
                    else tokens.push_back({TokenTypes::Percent, "%", line_, startCol});
                    break;
                case '.':
                    advance();
                    if (match('.')) {
                        if (match('.')) tokens.push_back({TokenTypes::Ellipsis, "...", line_, startCol});
                    }
                    else tokens.push_back({TokenTypes::Dot, ".", line_, startCol});
                    break;
                case '(': tokens.push_back({TokenTypes::LeftParen, "(", line_, startCol}); advance(); break;
                case ')': tokens.push_back({TokenTypes::RightParen, ")", line_, startCol}); advance(); break;
                case '{': tokens.push_back({TokenTypes::LeftBrace, "{", line_, startCol}); advance(); break;
                case '}': tokens.push_back({TokenTypes::RightBrace, "}", line_, startCol}); advance(); break;
                case ']': tokens.push_back({TokenTypes::RightBracket, "]", line_, startCol}); advance(); break;
                case '[': tokens.push_back({TokenTypes::LeftBracket, "[", line_, startCol}); advance(); break;
                case ',': tokens.push_back({TokenTypes::Comma, ",", line_, startCol}); advance(); break;
                case '&': tokens.push_back({TokenTypes::And, "&", line_, startCol}); advance(); break;
                case '|': tokens.push_back({TokenTypes::Or, "|", line_, startCol}); advance(); break;
                case '^': tokens.push_back({TokenTypes::Xor, "^", line_, startCol}); advance(); break;
                case '#': tokens.push_back({TokenTypes::HashTag, "#", line_, startCol}); advance(); break;
                case ';': tokens.push_back({TokenTypes::Semicolon, ";", line_, startCol}); advance(); break;
                case ':': tokens.push_back({TokenTypes::Colon, ":", line_, startCol}); advance(); break;
                case '?': tokens.push_back({TokenTypes::Question, "?", line_, startCol}); advance(); break;
                case '@': tokens.push_back({TokenTypes::At, "@", line_, startCol}); advance(); break;
                default:
                    tokens.push_back({TokenTypes::Unknown, std::string(1, c), line_, startCol});
                    advance();
                    break;
            }
        }

        tokens.push_back({TokenTypes::EndOfFile, "", line_, column_});
        return tokens;
    }

private:
    std::string source_;
    size_t cursor_ = 0;
    size_t line_ = 1;
    size_t column_ = 1;

    const std::unordered_map<std::string, TokenTypes> keywords_ = {
        {"if", TokenTypes::If},
        {"else", TokenTypes::Else},
        {"while", TokenTypes::While},
        {"for", TokenTypes::For},
        {"return", TokenTypes::Return},
        {"true", TokenTypes::True},
        {"false", TokenTypes::False},
        {"func", TokenTypes::Func},
        {"var", TokenTypes::Var},
        {"free", TokenTypes::Free},
        {"extern", TokenTypes::Extern},
        {"struct", TokenTypes::Struct},
        {"null", TokenTypes::Null}
    };

    const std::vector<std::string> types_ = {"int", "long", "float", "double", "bool", "string", "charray", "char", "void"};

    bool isAtEnd() const { return cursor_ >= source_.length(); }
    char peek() const { return isAtEnd() ? '\0' : source_[cursor_]; }
    char peekNext() const { return cursor_ + 1 >= source_.length() ? '\0' : source_[cursor_ + 1]; }

    char advance() {
        if (isAtEnd()) return '\0';
        char c = source_[cursor_++];
        if (c == '\n') {
            line_++;
            column_ = 1;
        } else {
            column_++;
        }
        return c;
    }

    bool match(char expected) {
        if (isAtEnd() || source_[cursor_] != expected) return false;
        advance();
        return true;
    }

    Token readString() {
        size_t startCol = column_;
        advance(); // Consume opening quote
        std::string value;

        while (!isAtEnd() && peek() != '"') {
            if (peek() == '\\' && !isAtEnd()) {
                advance();
                switch (peek()) {
                    case 'n':  value += '\n'; break;
                    case 't':  value += '\t'; break;
                    case '"':  value += '"';  break;
                    case '\\': value += '\\'; break;
                    default:   value += peek(); break;
                }
            } else {
                value += peek();
            }
            advance();
        }

        if (isAtEnd()) return {TokenTypes::Unknown, "Unterminated String", line_, startCol};

        advance(); // Consume closing quote
        return {TokenTypes::Charray, value, line_, startCol};
    }

    Token readNumber() {
        size_t startPos = cursor_;
        size_t startCol = column_;
        bool is_float = false;

        while (!isAtEnd() && std::isdigit(peek())) advance();

        if (peek() == '.' && std::isdigit(peekNext())) {
            is_float = true;
            advance();
            while (!isAtEnd() && std::isdigit(peek())) advance();
        }

        return {
            is_float ? TokenTypes::Float : TokenTypes::Integer,
            source_.substr(startPos, cursor_ - startPos),
            line_,
            startCol
        };
    }

    Token readIdentifierTypeOrKeyword() {
        size_t startPos = cursor_;
        size_t startCol = column_;

        while (!isAtEnd() && (std::isalnum(peek()) || peek() == '_')) {
            advance();
        }

        std::string text = source_.substr(startPos, cursor_ - startPos);
        auto it = keywords_.find(text);
        if (it != keywords_.end()) {
            return {it->second, text, line_, startCol};
        }

        auto type = std::find(types_.begin(), types_.end(), text);
        if (type != types_.end()) {
            return {TokenTypes::Type, text, line_, startCol};
        }

        return {TokenTypes::Identifier, text, line_, startCol};
    }
};

#endif // COMPILER_LEXER_H