#pragma once
#include "token.hpp"
#include "ast.hpp"
#include <vector>
#include <memory>
#include <stdexcept>

class Parser {
private:
    std::vector<Token> tokens;
    size_t pos = 0;

    const Token& peek() const {
        if (pos >= tokens.size()) return tokens.back();
        return tokens[pos];
    }

    const Token& advance() {
        if (pos >= tokens.size()) return tokens.back();
        return tokens[pos++];
    }

    bool check(TokenType type) const {
        return peek().type == type;
    }

    bool match(TokenType type) {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }

    void consume(TokenType type, const std::string& errorMessage) {
        if (check(type)) {
            advance();
            return;
        }
        const auto& tok = peek();
        throw std::runtime_error(errorMessage + " at line " + std::to_string(tok.line) + ", column " + std::to_string(tok.column) + " (got: '" + tok.value + "')");
    }

    std::shared_ptr<ASTSlice> parseSlice();
    std::shared_ptr<ASTField> parseField();
    std::shared_ptr<ASTAction> parseAction();
    void parseValidateBlock(std::shared_ptr<ASTSlice> slice);
    std::shared_ptr<ASTStatement> parseStatement();
    
    // View and API Parsing
    std::shared_ptr<ASTView> parseView();
    std::shared_ptr<ASTApi> parseApi();
    std::shared_ptr<ASTJob> parseJob();
    std::shared_ptr<ASTMiddleware> parseMiddleware();
    void parseConfig(std::shared_ptr<ASTProgram> program);
    std::shared_ptr<ASTImport> parseImport();
    
    // Expression Parsing Helpers
    std::shared_ptr<ASTExpression> parseExpression();
    std::shared_ptr<ASTExpression> parseRelational();
    std::shared_ptr<ASTExpression> parseAdditive();
    std::shared_ptr<ASTExpression> parseMultiplicative();
    std::shared_ptr<ASTExpression> parsePrimary();

public:
    Parser(std::vector<Token> tokens) : tokens(tokens) {}

    std::shared_ptr<ASTProgram> parse();
};
