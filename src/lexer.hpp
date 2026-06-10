#pragma once
#include "token.hpp"
#include <string>
#include <vector>

class Lexer {
private:
    std::string source;
    size_t pos = 0;
    int line = 1;
    int column = 1;

    char peek() const {
        if (pos >= source.length()) return '\0';
        return source[pos];
    }

    char peekNext() const {
        if (pos + 1 >= source.length()) return '\0';
        return source[pos + 1];
    }

    char advance() {
        if (pos >= source.length()) return '\0';
        char c = source[pos++];
        if (c == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        return c;
    }

public:
    Lexer(std::string source) : source(source) {}

    std::vector<Token> tokenize();
};
