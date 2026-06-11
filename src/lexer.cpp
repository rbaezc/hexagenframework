#include "lexer.hpp"
#include <cctype>

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (pos < source.length()) {
        char c = peek();

        if (std::isspace(c)) {
            advance();
            continue;
        }

        // Comments
        if (c == '/' && peekNext() == '/') {
            advance(); // '/'
            advance(); // '/'
            while (peek() != '\n' && peek() != '\0') {
                advance();
            }
            continue;
        }

        if (c == '{') {
            tokens.push_back({TokenType::LBRACE, "{", line, column});
            advance();
            continue;
        }
        if (c == '}') {
            tokens.push_back({TokenType::RBRACE, "}", line, column});
            advance();
            continue;
        }
        if (c == '(') {
            tokens.push_back({TokenType::LPAREN, "(", line, column});
            advance();
            continue;
        }
        if (c == ')') {
            tokens.push_back({TokenType::RPAREN, ")", line, column});
            advance();
            continue;
        }
        if (c == ':') {
            tokens.push_back({TokenType::COLON, ":", line, column});
            advance();
            continue;
        }
        if (c == '.') {
            tokens.push_back({TokenType::DOT, ".", line, column});
            advance();
            continue;
        }
        if (c == ',') {
            tokens.push_back({TokenType::COMMA, ",", line, column});
            advance();
            continue;
        }
        if (c == '+') {
            tokens.push_back({TokenType::PLUS, "+", line, column});
            advance();
            continue;
        }
        if (c == '-') {
            if (peekNext() == '>') {
                tokens.push_back({TokenType::ARROW, "->", line, column});
                advance(); // '-'
                advance(); // '>'
            } else {
                tokens.push_back({TokenType::MINUS, "-", line, column});
                advance();
            }
            continue;
        }
        if (c == '*') {
            tokens.push_back({TokenType::STAR, "*", line, column});
            advance();
            continue;
        }
        if (c == '/') {
            tokens.push_back({TokenType::SLASH, "/", line, column});
            advance();
            continue;
        }
        if (c == '<') {
            tokens.push_back({TokenType::LT, "<", line, column});
            advance();
            continue;
        }
        if (c == '>') {
            tokens.push_back({TokenType::GT, ">", line, column});
            advance();
            continue;
        }
        if (c == '=') {
            if (peekNext() == '=') {
                tokens.push_back({TokenType::EQ_EQ, "==", line, column});
                advance(); // '='
                advance(); // '='
            } else {
                tokens.push_back({TokenType::EQUALS, "=", line, column});
                advance();
            }
            continue;
        }
        if (c == '!') {
            if (peekNext() == '=') {
                tokens.push_back({TokenType::BANG_EQ, "!=", line, column});
                advance(); // '!'
                advance(); // '='
                continue;
            }
        }

        // String Literals
        if (c == '"') {
            int startLine = line;
            int startCol = column;
            advance(); // consume open quote
            std::string strVal = "";
            while (peek() != '"' && peek() != '\0') {
                strVal += advance();
            }
            if (peek() == '"') {
                advance(); // consume close quote
                tokens.push_back({TokenType::STRING_LITERAL, strVal, startLine, startCol});
            } else {
                tokens.push_back({TokenType::UNKNOWN, strVal, startLine, startCol});
            }
            continue;
        }

        // Numeric Literals
        if (std::isdigit(c)) {
            int startLine = line;
            int startCol = column;
            std::string numVal = "";
            while (std::isdigit(peek())) {
                numVal += advance();
            }
            tokens.push_back({TokenType::INT_LITERAL, numVal, startLine, startCol});
            continue;
        }

        // Identifiers and Keywords
        if (std::isalpha(c) || c == '_') {
            int startLine = line;
            int startCol = column;
            std::string ident = "";
            while (std::isalnum(peek()) || peek() == '_') {
                ident += advance();
            }

            TokenType type = TokenType::IDENTIFIER;
            if (ident == "slice") type = TokenType::SLICE;
            else if (ident == "field") type = TokenType::FIELD;
            else if (ident == "action") type = TokenType::ACTION;
            else if (ident == "print") type = TokenType::PRINT;
            else if (ident == "if") type = TokenType::IF;
            else if (ident == "else") type = TokenType::ELSE;
            else if (ident == "while") type = TokenType::WHILE;
            else if (ident == "config") type = TokenType::CONFIG;
            else if (ident == "int") type = TokenType::TYPE_INT;
            else if (ident == "string") type = TokenType::TYPE_STRING;
            else if (ident == "float") type = TokenType::TYPE_FLOAT;
            else if (ident == "bool") type = TokenType::TYPE_BOOL;
            else if (ident == "view") type = TokenType::VIEW;
            else if (ident == "title") type = TokenType::TITLE;
            else if (ident == "input") type = TokenType::INPUT;
            else if (ident == "button") type = TokenType::BUTTON;
            else if (ident == "table") type = TokenType::TABLE;
            else if (ident == "api") type = TokenType::API;
            else if (ident == "route") type = TokenType::ROUTE;
            else if (ident == "secure") type = TokenType::SECURE;
            else if (ident == "websocket") type = TokenType::WEBSOCKET;
            else if (ident == "GET") type = TokenType::HTTP_GET;
            else if (ident == "POST") type = TokenType::HTTP_POST;
            else if (ident == "DELETE" || ident == "delete") type = TokenType::HTTP_DELETE;

            tokens.push_back({type, ident, startLine, startCol});
            continue;
        }

        // Unknown character
        std::string valStr(1, c);
        tokens.push_back({TokenType::UNKNOWN, valStr, line, column});
        advance();
    }

    tokens.push_back({TokenType::END_OF_FILE, "", line, column});
    return tokens;
}
