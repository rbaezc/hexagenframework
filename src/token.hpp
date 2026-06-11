#pragma once
#include <string>

enum class TokenType {
    SLICE,
    FIELD,
    ACTION,
    PRINT,
    IDENTIFIER,
    INT_LITERAL,
    STRING_LITERAL,
    COLON,
    LBRACE,
    RBRACE,
    LPAREN,
    RPAREN,
    EQUALS,
    
    // UI View Keywords
    VIEW,
    TITLE,
    INPUT,
    BUTTON,
    TABLE, // "table"
    
    // API Routing Keywords
    API,
    ROUTE,
    SECURE, // "secure"
    WEBSOCKET, // "websocket"
    ARROW, // "->"
    HTTP_GET,
    HTTP_POST,
    HTTP_DELETE,
    DOT, // "."
    COMMA, // ","
    
    // Operators
    PLUS,
    MINUS,
    STAR,
    SLASH,
    EQ_EQ,
    BANG_EQ,
    LT,
    GT,
    
    // Keywords
    IF,
    ELSE,
    WHILE,
    CONFIG, // "config"

    // Types
    TYPE_INT,
    TYPE_STRING,
    TYPE_FLOAT,
    TYPE_BOOL,
    
    END_OF_FILE,
    UNKNOWN
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
};

inline std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::SLICE: return "slice";
        case TokenType::FIELD: return "field";
        case TokenType::ACTION: return "action";
        case TokenType::PRINT: return "print";
        case TokenType::IDENTIFIER: return "identifier";
        case TokenType::INT_LITERAL: return "integer literal";
        case TokenType::STRING_LITERAL: return "string literal";
        case TokenType::COLON: return ":";
        case TokenType::LBRACE: return "{";
        case TokenType::RBRACE: return "}";
        case TokenType::LPAREN: return "(";
        case TokenType::RPAREN: return ")";
        case TokenType::EQUALS: return "=";
        case TokenType::VIEW: return "view";
        case TokenType::TITLE: return "title";
        case TokenType::INPUT: return "input";
        case TokenType::BUTTON: return "button";
        case TokenType::TABLE: return "table";
        case TokenType::API: return "api";
        case TokenType::ROUTE: return "route";
        case TokenType::SECURE: return "secure";
        case TokenType::WEBSOCKET: return "websocket";
        case TokenType::ARROW: return "->";
        case TokenType::HTTP_GET: return "GET";
        case TokenType::HTTP_POST: return "POST";
        case TokenType::HTTP_DELETE: return "DELETE";
        case TokenType::DOT: return ".";
        case TokenType::COMMA: return ",";
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::STAR: return "*";
        case TokenType::SLASH: return "/";
        case TokenType::EQ_EQ: return "==";
        case TokenType::BANG_EQ: return "!=";
        case TokenType::LT: return "<";
        case TokenType::GT: return ">";
        case TokenType::IF: return "if";
        case TokenType::ELSE: return "else";
        case TokenType::WHILE: return "while";
        case TokenType::CONFIG: return "config";
        case TokenType::TYPE_INT: return "int";
        case TokenType::TYPE_STRING: return "string";
        case TokenType::TYPE_FLOAT: return "float";
        case TokenType::TYPE_BOOL: return "bool";
        case TokenType::END_OF_FILE: return "EOF";
        default: return "unknown";
    }
}
