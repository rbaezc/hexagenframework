#pragma once
#include "ast.hpp"
#include "token.hpp"
#include <vector>
#include <string>
#include <set>
#include <memory>

class SecurityAnalyzer {
private:
    std::shared_ptr<ASTProgram> program;
    std::vector<Token> tokens;

    // Helper methods for AST traversal and analysis
    void analyzeStatement(const std::shared_ptr<ASTStatement>& stmt, const std::string& sliceName, std::set<std::string>& taintedVars);
    bool isExpressionTainted(const std::shared_ptr<ASTExpression>& expr, const std::set<std::string>& taintedVars);
    bool hasVariables(const std::shared_ptr<ASTExpression>& expr);

public:
    SecurityAnalyzer(std::shared_ptr<ASTProgram> prog, const std::vector<Token>& toks)
        : program(prog), tokens(toks) {}

    static void checkObfuscation(const std::vector<Token>& tokens);
    static void checkSandboxing(const std::vector<Token>& tokens);
    void analyzeAST();

    void analyze() {
        checkObfuscation(tokens);
        checkSandboxing(tokens);
        analyzeAST();
    }
};
