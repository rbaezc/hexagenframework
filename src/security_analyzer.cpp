#include "security_analyzer.hpp"
#include <algorithm>
#include <stdexcept>
#include <iostream>

void SecurityAnalyzer::checkObfuscation(const std::vector<Token>& tokens) {
    // Rule 3: Detect strings too long without spaces (potential base64/hex payload)
    for (const auto& token : tokens) {
        if (token.type == TokenType::STRING_LITERAL) {
            if (token.value.length() > 200) {
                // If it doesn't contain space characters, it's highly suspicious
                if (token.value.find(' ') == std::string::npos) {
                    throw std::runtime_error("Security Error: Potential code obfuscation detected at line " +
                                             std::to_string(token.line) + ": String literal too long without spaces (>200 chars).");
                }
            }
        }
    }

    // Rule 3: Detect giant arrays of bytes/integers (potential shellcodes)
    int consecutiveInts = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::INT_LITERAL) {
            consecutiveInts++;
            if (consecutiveInts > 50) {
                throw std::runtime_error("Security Error: Giant byte/integer array detected at line " +
                                         std::to_string(tokens[i].line) + ". Obfuscated binary payload prohibited.");
            }
            if (i + 1 < tokens.size() && tokens[i + 1].type == TokenType::COMMA) {
                i++; // Skip comma separator
            } else {
                consecutiveInts = 0;
            }
        } else {
            consecutiveInts = 0;
        }
    }
}

void SecurityAnalyzer::checkSandboxing(const std::vector<Token>& tokens) {
    // Sandboxing: Forbid raw pointer operations, raw socket operations, or forbidden C++ standard library/system components
    std::set<std::string> forbiddenIdentifiers = {
        "malloc", "free", "realloc", "calloc", 
        "reinterpret_cast", "static_cast", "const_cast", "dynamic_cast",
        "socket", "connect", "bind", "accept", "recv",
        "mmap", "dlopen", "dlsym", "pthread_create",
        "fork", "execve", "execl", "execvp"
    };

    for (const auto& token : tokens) {
        if (token.type == TokenType::IDENTIFIER) {
            std::string nameLower = token.value;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (forbiddenIdentifiers.count(nameLower)) {
                throw std::runtime_error("Security Error: Direct system or memory access is prohibited (Sandboxing violation) at line " +
                                         std::to_string(token.line) + ": forbidden identifier '" + token.value + "' used.");
            }
        }
    }
}

void SecurityAnalyzer::analyzeAST() {
    for (const auto& slice : program->slices) {
        for (const auto& action : slice->actions) {
            std::set<std::string> taintedVars;
            for (const auto& stmt : action->statements) {
                analyzeStatement(stmt, slice->name, taintedVars);
            }
        }
    }
}

void SecurityAnalyzer::analyzeStatement(const std::shared_ptr<ASTStatement>& stmt, const std::string& sliceName, std::set<std::string>& taintedVars) {
    if (!stmt) return;

    if (auto assignStmt = std::dynamic_pointer_cast<ASTAssignmentStatement>(stmt)) {
        if (isExpressionTainted(assignStmt->expression, taintedVars)) {
            taintedVars.insert(assignStmt->variableName);
        } else {
            taintedVars.erase(assignStmt->variableName);
        }
    } 
    else if (auto ifStmt = std::dynamic_pointer_cast<ASTIfStatement>(stmt)) {
        std::set<std::string> thenTainted = taintedVars;
        std::set<std::string> elseTainted = taintedVars;
        for (const auto& s : ifStmt->thenBranch) {
            analyzeStatement(s, sliceName, thenTainted);
        }
        for (const auto& s : ifStmt->elseBranch) {
            analyzeStatement(s, sliceName, elseTainted);
        }
        // Union of taint sets from branches
        taintedVars.insert(thenTainted.begin(), thenTainted.end());
        taintedVars.insert(elseTainted.begin(), elseTainted.end());
    } 
    else if (auto whileStmt = std::dynamic_pointer_cast<ASTWhileStatement>(stmt)) {
        for (const auto& s : whileStmt->body) {
            analyzeStatement(s, sliceName, taintedVars);
        }
    } 
    else if (auto printStmt = std::dynamic_pointer_cast<ASTPrintStatement>(stmt)) {
        if (isExpressionTainted(printStmt->expression, taintedVars)) {
            throw std::runtime_error("Security Error: Potential leak of sensitive data detected in print statement in slice " + sliceName);
        }
    } 
    else if (auto callStmt = std::dynamic_pointer_cast<ASTCallStatement>(stmt)) {
        std::string actLower = callStmt->actionName;
        std::transform(actLower.begin(), actLower.end(), actLower.begin(), ::tolower);

        // Rule 1: Command Injection
        if (actLower == "system" || actLower == "popen" || actLower == "exec" || actLower == "spawn") {
            for (const auto& arg : callStmt->arguments) {
                if (hasVariables(arg)) {
                    throw std::runtime_error("Security Error: Potential Command Injection detected in slice " +
                                             sliceName + ": variable argument passed to system execution function '" + callStmt->actionName + "'. Only static strings are permitted.");
                }
            }
        }

        // Rule 2: Exfiltration of Credentials / Env Tokens
        // Forbid passing sensitive config variables/tainted variables to network/exfiltration calls
        std::set<std::string> exfiltrationTargets = {
            "fetch", "curl", "send", "write", "exfiltrate", "http_post", "http_get", "sendwebsocketframe", "broadcast_ws_message"
        };
        if (exfiltrationTargets.count(actLower)) {
            for (const auto& arg : callStmt->arguments) {
                if (isExpressionTainted(arg, taintedVars)) {
                    throw std::runtime_error("Security Error: Potential exfiltration of sensitive credentials detected in slice " +
                                             sliceName + ": tainted variable passed to network function '" + callStmt->actionName + "'.");
                }
            }
        }
    }
}

bool SecurityAnalyzer::isExpressionTainted(const std::shared_ptr<ASTExpression>& expr, const std::set<std::string>& taintedVars) {
    if (!expr) return false;
    
    if (auto id = std::dynamic_pointer_cast<ASTIdentifier>(expr)) {
        if (taintedVars.count(id->name)) return true;
        
        std::string nameLower = id->name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.find("pass") != std::string::npos ||
            nameLower.find("secret") != std::string::npos ||
            nameLower.find("key") != std::string::npos ||
            nameLower.find("token") != std::string::npos ||
            nameLower.find("auth") != std::string::npos ||
            nameLower.find("cred") != std::string::npos) {
            return true;
        }
        return false;
    }
    
    if (auto binary = std::dynamic_pointer_cast<ASTBinaryExpression>(expr)) {
        return isExpressionTainted(binary->left, taintedVars) || isExpressionTainted(binary->right, taintedVars);
    }
    
    return false;
}

bool SecurityAnalyzer::hasVariables(const std::shared_ptr<ASTExpression>& expr) {
    if (!expr) return false;
    
    if (std::dynamic_pointer_cast<ASTIdentifier>(expr)) {
        return true;
    }
    
    if (auto binary = std::dynamic_pointer_cast<ASTBinaryExpression>(expr)) {
        return hasVariables(binary->left) || hasVariables(binary->right);
    }
    
    return false;
}
