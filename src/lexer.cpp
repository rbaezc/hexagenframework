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
                // Preserve escape sequences verbatim (e.g. \" \' \\ \n \t) so the
                // closing quote is not detected prematurely and the generated C++
                // string literal stays valid.
                if (peek() == '\\') {
                    strVal += advance();          // copy the backslash
                    if (peek() != '\0') {
                        strVal += advance();      // copy the escaped char (incl. a quote)
                    }
                    continue;
                }
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
            else if (ident == "import") type = TokenType::IMPORT;
            else if (ident == "int") type = TokenType::TYPE_INT;
            else if (ident == "string") type = TokenType::TYPE_STRING;
            else if (ident == "float") type = TokenType::TYPE_FLOAT;
            else if (ident == "bool") type = TokenType::TYPE_BOOL;
            else if (ident == "view") type = TokenType::VIEW;
            else if (ident == "title") type = TokenType::TITLE;
            else if (ident == "input") type = TokenType::INPUT;
            else if (ident == "button") type = TokenType::BUTTON;
            else if (ident == "table") type = TokenType::TABLE;
            else if (ident == "html") type = TokenType::HTML;
            else if (ident == "api") type = TokenType::API;
            else if (ident == "route") type = TokenType::ROUTE;
            else if (ident == "secure") type = TokenType::SECURE;
            else if (ident == "websocket") type = TokenType::WEBSOCKET;
            else if (ident == "GET") type = TokenType::HTTP_GET;
            else if (ident == "POST") type = TokenType::HTTP_POST;
            else if (ident == "DELETE" || ident == "delete") type = TokenType::HTTP_DELETE;
            else if (ident == "job") type = TokenType::JOB;
            else if (ident == "enqueue") type = TokenType::ENQUEUE;
            else if (ident == "use") type = TokenType::USE;
            else if (ident == "cpp") type = TokenType::CPP;
            else if (ident == "validate") type = TokenType::VALIDATE;

            tokens.push_back({type, ident, startLine, startCol});

            // cpp { ... } block: collect raw C++ as a STRING_LITERAL token so
            // the parser sees exactly what cpp "..." emits. Handles nested braces
            // and string literals inside the block.
            if (type == TokenType::CPP) {
                size_t p2 = pos;
                while (p2 < source.size() && (source[p2]==' '||source[p2]=='\t'||source[p2]=='\r'||source[p2]=='\n')) p2++;
                if (p2 < source.size() && source[p2] == '{') {
                    pos = p2 + 1; // consume {
                    std::string raw;
                    int depth = 1;
                    bool inStr = false; char strCh = 0;
                    while (pos < source.size() && depth > 0) {
                        char ch = source[pos++];
                        if (ch == '\n') { line++; column = 1; } else { column++; }
                        if (inStr) {
                            if (ch == '\\' && pos < source.size()) { raw += ch; raw += source[pos++]; continue; }
                            if (ch == strCh) inStr = false;
                            raw += ch;
                        } else {
                            if (ch == '"' || ch == '\'') { inStr = true; strCh = ch; raw += ch; }
                            else if (ch == '{') { depth++; raw += ch; }
                            else if (ch == '}') { depth--; if (depth > 0) raw += ch; }
                            else { raw += ch; }
                        }
                    }
                    tokens.push_back({TokenType::STRING_LITERAL, raw, startLine, startCol});
                }
            }

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
