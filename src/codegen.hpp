#pragma once
#include "ast.hpp"
#include <string>
#include <memory>

class CodeGenerator {
private:
    std::shared_ptr<ASTProgram> program;

    std::string generateExpression(std::shared_ptr<ASTExpression> expr);
    std::string generateStatement(std::shared_ptr<ASTStatement> stmt);
    std::string generateField(std::shared_ptr<ASTField> field);
    std::string generateActionImpl(const std::string& className, std::shared_ptr<ASTAction> action);
    std::string generateSlice(std::shared_ptr<ASTSlice> slice);
    std::string generateJob(std::shared_ptr<ASTJob> job);
    std::string generateHTMLContent(std::shared_ptr<ASTView> view);
    void generateReactFrontend();
    void generateReactPage(std::shared_ptr<ASTView> view);
    std::string generateAdminHTML();

public:
    CodeGenerator(std::shared_ptr<ASTProgram> program) : program(program) {}

    std::string generateSourceCode(bool includeMain = true);
};
