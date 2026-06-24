#include "parser.hpp"
#include <algorithm>
#include <cctype>

std::shared_ptr<ASTProgram> Parser::parse() {
    auto program = std::make_shared<ASTProgram>();
    while (!check(TokenType::END_OF_FILE)) {
        if (check(TokenType::SLICE)) {
            program->slices.push_back(parseSlice());
        } else if (check(TokenType::VIEW)) {
            program->views.push_back(parseView());
        } else if (check(TokenType::API)) {
            program->apis.push_back(parseApi());
        } else if (check(TokenType::CONFIG)) {
            parseConfig(program);
        } else if (check(TokenType::IMPORT)) {
            program->imports.push_back(parseImport());
        } else if (check(TokenType::JOB)) {
            program->jobs.push_back(parseJob());
        } else {
            const auto& tok = peek();
            throw std::runtime_error("Unexpected token '" + tok.value + "' at root level, line " + std::to_string(tok.line));
        }
    }

    if (program->views.size() > 1) {
        int targetIdx = -1;
        for (size_t i = 0; i < program->views.size(); ++i) {
            std::string nameLower = program->views[i]->name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower == "home" || nameLower == "index") {
                targetIdx = i;
                break;
            }
        }
        if (targetIdx > 0) {
            auto targetView = program->views[targetIdx];
            program->views.erase(program->views.begin() + targetIdx);
            program->views.insert(program->views.begin(), targetView);
        }
    }

    return program;
}

std::shared_ptr<ASTSlice> Parser::parseSlice() {
    consume(TokenType::SLICE, "Expected 'slice'");
    const auto& nameTok = peek();
    consume(TokenType::IDENTIFIER, "Expected slice identifier");
    std::string sliceName = nameTok.value;

    consume(TokenType::LBRACE, "Expected '{' to start slice body");

    auto slice = std::make_shared<ASTSlice>(sliceName);

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        if (check(TokenType::FIELD)) {
            slice->fields.push_back(parseField());
        } else if (check(TokenType::ACTION)) {
            slice->actions.push_back(parseAction());
        } else if (check(TokenType::VALIDATE)) {
            parseValidateBlock(slice);
        } else {
            const auto& tok = peek();
            throw std::runtime_error("Expected 'field', 'action' or 'validate' in slice definition, got: '" + tok.value + "' at line " + std::to_string(tok.line));
        }
    }

    consume(TokenType::RBRACE, "Expected '}' to close slice body");
    return slice;
}

std::shared_ptr<ASTField> Parser::parseField() {
    consume(TokenType::FIELD, "Expected 'field'");
    const auto& nameTok = peek();
    consume(TokenType::IDENTIFIER, "Expected field name");
    std::string fieldName = nameTok.value;

    consume(TokenType::COLON, "Expected ':' after field name");

    DataType type = DataType::UNKNOWN;
    std::string relatedSlice = "";
    const auto& typeTok = peek();
    if (match(TokenType::TYPE_INT)) type = DataType::INT;
    else if (match(TokenType::TYPE_STRING)) type = DataType::STRING;
    else if (match(TokenType::TYPE_FLOAT)) type = DataType::FLOAT;
    else if (match(TokenType::TYPE_BOOL)) type = DataType::BOOL;
    else if (check(TokenType::IDENTIFIER) && peek().value == "relation") {
        consume(TokenType::IDENTIFIER, "Expected 'relation'");
        consume(TokenType::LPAREN, "Expected '(' after 'relation'");
        const auto& relTok = peek();
        consume(TokenType::IDENTIFIER, "Expected related slice identifier");
        relatedSlice = relTok.value;
        consume(TokenType::RPAREN, "Expected ')' after related slice name");
        type = DataType::RELATION;
    } else {
        throw std::runtime_error("Unknown data type '" + typeTok.value + "' at line " + std::to_string(typeTok.line));
    }

    return std::make_shared<ASTField>(fieldName, type, relatedSlice);
}

std::shared_ptr<ASTAction> Parser::parseAction() {
    consume(TokenType::ACTION, "Expected 'action'");
    const auto& nameTok = peek();
    consume(TokenType::IDENTIFIER, "Expected action name");
    std::string actionName = nameTok.value;

    consume(TokenType::LPAREN, "Expected '(' after action name");
    consume(TokenType::RPAREN, "Expected ')' after action parameters");
    consume(TokenType::LBRACE, "Expected '{' to start action body");

    auto action = std::make_shared<ASTAction>(actionName);

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        action->statements.push_back(parseStatement());
    }

    consume(TokenType::RBRACE, "Expected '}' to close action body");
    return action;
}

void Parser::parseValidateBlock(std::shared_ptr<ASTSlice> slice) {
    consume(TokenType::VALIDATE, "Expected 'validate'");
    consume(TokenType::LBRACE, "Expected '{' to start validate block");

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        const auto& ruleTok = peek();
        consume(TokenType::IDENTIFIER, "Expected validation rule name (e.g. required, length, format, min, max)");
        std::string rule = ruleTok.value;

        consume(TokenType::LPAREN, "Expected '(' after validation rule name");
        std::vector<std::string> args;
        while (!check(TokenType::RPAREN) && !check(TokenType::END_OF_FILE)) {
            const auto& argTok = peek();
            if (check(TokenType::IDENTIFIER) || check(TokenType::INT_LITERAL) || check(TokenType::STRING_LITERAL)) {
                args.push_back(argTok.value);
                advance();
            } else {
                throw std::runtime_error("Unexpected token '" + argTok.value + "' in validation arguments at line " + std::to_string(argTok.line));
            }
            if (!match(TokenType::COMMA)) break;
        }
        consume(TokenType::RPAREN, "Expected ')' to close validation rule");

        slice->validations.push_back(std::make_shared<ASTValidation>(rule, args));
    }

    consume(TokenType::RBRACE, "Expected '}' to close validate block");
}

std::shared_ptr<ASTStatement> Parser::parseStatement() {
    if (match(TokenType::ENQUEUE)) {
        const auto& jobTok = peek();
        consume(TokenType::IDENTIFIER, "Expected job identifier after 'enqueue'");
        std::string jobName = jobTok.value;
        
        consume(TokenType::LPAREN, "Expected '(' after job name in enqueue statement");
        auto enqueue = std::make_shared<ASTEnqueueStatement>(jobName);
        
        if (!check(TokenType::RPAREN)) {
            while (!check(TokenType::RPAREN) && !check(TokenType::END_OF_FILE)) {
                const auto& fieldTok = peek();
                consume(TokenType::IDENTIFIER, "Expected field name in enqueue arguments");
                std::string fieldName = fieldTok.value;
                
                consume(TokenType::COLON, "Expected ':' after field name in enqueue argument");
                auto valExpr = parseExpression();
                
                enqueue->arguments.push_back({fieldName, valExpr});
                
                if (check(TokenType::COMMA)) {
                    advance();
                } else {
                    break;
                }
            }
        }
        consume(TokenType::RPAREN, "Expected ')' after enqueue arguments");
        return enqueue;
    } else if (match(TokenType::CPP)) {
        const auto& codeTok = peek();
        consume(TokenType::STRING_LITERAL, "Expected string literal containing C++ code after 'cpp'");
        return std::make_shared<ASTCppStatement>(codeTok.value);
    } else if (match(TokenType::PRINT)) {
        consume(TokenType::LPAREN, "Expected '(' after 'print'");
        auto expr = parseExpression();
        consume(TokenType::RPAREN, "Expected ')' after print expression");
        return std::make_shared<ASTPrintStatement>(expr);
    } else if (match(TokenType::IF)) {
        consume(TokenType::LPAREN, "Expected '(' after 'if'");
        auto cond = parseExpression();
        consume(TokenType::RPAREN, "Expected ')' after if condition");
        
        consume(TokenType::LBRACE, "Expected '{' to start if branch");
        auto ifStmt = std::make_shared<ASTIfStatement>(cond);
        while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
            ifStmt->thenBranch.push_back(parseStatement());
        }
        consume(TokenType::RBRACE, "Expected '}' to close if branch");
        
        if (match(TokenType::ELSE)) {
            consume(TokenType::LBRACE, "Expected '{' to start else branch");
            while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
                ifStmt->elseBranch.push_back(parseStatement());
            }
            consume(TokenType::RBRACE, "Expected '}' to close else branch");
        }
        
        return ifStmt;
    } else if (match(TokenType::WHILE)) {
        consume(TokenType::LPAREN, "Expected '(' after 'while'");
        auto cond = parseExpression();
        consume(TokenType::RPAREN, "Expected ')' after while condition");
        
        consume(TokenType::LBRACE, "Expected '{' to start while loop body");
        auto whileStmt = std::make_shared<ASTWhileStatement>(cond);
        while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
            whileStmt->body.push_back(parseStatement());
        }
        consume(TokenType::RBRACE, "Expected '}' to close while loop body");
        
        return whileStmt;
    } else if (check(TokenType::IDENTIFIER)) {
        const auto& nameTok = advance();
        std::string name = nameTok.value;
        while (match(TokenType::DOT)) {
            const auto& memberTok = peek();
            consume(TokenType::IDENTIFIER, "Expected identifier after '.'");
            name += "." + memberTok.value;
        }
        if (match(TokenType::EQUALS)) {
            auto expr = parseExpression();
            return std::make_shared<ASTAssignmentStatement>(name, expr);
        } else if (match(TokenType::LPAREN)) {
            std::vector<std::shared_ptr<ASTExpression>> args;
            if (!check(TokenType::RPAREN)) {
                args.push_back(parseExpression());
                while (match(TokenType::COMMA)) {
                    args.push_back(parseExpression());
                }
            }
            consume(TokenType::RPAREN, "Expected ')' after action call");
            return std::make_shared<ASTCallStatement>(name, args);
        } else {
            throw std::runtime_error("Expected '=' or '(' after identifier '" + name + "' at line " + std::to_string(nameTok.line));
        }
    } else {
        const auto& tok = peek();
        throw std::runtime_error("Unexpected token in statement: '" + tok.value + "' at line " + std::to_string(tok.line));
    }
    return nullptr;
}

std::shared_ptr<ASTView> Parser::parseView() {
    consume(TokenType::VIEW, "Expected 'view'");
    const auto& nameTok = peek();
    consume(TokenType::IDENTIFIER, "Expected view identifier");
    std::string viewName = nameTok.value;

    consume(TokenType::LBRACE, "Expected '{' to start view body");

    auto view = std::make_shared<ASTView>(viewName);

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        std::shared_ptr<ASTViewField> field = nullptr;
        if (match(TokenType::TITLE)) {
            const auto& strTok = peek();
            consume(TokenType::STRING_LITERAL, "Expected string literal after 'title'");
            field = std::make_shared<ASTViewField>("title", strTok.value, "", "");
        } else if (match(TokenType::INPUT)) {
            const auto& varTok = peek();
            consume(TokenType::IDENTIFIER, "Expected input variable name");
            consume(TokenType::COLON, "Expected ':' after input variable name");
            
            const auto& typeTok = peek();
            if (match(TokenType::TYPE_INT) || match(TokenType::TYPE_STRING) || 
                match(TokenType::TYPE_FLOAT) || match(TokenType::TYPE_BOOL)) {
                // Type consumed
            } else {
                throw std::runtime_error("Expected data type for input at line " + std::to_string(typeTok.line));
            }
            field = std::make_shared<ASTViewField>("input", varTok.value, varTok.value, "");
        } else if (match(TokenType::BUTTON)) {
            const auto& labelTok = peek();
            consume(TokenType::STRING_LITERAL, "Expected string literal for button label");
            consume(TokenType::ARROW, "Expected '->' after button label");
            
            std::string targetAction = "";
            const auto& target1 = peek();
            consume(TokenType::IDENTIFIER, "Expected identifier for button action target");
            targetAction = target1.value;
            if (match(TokenType::DOT)) {
                const auto& target2 = peek();
                consume(TokenType::IDENTIFIER, "Expected action name after '.' in button target");
                targetAction += "." + target2.value;
            }
            field = std::make_shared<ASTViewField>("button", labelTok.value, "", targetAction);
        } else if (match(TokenType::TABLE)) {
            const auto& sliceTok = peek();
            consume(TokenType::IDENTIFIER, "Expected slice identifier for table datasource");
            consume(TokenType::ARROW, "Expected '->' after table datasource");
            
            std::vector<std::string> cols;
            const auto& firstCol = peek();
            consume(TokenType::IDENTIFIER, "Expected column name");
            cols.push_back(firstCol.value);
            
            while (match(TokenType::COMMA)) {
                const auto& colTok = peek();
                consume(TokenType::IDENTIFIER, "Expected column name after ','");
                cols.push_back(colTok.value);
            }
            field = std::make_shared<ASTViewField>("table", sliceTok.value, cols);
        } else if (match(TokenType::HTML)) {
            const auto& strTok = peek();
            consume(TokenType::STRING_LITERAL, "Expected string literal after 'html'");
            field = std::make_shared<ASTViewField>("html", strTok.value, "", "");
        } else {
            const auto& tok = peek();
            throw std::runtime_error("Expected 'title', 'input', 'button', 'table', or 'html' in view definition, got: '" + tok.value + "' at line " + std::to_string(tok.line));
        }

        if (field) {
            if (check(TokenType::IDENTIFIER) && peek().value == "class") {
                consume(TokenType::IDENTIFIER, "class");
                const auto& classTok = peek();
                consume(TokenType::STRING_LITERAL, "Expected string literal for class name");
                field->className = classTok.value;
            }
            view->elements.push_back(field);
        }
    }

    consume(TokenType::RBRACE, "Expected '}' to close view body");
    return view;
}

std::shared_ptr<ASTApi> Parser::parseApi() {
    consume(TokenType::API, "Expected 'api'");
    const auto& nameTok = peek();
    consume(TokenType::IDENTIFIER, "Expected api identifier");
    std::string apiName = nameTok.value;

    consume(TokenType::LBRACE, "Expected '{' to start api body");

    auto api = std::make_shared<ASTApi>(apiName);

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        if (check(TokenType::USE)) {
            api->middlewares.push_back(parseMiddleware());
            continue;
        }
        bool isSecure = false;
        if (match(TokenType::SECURE)) {
            isSecure = true;
        }
        
        bool isWS = false;
        if (match(TokenType::WEBSOCKET)) {
            isWS = true;
        } else {
            consume(TokenType::ROUTE, "Expected 'route' or 'websocket' definition");
        }

        const auto& pathTok = peek();
        consume(TokenType::STRING_LITERAL, "Expected string literal for route path");
        
        std::string method = "GET";
        if (isWS) {
            method = "WEBSOCKET";
        } else {
            const auto& methodTok = peek();
            if (match(TokenType::HTTP_GET)) method = "GET";
            else if (match(TokenType::HTTP_POST)) method = "POST";
            else if (match(TokenType::HTTP_DELETE)) method = "DELETE";
            else {
                throw std::runtime_error("Expected HTTP method (GET, POST, DELETE) at line " + std::to_string(methodTok.line));
            }
        }

        consume(TokenType::ARROW, "Expected '->' in route definition");

        std::string targetAction = "";
        const auto& target1 = peek();
        consume(TokenType::IDENTIFIER, "Expected identifier for route target action");
        targetAction = target1.value;
        if (match(TokenType::DOT)) {
            const auto& target2 = peek();
            consume(TokenType::IDENTIFIER, "Expected action name after '.' in route target");
            targetAction += "." + target2.value;
        }

        api->routes.push_back(std::make_shared<ASTRoute>(pathTok.value, method, targetAction, isSecure));
    }

    consume(TokenType::RBRACE, "Expected '}' to close api body");
    return api;
}

std::shared_ptr<ASTExpression> Parser::parseExpression() {
    return parseRelational();
}

std::shared_ptr<ASTExpression> Parser::parseRelational() {
    auto expr = parseAdditive();
    while (check(TokenType::EQ_EQ) || check(TokenType::BANG_EQ) ||
           check(TokenType::LT) || check(TokenType::GT)) {
        const auto& opTok = advance();
        auto right = parseAdditive();
        expr = std::make_shared<ASTBinaryExpression>(opTok.value, expr, right);
    }
    return expr;
}

std::shared_ptr<ASTExpression> Parser::parseAdditive() {
    auto expr = parseMultiplicative();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        const auto& opTok = advance();
        auto right = parseMultiplicative();
        expr = std::make_shared<ASTBinaryExpression>(opTok.value, expr, right);
    }
    return expr;
}

std::shared_ptr<ASTExpression> Parser::parseMultiplicative() {
    auto expr = parsePrimary();
    while (check(TokenType::STAR) || check(TokenType::SLASH)) {
        const auto& opTok = advance();
        auto right = parsePrimary();
        expr = std::make_shared<ASTBinaryExpression>(opTok.value, expr, right);
    }
    return expr;
}

std::shared_ptr<ASTExpression> Parser::parsePrimary() {
    const auto& tok = peek();
    if (match(TokenType::STRING_LITERAL)) {
        return std::make_shared<ASTLiteral>(DataType::STRING, tok.value);
    } else if (match(TokenType::INT_LITERAL)) {
        return std::make_shared<ASTLiteral>(DataType::INT, tok.value);
    } else if (match(TokenType::IDENTIFIER)) {
        return std::make_shared<ASTIdentifier>(tok.value);
    } else if (match(TokenType::LPAREN)) {
        auto expr = parseExpression();
        consume(TokenType::RPAREN, "Expected ')' to close grouped expression");
        return expr;
    } else {
        throw std::runtime_error("Expected primary expression (literal, variable, or grouped expression) at line " +
                                 std::to_string(tok.line) + ", got: '" + tok.value + "'");
    }
}

void Parser::parseConfig(std::shared_ptr<ASTProgram> program) {
    consume(TokenType::CONFIG, "Expected 'config'");
    consume(TokenType::LBRACE, "Expected '{' to open config block");
    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        const auto& keyTok = peek();
        consume(TokenType::IDENTIFIER, "Expected config parameter key");
        consume(TokenType::COLON, "Expected ':' after config parameter key");
        
        if (keyTok.value == "database") {
            const auto& valTok = peek();
            consume(TokenType::IDENTIFIER, "Expected database type identifier (e.g., jsonl, postgres, mysql, sqlite)");
            program->dbType = valTok.value;
        } else if (keyTok.value == "frontend") {
            const auto& valTok = peek();
            consume(TokenType::IDENTIFIER, "Expected frontend type identifier (e.g., vanilla, react, svelte)");
            program->frontend = valTok.value;
        } else if (keyTok.value == "css") {
            const auto& valTok = peek();
            consume(TokenType::IDENTIFIER, "Expected CSS type identifier (e.g., vanilla, tailwind)");
            program->css = valTok.value;
        } else if (keyTok.value == "target") {
            const auto& valTok = peek();
            consume(TokenType::IDENTIFIER, "Expected target identifier (e.g., web, desktop)");
            program->target = valTok.value;
        } else if (keyTok.value == "http") {
            const auto& valTok = peek();
            consume(TokenType::IDENTIFIER, "Expected http value (true/false) to enable the outbound HTTP client");
            program->useHttp = (valTok.value == "true" || valTok.value == "on" || valTok.value == "yes");
        } else if (keyTok.value == "requires") {
            // One or more library names, comma-separated (e.g. requires: curl, ssl).
            while (true) {
                const auto& libTok = peek();
                consume(TokenType::IDENTIFIER, "Expected a library name in 'requires'");
                program->requiredLibs.push_back(libTok.value);
                if (!match(TokenType::COMMA)) break;
            }
        } else {
            advance(); // consume value
        }
    }
    consume(TokenType::RBRACE, "Expected '}' to close config block");
}

std::shared_ptr<ASTJob> Parser::parseJob() {
    consume(TokenType::JOB, "Expected 'job'");
    const auto& nameTok = peek();
    consume(TokenType::IDENTIFIER, "Expected job identifier");
    std::string jobName = nameTok.value;

    consume(TokenType::LBRACE, "Expected '{' to start job body");

    auto job = std::make_shared<ASTJob>(jobName);

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        if (check(TokenType::FIELD)) {
            job->fields.push_back(parseField());
        } else if (check(TokenType::ACTION)) {
            job->actions.push_back(parseAction());
        } else {
            const auto& tok = peek();
            throw std::runtime_error("Unexpected token '" + tok.value + "' inside job '" + jobName + "', line " + std::to_string(tok.line));
        }
    }

    consume(TokenType::RBRACE, "Expected '}' to close job body");
    return job;
}

std::shared_ptr<ASTMiddleware> Parser::parseMiddleware() {
    consume(TokenType::USE, "Expected 'use'");
    const auto& nameTok = peek();
    consume(TokenType::IDENTIFIER, "Expected middleware identifier");
    std::string mwName = nameTok.value;

    std::vector<std::string> args;
    if (match(TokenType::LPAREN)) {
        while (!check(TokenType::RPAREN) && !check(TokenType::END_OF_FILE)) {
            const auto& argTok = peek();
            if (check(TokenType::INT_LITERAL) || check(TokenType::IDENTIFIER) || check(TokenType::STRING_LITERAL)) {
                advance();
                args.push_back(argTok.value);
            } else {
                throw std::runtime_error("Unexpected token in middleware arguments: " + argTok.value + " at line " + std::to_string(argTok.line));
            }
            if (check(TokenType::COMMA)) {
                advance();
            } else {
                break;
            }
        }
        consume(TokenType::RPAREN, "Expected ')' to close middleware arguments");
    }
    return std::make_shared<ASTMiddleware>(mwName, args);
}

std::shared_ptr<ASTImport> Parser::parseImport() {
    consume(TokenType::IMPORT, "Expected 'import'");
    const auto& pathTok = peek();
    consume(TokenType::STRING_LITERAL, "Expected string path after 'import'");
    auto node = std::make_shared<ASTImport>();
    node->path = pathTok.value;
    return node;
}
