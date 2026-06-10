#include "codegen.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

std::string CodeGenerator::generateExpression(std::shared_ptr<ASTExpression> expr) {
    if (auto literal = std::dynamic_pointer_cast<ASTLiteral>(expr)) {
        if (literal->type == DataType::STRING) {
            return "\"" + literal->value + "\"";
        }
        return literal->value;
    } else if (auto identifier = std::dynamic_pointer_cast<ASTIdentifier>(expr)) {
        return identifier->name;
    } else if (auto binaryExpr = std::dynamic_pointer_cast<ASTBinaryExpression>(expr)) {
        return "(" + generateExpression(binaryExpr->left) + " " + binaryExpr->op + " " + generateExpression(binaryExpr->right) + ")";
    }
    return "";
}

std::string CodeGenerator::generateStatement(std::shared_ptr<ASTStatement> stmt) {
    std::stringstream ss;
    if (auto printStmt = std::dynamic_pointer_cast<ASTPrintStatement>(stmt)) {
        ss << "        std::cout << " << generateExpression(printStmt->expression) << " << std::endl;\n";
    } else if (auto assignStmt = std::dynamic_pointer_cast<ASTAssignmentStatement>(stmt)) {
        ss << "        " << assignStmt->variableName << " = " << generateExpression(assignStmt->expression) << ";\n";
    } else if (auto ifStmt = std::dynamic_pointer_cast<ASTIfStatement>(stmt)) {
        ss << "        if (" << generateExpression(ifStmt->condition) << ") {\n";
        for (const auto& s : ifStmt->thenBranch) {
            ss << "    " << generateStatement(s);
        }
        ss << "        }";
        if (!ifStmt->elseBranch.empty()) {
            ss << " else {\n";
            for (const auto& s : ifStmt->elseBranch) {
                ss << "    " << generateStatement(s);
            }
            ss << "        }\n";
        } else {
            ss << "\n";
        }
    } else if (auto whileStmt = std::dynamic_pointer_cast<ASTWhileStatement>(stmt)) {
        ss << "        while (" << generateExpression(whileStmt->condition) << ") {\n";
        for (const auto& s : whileStmt->body) {
            ss << "    " << generateStatement(s);
        }
        ss << "        }\n";
    } else if (auto callStmt = std::dynamic_pointer_cast<ASTCallStatement>(stmt)) {
        ss << "        " << callStmt->actionName << "();\n";
    }
    return ss.str();
}

std::string CodeGenerator::generateField(std::shared_ptr<ASTField> field) {
    std::stringstream ss;
    ss << "    " << dataTypeToString(field->type) << " " << field->name << ";\n";
    return ss.str();
}

std::string CodeGenerator::generateAction(std::shared_ptr<ASTAction> action) {
    std::stringstream ss;
    ss << "    void " << action->name << "() {\n";
    for (const auto& stmt : action->statements) {
        ss << generateStatement(stmt);
    }
    ss << "    }\n";
    return ss.str();
}

std::string CodeGenerator::generateSlice(std::shared_ptr<ASTSlice> slice) {
    std::stringstream ss;
    ss << "class " << slice->name << " {\n";
    ss << "public:\n";
    
    for (const auto& field : slice->fields) {
        ss << generateField(field);
    }
    
    ss << "\n";

    std::string dbType = program->dbType;

    // Helper to get SQL column escape char
    auto escapeCol = [&](const std::string& name) {
        if (dbType == "mysql") {
            return "`" + name + "`";
        } else if (dbType == "postgres" || dbType == "postgresql") {
            return "\\\"" + name + "\\\"";
        } else {
            return "\\\"" + name + "\\\""; // default to double quotes
        }
    };

    // -------------------------------------------------------------
    // JSONL Fallbacks
    // -------------------------------------------------------------
    // saveJSONL
    ss << "    void saveJSONL() {\n";
    ss << "        std::ofstream outfile(\"db_" << slice->name << ".jsonl\", std::ios::app);\n";
    ss << "        if (outfile.is_open()) {\n";
    ss << "            outfile << \"{\";\n";
    for (size_t i = 0; i < slice->fields.size(); ++i) {
        const auto& field = slice->fields[i];
        ss << "            outfile << \"\\\"" << field->name << "\\\":\";\n";
        if (field->type == DataType::STRING) {
            ss << "            outfile << \"\\\"\" << " << field->name << " << \"\\\"\";\n";
        } else if (field->type == DataType::BOOL) {
            ss << "            outfile << (" << field->name << " ? \"true\" : \"false\");\n";
        } else {
            ss << "            outfile << " << field->name << ";\n";
        }
        if (i + 1 < slice->fields.size()) {
            ss << "            outfile << \",\";\n";
        }
    }
    ss << "            outfile << \"}\\n\";\n";
    ss << "            outfile.close();\n";
    ss << "        }\n";
    ss << "    }\n\n";

    // getAllAsJSON_JSONL
    ss << "    static std::string getAllAsJSON_JSONL(const std::string& req = \"\") {\n";
    ss << "        std::ifstream infile(\"db_" << slice->name << ".jsonl\");\n";
    ss << "        std::stringstream ss;\n";
    ss << "        ss << \"[\";\n";
    ss << "        int limitVal = -1;\n";
    ss << "        int offsetVal = 0;\n";
    ss << "        std::string limitStr = getQueryParam(req, \"_limit\");\n";
    ss << "        if (!limitStr.empty()) {\n";
    ss << "            limitVal = safeStoi(limitStr, -1);\n";
    ss << "        }\n";
    ss << "        std::string offsetStr = getQueryParam(req, \"_offset\");\n";
    ss << "        if (!offsetStr.empty()) {\n";
    ss << "            offsetVal = safeStoi(offsetStr, 0);\n";
    ss << "        }\n";
    for (const auto& field : slice->fields) {
        ss << "        std::string filter_" << field->name << " = getQueryParam(req, \"" << field->name << "\");\n";
    }
    ss << "        int matchedCount = 0;\n";
    ss << "        int skipped = 0;\n";
    ss << "        if (infile.is_open()) {\n";
    ss << "            std::string line;\n";
    ss << "            bool first = true;\n";
    ss << "            while (std::getline(infile, line)) {\n";
    ss << "                if (line.empty()) continue;\n";
    ss << "                bool matches = true;\n";
    for (const auto& field : slice->fields) {
        ss << "                if (!filter_" << field->name << ".empty()) {\n";
        ss << "                    if (getJSONVal(line, \"" << field->name << "\") != filter_" << field->name << ") {\n";
        ss << "                        matches = false;\n";
        ss << "                    }\n";
        ss << "                }\n";
    }
    ss << "                if (!matches) continue;\n";
    ss << "                if (skipped < offsetVal) {\n";
    ss << "                    skipped++;\n";
    ss << "                    continue;\n";
    ss << "                }\n";
    ss << "                if (limitVal >= 0 && matchedCount >= limitVal) {\n";
    ss << "                    break;\n";
    ss << "                }\n";
    ss << "                if (!first) ss << \",\";\n";
    ss << "                ss << line;\n";
    ss << "                first = false;\n";
    ss << "                matchedCount++;\n";
    ss << "            }\n";
    ss << "            infile.close();\n";
    ss << "        }\n";
    ss << "        ss << \"]\";\n";
    ss << "        return ss.str();\n";
    ss << "    }\n\n";

    // deleteRecord_JSONL
    ss << "    static void deleteRecord_JSONL(const std::string& key, const std::string& value) {\n";
    ss << "        std::ifstream infile(\"db_" << slice->name << ".jsonl\");\n";
    ss << "        std::vector<std::string> lines;\n";
    ss << "        if (infile.is_open()) {\n";
    ss << "            std::string line;\n";
    ss << "            while (std::getline(infile, line)) {\n";
    ss << "                if (line.empty()) continue;\n";
    ss << "                if (getJSONVal(line, key) != value) {\n";
    ss << "                    lines.push_back(line);\n";
    ss << "                }\n";
    ss << "            }\n";
    ss << "            infile.close();\n";
    ss << "        }\n";
    ss << "        std::ofstream outfile(\"db_" << slice->name << ".jsonl\", std::ios::trunc);\n";
    ss << "        if (outfile.is_open()) {\n";
    ss << "            for (const auto& l : lines) {\n";
    ss << "                outfile << l << \"\\n\";\n";
    ss << "            }\n";
    ss << "            outfile.close();\n";
    ss << "        }\n";
    ss << "    }\n\n";

    // -------------------------------------------------------------
    // save() Method
    // -------------------------------------------------------------
    ss << "    void save() {\n";
    if (dbType == "sqlite") {
        ss << "        sqlite3* db = getSQLiteConn();\n";
        ss << "        if (!db) {\n";
        ss << "            saveJSONL();\n";
        ss << "            return;\n";
        ss << "        }\n";
        ss << "        std::string query = \"INSERT INTO \\\"" << slice->name << "\\\" (";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            ss << "\\\"" << slice->fields[i]->name << "\\\"" << (i + 1 < slice->fields.size() ? ", " : "");
        }
        ss << ") VALUES (";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            ss << "?" << (i + 1 < slice->fields.size() ? ", " : "");
        }
        ss << ");\";\n";
        ss << "        sqlite3_stmt* stmt;\n";
        ss << "        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {\n";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            const auto& field = slice->fields[i];
            if (field->type == DataType::STRING) {
                ss << "            sqlite3_bind_text(stmt, " << (i + 1) << ", " << field->name << ".c_str(), -1, SQLITE_TRANSIENT);\n";
            } else if (field->type == DataType::INT || field->type == DataType::RELATION) {
                ss << "            sqlite3_bind_int(stmt, " << (i + 1) << ", " << field->name << ");\n";
            } else if (field->type == DataType::FLOAT) {
                ss << "            sqlite3_bind_double(stmt, " << (i + 1) << ", " << field->name << ");\n";
            } else if (field->type == DataType::BOOL) {
                ss << "            sqlite3_bind_int(stmt, " << (i + 1) << ", " << field->name << " ? 1 : 0);\n";
            }
        }
        ss << "            if (sqlite3_step(stmt) != SQLITE_DONE) {\n";
        ss << "                std::cerr << \"[SQLite] Insert failed\" << std::endl;\n";
        ss << "            }\n";
        ss << "            sqlite3_finalize(stmt);\n";
        ss << "        }\n";
        ss << "        sqlite3_close(db);\n";
    } else if (dbType == "postgres" || dbType == "postgresql") {
        ss << "        PGconn* conn = getPGConn();\n";
        ss << "        if (!conn) {\n";
        ss << "            saveJSONL();\n";
        ss << "            return;\n";
        ss << "        }\n";
        ss << "        const char* paramValues[" << slice->fields.size() << "];\n";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            const auto& field = slice->fields[i];
            if (field->type == DataType::STRING) {
                ss << "        paramValues[" << i << "] = " << field->name << ".c_str();\n";
            } else {
                ss << "        std::string param_" << field->name << " = std::to_string(" << field->name << ");\n";
                ss << "        paramValues[" << i << "] = param_" << field->name << ".c_str();\n";
            }
        }
        ss << "        std::string query = \"INSERT INTO \\\"" << slice->name << "\\\" (";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            ss << "\\\"" << slice->fields[i]->name << "\\\"" << (i + 1 < slice->fields.size() ? ", " : "");
        }
        ss << ") VALUES (";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            ss << "$" << (i + 1) << (i + 1 < slice->fields.size() ? ", " : "");
        }
        ss << ");\";\n";
        ss << "        PGresult* res = PQexecParams(conn, query.c_str(), " << slice->fields.size() << ", NULL, paramValues, NULL, NULL, 0);\n";
        ss << "        if (PQresultStatus(res) != PGRES_COMMAND_OK) {\n";
        ss << "            std::cerr << \"[PostgreSQL] Insert failed: \" << PQerrorMessage(conn) << std::endl;\n";
        ss << "        }\n";
        ss << "        PQclear(res);\n";
        ss << "        PQfinish(conn);\n";
    } else if (dbType == "mysql") {
        ss << "        MYSQL* conn = getMySQLConn();\n";
        ss << "        if (!conn) {\n";
        ss << "            saveJSONL();\n";
        ss << "            return;\n";
        ss << "        }\n";
        ss << "        MYSQL_STMT* stmt = mysql_stmt_init(conn);\n";
        ss << "        if (stmt) {\n";
        ss << "            std::string query = \"INSERT INTO `" << slice->name << "` (";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            ss << "`" << slice->fields[i]->name << "`" << (i + 1 < slice->fields.size() ? ", " : "");
        }
        ss << ") VALUES (";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            ss << "?" << (i + 1 < slice->fields.size() ? ", " : "");
        }
        ss << ")\";\n";
        ss << "            if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) == 0) {\n";
        ss << "                MYSQL_BIND bind[" << slice->fields.size() << "];\n";
        ss << "                std::memset(bind, 0, sizeof(bind));\n";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            const auto& field = slice->fields[i];
            if (field->type == DataType::STRING) {
                ss << "                bind[" << i << "].buffer_type = MYSQL_TYPE_STRING;\n";
                ss << "                bind[" << i << "].buffer = (char*)" << field->name << ".c_str();\n";
                ss << "                bind[" << i << "].buffer_length = " << field->name << ".length();\n";
            } else if (field->type == DataType::INT || field->type == DataType::RELATION) {
                ss << "                bind[" << i << "].buffer_type = MYSQL_TYPE_LONG;\n";
                ss << "                bind[" << i << "].buffer = &" << field->name << ";\n";
            } else if (field->type == DataType::FLOAT) {
                ss << "                bind[" << i << "].buffer_type = MYSQL_TYPE_DOUBLE;\n";
                ss << "                bind[" << i << "].buffer = &" << field->name << ";\n";
            } else if (field->type == DataType::BOOL) {
                ss << "                bind[" << i << "].buffer_type = MYSQL_TYPE_TINY;\n";
                ss << "                bind[" << i << "].buffer = &" << field->name << ";\n";
            }
        }
        ss << "                mysql_stmt_bind_param(stmt, bind);\n";
        ss << "                if (mysql_stmt_execute(stmt) != 0) {\n";
        ss << "                    std::cerr << \"[MySQL] Insert failed: \" << mysql_stmt_error(stmt) << std::endl;\n";
        ss << "                }\n";
        ss << "                mysql_stmt_close(stmt);\n";
        ss << "            }\n";
        ss << "        }\n";
        ss << "        mysql_close(conn);\n";
    } else {
        ss << "        saveJSONL();\n";
    }
    ss << "    }\n\n";

    // -------------------------------------------------------------
    // getAllAsJSON() Method
    // -------------------------------------------------------------
    ss << "    static std::string getAllAsJSON(const std::string& req = \"\") {\n";
    if (dbType == "sqlite") {
        ss << "        sqlite3* db = getSQLiteConn();\n";
        ss << "        if (!db) return getAllAsJSON_JSONL(req);\n";
        ss << "        std::vector<std::pair<std::string, std::string>> filters;\n";
        for (const auto& field : slice->fields) {
            ss << "        {\n";
            ss << "            std::string val = getQueryParam(req, \"" << field->name << "\");\n";
            ss << "            if (!val.empty()) {\n";
            if (field->type == DataType::BOOL) {
                ss << "                if (val == \"true\") val = \"1\";\n";
                ss << "                else if (val == \"false\") val = \"0\";\n";
            }
            ss << "                filters.push_back({\"" << field->name << "\", val});\n";
            ss << "            }\n";
            ss << "        }\n";
        }
        ss << "        std::string query = \"SELECT * FROM \\\"" << slice->name << "\\\"\";\n";
        ss << "        if (!filters.empty()) {\n";
        ss << "            query += \" WHERE \";\n";
        ss << "            for (size_t i = 0; i < filters.size(); ++i) {\n";
        ss << "                query += \"\\\"\" + filters[i].first + \"\\\" = ?\";\n";
        ss << "                if (i + 1 < filters.size()) {\n";
        ss << "                    query += \" AND \";\n";
        ss << "                }\n";
        ss << "            }\n";
        ss << "        }\n";
        ss << "        std::string limitStr = getQueryParam(req, \"_limit\");\n";
        ss << "        std::string offsetStr = getQueryParam(req, \"_offset\");\n";
        ss << "        if (!limitStr.empty()) {\n";
        ss << "            query += \" LIMIT ?\";\n";
        ss << "        }\n";
        ss << "        if (!offsetStr.empty()) {\n";
        ss << "            query += \" OFFSET ?\";\n";
        ss << "        }\n";
        ss << "        sqlite3_stmt* stmt;\n";
        ss << "        std::stringstream ss;\n";
        ss << "        ss << \"[\";\n";
        ss << "        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {\n";
        ss << "            int bindIdx = 1;\n";
        ss << "            for (const auto& f : filters) {\n";
        ss << "                sqlite3_bind_text(stmt, bindIdx++, f.second.c_str(), -1, SQLITE_TRANSIENT);\n";
        ss << "            }\n";
        ss << "            if (!limitStr.empty()) {\n";
        ss << "                sqlite3_bind_int(stmt, bindIdx++, safeStoi(limitStr));\n";
        ss << "            }\n";
        ss << "            if (!offsetStr.empty()) {\n";
        ss << "                sqlite3_bind_int(stmt, bindIdx++, safeStoi(offsetStr));\n";
        ss << "            }\n";
        ss << "            bool first = true;\n";
        ss << "            while (sqlite3_step(stmt) == SQLITE_ROW) {\n";
        ss << "                if (!first) ss << \",\";\n";
        ss << "                ss << \"{\";\n";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            const auto& field = slice->fields[i];
            ss << "                ss << \"\\\"" << field->name << "\\\":\";\n";
            int colIdx = i + 1; // 0 is id
            if (field->type == DataType::STRING) {
                ss << "                ss << \"\\\"\" << sqlite3_column_text(stmt, " << colIdx << ") << \"\\\"\";\n";
            } else if (field->type == DataType::INT || field->type == DataType::RELATION) {
                ss << "                ss << sqlite3_column_int(stmt, " << colIdx << ");\n";
            } else if (field->type == DataType::FLOAT) {
                ss << "                ss << sqlite3_column_double(stmt, " << colIdx << ");\n";
            } else if (field->type == DataType::BOOL) {
                ss << "                ss << (sqlite3_column_int(stmt, " << colIdx << ") ? \"true\" : \"false\");\n";
            }
            if (i + 1 < slice->fields.size()) {
                ss << "                ss << \",\";\n";
            }
        }
        ss << "                ss << \"}\";\n";
        ss << "                first = false;\n";
        ss << "            }\n";
        ss << "            sqlite3_finalize(stmt);\n";
        ss << "        }\n";
        ss << "        ss << \"]\";\n";
        ss << "        sqlite3_close(db);\n";
        ss << "        return ss.str();\n";
    } else if (dbType == "postgres" || dbType == "postgresql") {
        ss << "        PGconn* conn = getPGConn();\n";
        ss << "        if (!conn) return getAllAsJSON_JSONL(req);\n";
        ss << "        std::vector<std::string> paramValues;\n";
        ss << "        std::string query = \"SELECT * FROM \\\"" << slice->name << "\\\"\";\n";
        for (const auto& field : slice->fields) {
            ss << "        {\n";
            ss << "            std::string val = getQueryParam(req, \"" << field->name << "\");\n";
            ss << "            if (!val.empty()) {\n";
            ss << "                paramValues.push_back(val);\n";
            ss << "                if (query.find(\" WHERE \") == std::string::npos) {\n";
            ss << "                    query += \" WHERE \";\n";
            ss << "                } else {\n";
            ss << "                    query += \" AND \";\n";
            ss << "                }\n";
            ss << "                query += \"\\\"" << field->name << "\\\" = $\" + std::to_string(paramValues.size());\n";
            if (field->type == DataType::INT || field->type == DataType::RELATION) {
                ss << "                query += \"::int\";\n";
            } else if (field->type == DataType::FLOAT) {
                ss << "                query += \"::float\";\n";
            } else if (field->type == DataType::BOOL) {
                ss << "                query += \"::boolean\";\n";
            }
            ss << "            }\n";
            ss << "        }\n";
        }
        ss << "        std::string limitStr = getQueryParam(req, \"_limit\");\n";
        ss << "        std::string offsetStr = getQueryParam(req, \"_offset\");\n";
        ss << "        if (!limitStr.empty()) {\n";
        ss << "            paramValues.push_back(limitStr);\n";
        ss << "            query += \" LIMIT $\" + std::to_string(paramValues.size()) + \"::int\";\n";
        ss << "        }\n";
        ss << "        if (!offsetStr.empty()) {\n";
        ss << "            paramValues.push_back(offsetStr);\n";
        ss << "            query += \" OFFSET $\" + std::to_string(paramValues.size()) + \"::int\";\n";
        ss << "        }\n";
        ss << "        std::vector<const char*> c_params;\n";
        ss << "        for (const auto& p : paramValues) {\n";
        ss << "            c_params.push_back(p.c_str());\n";
        ss << "        }\n";
        ss << "        PGresult* res = PQexecParams(conn, query.c_str(), c_params.size(), NULL, c_params.empty() ? NULL : c_params.data(), NULL, NULL, 0);\n";
        ss << "        if (PQresultStatus(res) != PGRES_TUPLES_OK) {\n";
        ss << "            PQclear(res);\n";
        ss << "            PQfinish(conn);\n";
        ss << "            return getAllAsJSON_JSONL(req);\n";
        ss << "        }\n";
        ss << "        int rows = PQntuples(res);\n";
        ss << "        std::stringstream ss;\n";
        ss << "        ss << \"[\";\n";
        ss << "        for (int i = 0; i < rows; ++i) {\n";
        ss << "            if (i > 0) ss << \",\";\n";
        ss << "            ss << \"{\";\n";
        for (size_t fIdx = 0; fIdx < slice->fields.size(); ++fIdx) {
            const auto& field = slice->fields[fIdx];
            ss << "            {\n";
            ss << "                int colIdx = PQfnumber(res, \"" << field->name << "\");\n";
            ss << "                ss << \"\\\"" << field->name << "\\\":\";\n";
            ss << "                if (PQgetisnull(res, i, colIdx)) {\n";
            ss << "                    ss << \"null\";\n";
            ss << "                } else {\n";
            ss << "                    std::string val = PQgetvalue(res, i, colIdx);\n";
            if (field->type == DataType::STRING) {
                ss << "                    ss << \"\\\"\" << val << \"\\\"\";\n";
            } else if (field->type == DataType::BOOL) {
                ss << "                    ss << (val == \"t\" || val == \"true\" || val == \"1\" ? \"true\" : \"false\");\n";
            } else {
                ss << "                    ss << val;\n";
            }
            ss << "                }\n";
            ss << "            }\n";
            if (fIdx + 1 < slice->fields.size()) {
                ss << "            ss << \",\";\n";
            }
        }
        ss << "            ss << \"}\";\n";
        ss << "        }\n";
        ss << "        ss << \"]\";\n";
        ss << "        PQclear(res);\n";
        ss << "        PQfinish(conn);\n";
        ss << "        return ss.str();\n";
    } else if (dbType == "mysql") {
        ss << "        MYSQL* conn = getMySQLConn();\n";
        ss << "        if (!conn) return getAllAsJSON_JSONL(req);\n";
        ss << "        std::string query = \"SELECT * FROM `" << slice->name << "`\";\n";
        for (const auto& field : slice->fields) {
            ss << "        {\n";
            ss << "            std::string val = getQueryParam(req, \"" << field->name << "\");\n";
            ss << "            if (!val.empty()) {\n";
            ss << "                if (query.find(\" WHERE \") == std::string::npos) {\n";
            ss << "                    query += \" WHERE \";\n";
            ss << "                } else {\n";
            ss << "                    query += \" AND \";\n";
            ss << "                }\n";
            if (field->type == DataType::INT || field->type == DataType::RELATION) {
                ss << "                query += \"`" << field->name << "` = \" + std::to_string(safeStoi(val));\n";
            } else if (field->type == DataType::BOOL) {
                ss << "                query += \"`" << field->name << "` = \" + std::to_string(val == \"true\" || val == \"1\" ? 1 : 0);\n";
            } else if (field->type == DataType::FLOAT) {
                ss << "                query += \"`" << field->name << "` = \" + std::to_string(safeStod(val));\n";
            } else {
                ss << "                char* escaped = new char[val.length() * 2 + 1];\n";
                ss << "                mysql_real_escape_string(conn, escaped, val.c_str(), val.length());\n";
                ss << "                query += \"`" << field->name << "` = '\" + std::string(escaped) + \"'\";\n";
                ss << "                delete[] escaped;\n";
            }
            ss << "            }\n";
            ss << "        }\n";
        }
        ss << "        std::string limitStr = getQueryParam(req, \"_limit\");\n";
        ss << "        std::string offsetStr = getQueryParam(req, \"_offset\");\n";
        ss << "        if (!limitStr.empty()) {\n";
        ss << "            query += \" LIMIT \" + std::to_string(safeStoi(limitStr));\n";
        ss << "        }\n";
        ss << "        if (!offsetStr.empty()) {\n";
        ss << "            query += \" OFFSET \" + std::to_string(safeStoi(offsetStr));\n";
        ss << "        }\n";
        ss << "        std::stringstream ss;\n";
        ss << "        ss << \"[\";\n";
        ss << "        if (mysql_query(conn, query.c_str()) == 0) {\n";
        ss << "            MYSQL_RES* result = mysql_store_result(conn);\n";
        ss << "            if (result) {\n";
        ss << "                int num_fields = mysql_num_fields(result);\n";
        ss << "                MYSQL_FIELD* fields = mysql_fetch_fields(result);\n";
        ss << "                MYSQL_ROW row;\n";
        ss << "                bool firstRow = true;\n";
        ss << "                while ((row = mysql_fetch_row(result))) {\n";
        ss << "                    if (!firstRow) ss << \",\";\n";
        ss << "                    ss << \"{\";\n";
        for (size_t fIdx = 0; fIdx < slice->fields.size(); ++fIdx) {
            const auto& field = slice->fields[fIdx];
            ss << "                    {\n";
            ss << "                        int colIdx = -1;\n";
            ss << "                        for (int k = 0; k < num_fields; ++k) {\n";
            ss << "                            if (std::string(fields[k].name) == \"" << field->name << "\") { colIdx = k; break; }\n";
            ss << "                        }\n";
            ss << "                        ss << \"\\\"" << field->name << "\\\":\";\n";
            ss << "                        if (colIdx == -1 || row[colIdx] == nullptr) {\n";
            ss << "                            ss << \"null\";\n";
            ss << "                        } else {\n";
            if (field->type == DataType::STRING) {
                ss << "                            ss << \"\\\"\" << row[colIdx] << \"\\\"\";\n";
            } else if (field->type == DataType::BOOL) {
                ss << "                            ss << (std::string(row[colIdx]) == \"1\" || std::string(row[colIdx]) == \"true\" ? \"true\" : \"false\");\n";
            } else {
                ss << "                            ss << row[colIdx];\n";
            }
            ss << "                        }\n";
            ss << "                    }\n";
            if (fIdx + 1 < slice->fields.size()) {
                ss << "                    ss << \",\";\n";
            }
        }
        ss << "                    ss << \"}\";\n";
        ss << "                    firstRow = false;\n";
        ss << "                }\n";
        ss << "                mysql_free_result(result);\n";
        ss << "            }\n";
        ss << "        }\n";
        ss << "        ss << \"]\";\n";
        ss << "        mysql_close(conn);\n";
        ss << "        return ss.str();\n";
    } else {
        ss << "        return getAllAsJSON_JSONL(req);\n";
    }
    ss << "    }\n\n";

    ss << "    static void deleteRecord(const std::string& key, const std::string& value) {\n";
    if (dbType == "sqlite") {
        ss << "        sqlite3* db = getSQLiteConn();\n";
        ss << "        if (!db) {\n";
        ss << "            deleteRecord_JSONL(key, value);\n";
        ss << "            return;\n";
        ss << "        }\n";
        ss << "        std::string query = \"DELETE FROM \\\"" << slice->name << "\\\" WHERE \\\"\" + key + \"\\\" = ?;\";\n";
        ss << "        sqlite3_stmt* stmt;\n";
        ss << "        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {\n";
        ss << "            sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);\n";
        ss << "            sqlite3_step(stmt);\n";
        ss << "            sqlite3_finalize(stmt);\n";
        ss << "        }\n";
        ss << "        sqlite3_close(db);\n";
    } else if (dbType == "postgres" || dbType == "postgresql") {
        ss << "        PGconn* conn = getPGConn();\n";
        ss << "        if (!conn) {\n";
        ss << "            deleteRecord_JSONL(key, value);\n";
        ss << "            return;\n";
        ss << "        }\n";
        ss << "        const char* paramValues[1];\n";
        ss << "        paramValues[0] = value.c_str();\n";
        ss << "        std::string query = \"DELETE FROM \\\"" << slice->name << "\\\" WHERE \\\"\" + key + \"\\\" = $1;\";\n";
        ss << "        PGresult* res = PQexecParams(conn, query.c_str(), 1, NULL, paramValues, NULL, NULL, 0);\n";
        ss << "        PQclear(res);\n";
        ss << "        PQfinish(conn);\n";
    } else if (dbType == "mysql") {
        ss << "        MYSQL* conn = getMySQLConn();\n";
        ss << "        if (!conn) {\n";
        ss << "            deleteRecord_JSONL(key, value);\n";
        ss << "            return;\n";
        ss << "        }\n";
        ss << "        MYSQL_STMT* stmt = mysql_stmt_init(conn);\n";
        ss << "        if (stmt) {\n";
        ss << "            std::string query = \"DELETE FROM `" << slice->name << "` WHERE `\" + key + \"` = ?\";\n";
        ss << "            if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) == 0) {\n";
        ss << "                MYSQL_BIND bind[1];\n";
        ss << "                std::memset(bind, 0, sizeof(bind));\n";
        ss << "                bind[0].buffer_type = MYSQL_TYPE_STRING;\n";
        ss << "                bind[0].buffer = (char*)value.c_str();\n";
        ss << "                bind[0].buffer_length = value.length();\n";
        ss << "                mysql_stmt_bind_param(stmt, bind);\n";
        ss << "                mysql_stmt_execute(stmt);\n";
        ss << "                mysql_stmt_close(stmt);\n";
        ss << "            }\n";
        ss << "        }\n";
        ss << "        mysql_close(conn);\n";
    } else {
        ss << "        deleteRecord_JSONL(key, value);\n";
    }
    ss << "    }\n\n";

    for (const auto& action : slice->actions) {
        ss << generateAction(action);
    }

    ss << "};\n";
    return ss.str();
}

std::string CodeGenerator::generateHTMLContent(std::shared_ptr<ASTView> view) {
    std::stringstream ss;
    ss << "<!DOCTYPE html>\n<html lang=\"es\">\n<head>\n"
       << "    <meta charset=\"UTF-8\">\n"
       << "    <title>";
    std::string titleText = "Hexagen App";
    for (const auto& elem : view->elements) {
        if (elem->type == "title") titleText = elem->label;
    }
    ss << titleText << "</title>\n"
       << "    <link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">\n"
       << "    <link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>\n"
       << "    <link href=\"https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&family=JetBrains+Mono:wght@400;700&display=swap\" rel=\"stylesheet\">\n"
       << "    <style>\n"
       << "        :root {\n"
       << "            --bg-color: #0b0f19;\n"
       << "            --card-bg: rgba(20, 30, 55, 0.45);\n"
       << "            --border-color: rgba(255, 255, 255, 0.08);\n"
       << "            --primary-glow: #00f2fe;\n"
       << "            --secondary-glow: #4facfe;\n"
       << "            --text-color: #f3f4f6;\n"
       << "            --text-muted: #9ca3af;\n"
       << "        }\n"
       << "        * { box-sizing: border-box; margin: 0; padding: 0; }\n"
       << "        body {\n"
       << "            font-family: 'Outfit', sans-serif; background-color: var(--bg-color); color: var(--text-color);\n"
       << "            min-height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; overflow-x: hidden; position: relative;\n"
       << "        }\n"
       << "        body::before {\n"
       << "            content: ''; position: absolute; width: 300px; height: 300px;\n"
       << "            background: radial-gradient(circle, var(--primary-glow) 0%, transparent 70%);\n"
       << "            top: 10%; left: 15%; opacity: 0.15; filter: blur(80px); z-index: 0;\n"
       << "        }\n"
       << "        body::after {\n"
       << "            content: ''; position: absolute; width: 350px; height: 350px;\n"
       << "            background: radial-gradient(circle, var(--secondary-glow) 0%, transparent 70%);\n"
       << "            bottom: 15%; right: 15%; opacity: 0.15; filter: blur(80px); z-index: 0;\n"
       << "        }\n"
       << "        .container { width: 100%; max-width: 550px; padding: 2rem; z-index: 1; }\n"
       << "        .card {\n"
       << "            background: var(--card-bg); backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px);\n"
       << "            border: 1px solid var(--border-color); border-radius: 24px; padding: 2.5rem; box-shadow: 0 20px 50px rgba(0, 0, 0, 0.3);\n"
       << "        }\n"
       << "        .heading-container { margin-bottom: 2rem; text-align: center; }\n"
       << "        .main-heading { font-size: 2rem; font-weight: 800; background: linear-gradient(135deg, #fff 0%, #a5b4fc 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent; margin-bottom: 0.5rem; }\n"
       << "        .sub-heading { font-size: 0.95rem; color: var(--text-muted); }\n"
       << "        .form-group { margin-bottom: 1.5rem; }\n"
       << "        .form-label { display: block; font-size: 0.85rem; font-weight: 600; text-transform: uppercase; color: var(--text-muted); margin-bottom: 0.5rem; }\n"
       << "        .form-input { width: 100%; background: rgba(255, 255, 255, 0.03); border: 1px solid var(--border-color); border-radius: 12px; padding: 0.85rem 1rem; color: white; font-family: inherit; font-size: 1rem; }\n"
       << "        .form-input:focus { outline: none; border-color: var(--primary-glow); background: rgba(255, 255, 255, 0.06); }\n"
       << "        .btn {\n"
       << "            width: 100%; background: linear-gradient(135deg, var(--secondary-glow) 0%, var(--primary-glow) 100%);\n"
       << "            border: none; color: #0b0f19; padding: 1rem; font-size: 1rem; font-weight: 700; border-radius: 12px; cursor: pointer; transition: all 0.3s ease; margin-bottom: 1rem;\n"
       << "        }\n"
       << "        .btn:hover { transform: translateY(-2px); filter: brightness(1.1); }\n"
       << "        .result-panel { margin-top: 2rem; background: rgba(0, 0, 0, 0.25); border-radius: 16px; border: 1px solid rgba(255, 255, 255, 0.05); padding: 1.25rem; display: none; }\n"
       << "        .result-title { font-size: 0.85rem; font-weight: 600; color: var(--primary-glow); margin-bottom: 0.5rem; text-transform: uppercase; }\n"
       << "        .result-code { font-family: 'JetBrains Mono', monospace; font-size: 0.85rem; white-space: pre-wrap; color: #e5e7eb; }\n"
       << "        .table-container { margin-top: 2rem; background: rgba(0, 0, 0, 0.2); border-radius: 12px; overflow: hidden; border: 1px solid var(--border-color); }\n"
       << "        .data-table { width: 100%; text-align: left; border-collapse: collapse; }\n"
       << "        .data-table th, .data-table td { padding: 0.75rem 1rem; border-bottom: 1px solid var(--border-color); }\n"
       << "        .data-table th { background: rgba(255, 255, 255, 0.03); font-size: 0.85rem; text-transform: uppercase; color: var(--text-muted); }\n"
       << "        .data-table td { font-size: 0.95rem; }\n"
       << "    </style>\n"
       << "</head>\n"
       << "<body>\n"
       << "    <main class=\"container\">\n"
       << "        <section class=\"card\">\n"
       << "            <div id=\"hexa-root\">\n";

    ss << "                <div class=\"heading-container\">\n";
    ss << "                    <h1 class=\"main-heading\">" << view->name << "</h1>\n";
    
    std::string sub = "Hexagen Compiled UI";
    for (const auto& elem : view->elements) {
        if (elem->type == "title") sub = elem->label;
    }
    ss << "                    <p class=\"sub-heading\">" << sub << "</p>\n";
    ss << "                </div>\n";

    for (const auto& elem : view->elements) {
        if (elem->type == "input") {
            ss << "                <div class=\"form-group\">\n";
            ss << "                    <label class=\"form-label\">" << elem->label << "</label>\n";
            ss << "                    <input type=\"text\" class=\"form-input\" id=\"input-" << elem->name << "\" name=\"" << elem->name << "\">\n";
            ss << "                </div>\n";
        } else if (elem->type == "button") {
            // Check if redirect / navigation button
            bool isNavigationView = false;
            std::string viewTarget = "";
            for (const auto& v : program->views) {
                if (v->name == elem->targetAction) {
                    isNavigationView = true;
                    viewTarget = v->name;
                    std::transform(viewTarget.begin(), viewTarget.end(), viewTarget.begin(), ::tolower);
                    break;
                }
            }

            if (isNavigationView) {
                ss << "                <button class=\"btn\" onclick=\"window.location.href = '/" << viewTarget << "'\">" << elem->label << "</button>\n";
            } else {
                std::string apiEndpoint = "/execute";
                if (!program->apis.empty()) {
                    for (const auto& r : program->apis[0]->routes) {
                        if (r->targetAction == elem->targetAction) {
                            apiEndpoint = r->path;
                            break;
                        }
                    }
                }
                ss << "                <button class=\"btn\" onclick=\"triggerAction('" << apiEndpoint << "')\">" << elem->label << "</button>\n";
            }
        } else if (elem->type == "table") {
            ss << "                <div class=\"table-container\">\n";
            ss << "                    <table class=\"data-table\">\n";
            ss << "                        <thead>\n";
            ss << "                            <tr>\n";
            for (const auto& col : elem->columns) {
                ss << "                                <th>" << col << "</th>\n";
            }
            // Add action column if there is delete route
            bool hasDeleteRoute = false;
            std::string deleteEndpoint = "";
            if (!program->apis.empty()) {
                for (const auto& r : program->apis[0]->routes) {
                    if (r->method == "DELETE") {
                        size_t dotPos = r->targetAction.find('.');
                        std::string targetSlice = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                        if (targetSlice == elem->label) {
                            hasDeleteRoute = true;
                            deleteEndpoint = r->path;
                            break;
                        }
                    }
                }
            }
            if (hasDeleteRoute) {
                ss << "                                <th>Acciones</th>\n";
            }
            ss << "                            </tr>\n";
            ss << "                        </thead>\n";
            ss << "                        <tbody id=\"table-body-" << elem->label << "\">\n";
            ss << "                            <!-- dynamic rows -->\n";
            ss << "                        </tbody>\n";
            ss << "                    </table>\n";
            ss << "                </div>\n";
        }
    }

    ss << "            </div>\n"
       << "            <div class=\"result-panel\" id=\"result-panel\">\n"
       << "                <div class=\"result-title\" id=\"result-title\">Respuesta de la API C++</div>\n"
       << "                <pre class=\"result-code\"><code id=\"result-code\"></code></pre>\n"
       << "            </div>\n"
       << "        </section>\n"
       << "    </main>\n"
       << "    <script>\n";

    // Refresh dynamic tables script
    ss << "        async function refreshTables() {\n";
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            bool hasDeleteRoute = false;
            std::string deleteEndpoint = "";
            if (!program->apis.empty()) {
                for (const auto& r : program->apis[0]->routes) {
                    if (r->method == "DELETE") {
                        size_t dotPos = r->targetAction.find('.');
                        std::string targetSlice = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                        if (targetSlice == elem->label) {
                            hasDeleteRoute = true;
                            deleteEndpoint = r->path;
                            break;
                        }
                    }
                }
            }
            ss << "            try {\n";
            ss << "                const response = await fetch('/api/" << elem->label << "');\n";
            ss << "                const data = await response.json();\n";
            ss << "                const tbody = document.getElementById('table-body-" << elem->label << "');\n";
            ss << "                if (tbody) {\n";
            ss << "                    tbody.innerHTML = '';\n";
            ss << "                    data.forEach(row => {\n";
            ss << "                        const tr = document.createElement('tr');\n";
            ss << "                        let rowHtml = '';\n";
            for (const auto& col : elem->columns) {
                ss << "                        rowHtml += `<td>${row." << col << " || ''}</td>`;\n";
            }
            if (hasDeleteRoute) {
                std::string keyCol = elem->columns.empty() ? "" : elem->columns[0];
                ss << "                        rowHtml += `<td><button class=\"btn\" style=\"padding:0.4rem 0.8rem; font-size:0.8rem; margin:0; width:auto; background:linear-gradient(135deg, #f43f5e 0%, #e11d48 100%); color:white;\" onclick=\"deleteRow('${row." << keyCol << "}', '" << deleteEndpoint << "')\">Eliminar</button></td>`;\n";
            }
            ss << "                        tr.innerHTML = rowHtml;\n";
            ss << "                        tbody.appendChild(tr);\n";
            ss << "                    });\n";
            ss << "                }\n";
            ss << "            } catch (err) {}\n";
        }
    }
    ss << "        }\n\n";

    // Delete row function
    ss << "        async function deleteRow(idValue, endpoint) {\n";
    ss << "            if (!confirm('¿Seguro que deseas eliminar este registro?')) return;\n";
    ss << "            const payload = {};\n";
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            std::string firstFieldName = "";
            for (const auto& slice : program->slices) {
                if (slice->name == elem->label) {
                    if (!slice->fields.empty()) {
                        firstFieldName = slice->fields[0]->name;
                    }
                }
            }
            if (firstFieldName.empty() && !elem->columns.empty()) {
                firstFieldName = elem->columns[0];
            }
            ss << "            payload['" << firstFieldName << "'] = idValue;\n";
        }
    }
    ss << "            try {\n";
    ss << "                const response = await fetch(endpoint, {\n";
    ss << "                    method: 'DELETE',\n";
    ss << "                    headers: {\n";
    ss << "                        'Content-Type': 'application/json',\n";
    ss << "                        'Authorization': 'Bearer hexagen_token_123'\n";
    ss << "                    },\n";
    ss << "                    body: JSON.stringify(payload)\n";
    ss << "                });\n";
    ss << "                const data = await response.json();\n";
    ss << "                document.getElementById('result-code').innerText = JSON.stringify(data, null, 2);\n";
    ss << "                document.getElementById('result-panel').style.display = 'block';\n";
    ss << "                refreshTables();\n";
    ss << "            } catch (err) {\n";
    ss << "                alert('Error al eliminar el registro');\n";
    ss << "            }\n";
    ss << "        }\n\n";

    ss << "        async function triggerAction(endpoint) {\n"
       << "            const payload = {};\n"
       << "            document.querySelectorAll('.form-input').forEach(input => {\n"
       << "                payload[input.name] = input.value;\n"
       << "            });\n"
       << "            try {\n"
       << "                const response = await fetch(endpoint, {\n"
       << "                    method: 'POST',\n"
       << "                    headers: { \n"
       << "                        'Content-Type': 'application/json',\n"
       << "                        'Authorization': 'Bearer hexagen_token_123'\n"
       << "                    },\n"
       << "                    body: JSON.stringify(payload)\n"
       << "                });\n"
       << "                const data = await response.json();\n"
       << "                document.getElementById('result-code').innerText = JSON.stringify(data, null, 2);\n"
       << "                document.getElementById('result-panel').style.display = 'block';\n"
       << "                refreshTables();\n"
       << "            } catch (err) {\n"
       << "                document.getElementById('result-code').innerText = 'Error connecting to API server.';\n"
       << "                document.getElementById('result-panel').style.display = 'block';\n"
       << "            }\n"
       << "        }\n";
    
    ss << "        window.onload = refreshTables;\n"
       << "    </script>\n"
       << "</body>\n"
       << "</html>\n";

    return ss.str();
}

std::string CodeGenerator::generateSourceCode(bool includeMain) {
    std::stringstream ss;
    ss << "// Generated automatically by Hexagen Framework\n";
    ss << "// Database Engine: " << program->dbType << "\n";
    if (program->dbType == "sqlite") {
        ss << "#include <sqlite3.h>\n";
    } else if (program->dbType == "postgres" || program->dbType == "postgresql") {
        ss << "#include <libpq-fe.h>\n";
    } else if (program->dbType == "mysql") {
        ss << "#include <mysql/mysql.h>\n";
    }
    ss << "#include <iostream>\n";
    ss << "#include <string>\n";
    ss << "#include <sstream>\n";
    ss << "#include <vector>\n";
    ss << "#include <thread>\n";
    ss << "#include <chrono>\n";
    ss << "#include <sys/socket.h>\n";
    ss << "#include <netinet/in.h>\n";
    ss << "#include <unistd.h>\n";
    ss << "#include <cstring>\n";
    ss << "#include <fstream>\n\n";

    ss << "// Simple JSON parser helpers\n"
       << "std::string getJSONVal(const std::string& json, const std::string& field) {\n"
       << "    std::string key = \"\\\"\" + field + \"\\\"\";\n"
       << "    size_t pos = json.find(key);\n"
       << "    if (pos == std::string::npos) return \"\";\n"
       << "    size_t colon = json.find(\":\", pos);\n"
       << "    if (colon == std::string::npos) return \"\";\n"
       << "    size_t valStart = colon + 1;\n"
       << "    while (valStart < json.length() && (json[valStart] == ' ' || json[valStart] == '\"')) valStart++;\n"
       << "    size_t valEnd = valStart;\n"
       << "    while (valEnd < json.length() && json[valEnd] != ',' && json[valEnd] != '}' && json[valEnd] != '\"' && json[valEnd] != '\\n') valEnd++;\n"
       << "    if (valStart >= valEnd) return \"\";\n"
       << "    return json.substr(valStart, valEnd - valStart);\n"
       << "}\n\n";

    ss << "// Simple query parameter extraction helper\n"
       << "std::string getQueryParam(const std::string& req, const std::string& key) {\n"
       << "    size_t firstLineEnd = req.find(\"\\n\");\n"
       << "    if (firstLineEnd == std::string::npos) return \"\";\n"
       << "    std::string reqLine = req.substr(0, firstLineEnd);\n"
       << "    size_t qPos = reqLine.find('?');\n"
       << "    if (qPos == std::string::npos) return \"\";\n"
       << "    size_t spacePos = reqLine.find(' ', qPos);\n"
       << "    if (spacePos == std::string::npos) spacePos = reqLine.length();\n"
       << "    std::string queryString = reqLine.substr(qPos + 1, spacePos - qPos - 1);\n"
       << "    std::string target = key + \"=\";\n"
       << "    size_t start = 0;\n"
       << "    while (true) {\n"
       << "        size_t p = queryString.find(target, start);\n"
       << "        if (p == std::string::npos) return \"\";\n"
       << "        if (p == 0 || queryString[p - 1] == '&') {\n"
       << "            size_t valStart = p + target.length();\n"
       << "            size_t valEnd = queryString.find('&', valStart);\n"
       << "            if (valEnd == std::string::npos) {\n"
       << "                return queryString.substr(valStart);\n"
       << "            } else {\n"
       << "                return queryString.substr(valStart, valEnd - valStart);\n"
       << "            }\n"
       << "        }\n"
       << "        start = p + 1;\n"
       << "    }\n"
       << "    return \"\";\n"
       << "}\n\n";


    // Global environment loader and helpers
    ss << "// Global environment loader and helpers\n"
       << "const char* getEnvOr(const char* key, const char* defaultVal) {\n"
       << "    const char* val = std::getenv(key);\n"
       << "    return val ? val : defaultVal;\n"
       << "}\n\n"
       << "std::string trim(const std::string& str) {\n"
       << "    size_t first = str.find_first_not_of(\" \\t\\r\\n\");\n"
       << "    if (first == std::string::npos) return \"\";\n"
       << "    size_t last = str.find_last_not_of(\" \\t\\r\\n\");\n"
       << "    return str.substr(first, (last - first + 1));\n"
       << "}\n\n"
       << "void loadEnv() {\n"
       << "    std::ifstream envFile(\".env\");\n"
       << "    if (!envFile.is_open()) return;\n"
       << "    std::string line;\n"
       << "    while (std::getline(envFile, line)) {\n"
       << "        line = trim(line);\n"
       << "        if (line.empty() || line[0] == '#') continue;\n"
       << "        size_t eqPos = line.find('=');\n"
       << "        if (eqPos == std::string::npos) continue;\n"
       << "        std::string key = trim(line.substr(0, eqPos));\n"
       << "        std::string val = trim(line.substr(eqPos + 1));\n"
       << "        if (val.size() >= 2 && val.front() == '\"' && val.back() == '\"') {\n"
       << "            val = val.substr(1, val.size() - 2);\n"
       << "        } else if (val.size() >= 2 && val.front() == '\\'' && val.back() == '\\'') {\n"
       << "            val = val.substr(1, val.size() - 2);\n"
       << "        }\n"
       << "        setenv(key.c_str(), val.c_str(), 1);\n"
       << "    }\n"
       << "    envFile.close();\n"
       << "}\n\n"
       << "int safeStoi(const std::string& val, int defaultVal = 0) {\n"
       << "    if (val.empty()) return defaultVal;\n"
       << "    try {\n"
       << "        return std::stoi(val);\n"
       << "    } catch (...) {\n"
       << "        return defaultVal;\n"
       << "    }\n"
       << "}\n\n"
       << "double safeStod(const std::string& val, double defaultVal = 0.0) {\n"
       << "    if (val.empty()) return defaultVal;\n"
       << "    try {\n"
       << "        return std::stod(val);\n"
       << "    } catch (...) {\n"
       << "        return defaultVal;\n"
       << "    }\n"
       << "}\n\n"
       << "std::string readHttpRequest(int client_fd) {\n"
        << "    std::string req;\n"
        << "    char buffer[4096];\n"
        << "    size_t bodyPos = std::string::npos;\n"
        << "    size_t contentLength = 0;\n"
        << "    while (true) {\n"
        << "        int valread = read(client_fd, buffer, sizeof(buffer));\n"
        << "        if (valread <= 0) break;\n"
        << "        req.append(buffer, valread);\n"
        << "        if (bodyPos == std::string::npos) {\n"
        << "            bodyPos = req.find(\"\\r\\n\\r\\n\");\n"
        << "            if (bodyPos != std::string::npos) {\n"
        << "                std::string reqLower = req.substr(0, bodyPos);\n"
        << "                for (char &c : reqLower) { if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; }\n"
        << "                size_t clPos = reqLower.find(\"content-length:\");\n"
        << "                if (clPos != std::string::npos) {\n"
        << "                    size_t valStart = clPos + 15;\n"
        << "                    while (valStart < reqLower.length() && (reqLower[valStart] == ' ' || reqLower[valStart] == '\\t')) valStart++;\n"
        << "                    size_t valEnd = req.find_first_of(\"\\r\\n\", valStart);\n"
        << "                    if (valEnd != std::string::npos) {\n"
        << "                        try {\n"
        << "                            contentLength = std::stoul(req.substr(valStart, valEnd - valStart));\n"
        << "                        } catch (...) {}\n"
        << "                    }\n"
        << "                }\n"
        << "            }\n"
        << "        }\n"
        << "        if (bodyPos != std::string::npos) {\n"
        << "            size_t readBodyBytes = req.length() - (bodyPos + 4);\n"
        << "            if (readBodyBytes >= contentLength) {\n"
        << "                break;\n"
        << "            }\n"
        << "        }\n"
        << "    }\n"
        << "    return req;\n"
        << "}\n\n";

        // Generate database connections helpers
    if (program->dbType == "sqlite") {
        ss << "sqlite3* getSQLiteConn() {\n"
           << "    std::string dbName = getEnvOr(\"DB_NAME\", \"vortex_db.db\");\n"
           << "    sqlite3* db = nullptr;\n"
           << "    int rc = sqlite3_open(dbName.c_str(), &db);\n"
           << "    if (rc != SQLITE_OK) {\n"
           << "        std::cerr << \"[SQLite] Can't open database: \" << sqlite3_errmsg(db) << std::endl;\n"
           << "        if (db) sqlite3_close(db);\n"
           << "        return nullptr;\n"
           << "    }\n"
           << "    return db;\n"
           << "}\n\n";
    } else if (program->dbType == "postgres" || program->dbType == "postgresql") {
        ss << "PGconn* getPGConn() {\n"
           << "    if (!std::getenv(\"DB_HOST\") && !std::getenv(\"DB_USER\")) return nullptr;\n"
           << "    std::string conninfo = \"host=\" + std::string(getEnvOr(\"DB_HOST\", \"localhost\")) +\n"
           << "                           \" port=\" + std::string(getEnvOr(\"DB_PORT\", \"5432\")) +\n"
           << "                           \" dbname=\" + std::string(getEnvOr(\"DB_NAME\", \"vortex_db\")) +\n"
           << "                           \" user=\" + std::string(getEnvOr(\"DB_USER\", \"postgres\")) +\n"
           << "                           \" password=\" + std::string(getEnvOr(\"DB_PASS\", \"\"));\n"
           << "    PGconn* conn = PQconnectdb(conninfo.c_str());\n"
           << "    if (PQstatus(conn) != CONNECTION_OK) {\n"
           << "        std::cerr << \"[PostgreSQL] Connection failed: \" << PQerrorMessage(conn) << std::endl;\n"
           << "        PQfinish(conn);\n"
           << "        return nullptr;\n"
           << "    }\n"
           << "    return conn;\n"
           << "}\n\n";
    } else if (program->dbType == "mysql") {
        ss << "MYSQL* getMySQLConn() {\n"
           << "    if (!std::getenv(\"DB_HOST\") && !std::getenv(\"DB_USER\")) return nullptr;\n"
           << "    MYSQL* conn = mysql_init(nullptr);\n"
           << "    if (!conn) {\n"
           << "        std::cerr << \"[MySQL] Initialization failed\" << std::endl;\n"
           << "        return nullptr;\n"
           << "    }\n"
           << "    const char* host = getEnvOr(\"DB_HOST\", \"127.0.0.1\");\n"
           << "    const char* user = getEnvOr(\"DB_USER\", \"root\");\n"
           << "    const char* pass = getEnvOr(\"DB_PASS\", \"\");\n"
           << "    const char* db = getEnvOr(\"DB_NAME\", \"vortex_db\");\n"
           << "    const char* portStr = getEnvOr(\"DB_PORT\", \"3306\");\n"
           << "    int port = std::stoi(portStr);\n"
           << "    if (!mysql_real_connect(conn, host, user, pass, db, port, nullptr, 0)) {\n"
           << "        std::cerr << \"[MySQL] Connection failed: \" << mysql_error(conn) << std::endl;\n"
           << "        mysql_close(conn);\n"
           << "        return nullptr;\n"
           << "    }\n"
           << "    return conn;\n"
           << "}\n\n";
    }

    // initDatabase()
    std::string dbType = program->dbType;
    ss << "void initDatabase() {\n";
    ss << "    loadEnv();\n";
    if (dbType == "sqlite") {
        ss << "    sqlite3* db = getSQLiteConn();\n"
           << "    if (db) {\n";
        for (const auto& slice : program->slices) {
            ss << "        {\n"
               << "            std::string q = \"CREATE TABLE IF NOT EXISTS \\\"" << slice->name << "\\\" (id INTEGER PRIMARY KEY AUTOINCREMENT";
            for (const auto& field : slice->fields) {
                ss << ", \\\"" << field->name << "\\\" ";
                if (field->type == DataType::INT) ss << "INTEGER";
                else if (field->type == DataType::RELATION) ss << "INTEGER REFERENCES \\\"" << field->relatedSlice << "\\\"(id)";
                else if (field->type == DataType::FLOAT) ss << "REAL";
                else if (field->type == DataType::BOOL) ss << "INTEGER";
                else ss << "TEXT";
            }
            ss << ");\";\n"
               << "            char* errMsg = nullptr;\n"
               << "            sqlite3_exec(db, q.c_str(), nullptr, nullptr, &errMsg);\n"
               << "        }\n";
        }
        ss << "        sqlite3_close(db);\n"
           << "    }\n";
    } else if (dbType == "postgres" || dbType == "postgresql") {
        ss << "    PGconn* conn = getPGConn();\n"
           << "    if (conn) {\n";
        for (const auto& slice : program->slices) {
            ss << "        {\n"
               << "            std::string q = \"CREATE TABLE IF NOT EXISTS \\\"" << slice->name << "\\\" (id SERIAL PRIMARY KEY";
            for (const auto& field : slice->fields) {
                ss << ", \\\"" << field->name << "\\\" ";
                if (field->type == DataType::INT) ss << "INT";
                else if (field->type == DataType::RELATION) ss << "INT REFERENCES \\\"" << field->relatedSlice << "\\\"(id)";
                else if (field->type == DataType::FLOAT) ss << "REAL";
                else if (field->type == DataType::BOOL) ss << "BOOLEAN";
                else ss << "VARCHAR(255)";
            }
            ss << ");\";\n"
               << "            PGresult* res = PQexec(conn, q.c_str());\n"
               << "            PQclear(res);\n"
               << "        }\n";
        }
        ss << "        PQfinish(conn);\n"
           << "    }\n";
    } else if (dbType == "mysql") {
        ss << "    MYSQL* conn = getMySQLConn();\n"
           << "    if (conn) {\n";
        for (const auto& slice : program->slices) {
            ss << "        {\n"
               << "            std::string q = \"CREATE TABLE IF NOT EXISTS `" << slice->name << "` (id INT AUTO_INCREMENT PRIMARY KEY";
            for (const auto& field : slice->fields) {
                ss << ", `" << field->name << "` ";
                if (field->type == DataType::INT) ss << "INT";
                else if (field->type == DataType::RELATION) ss << "INT REFERENCES `" << field->relatedSlice << "`(id)";
                else if (field->type == DataType::FLOAT) ss << "DOUBLE";
                else if (field->type == DataType::BOOL) ss << "TINYINT(1)";
                else ss << "VARCHAR(255)";
            }
            ss << ");\";\n"
               << "            mysql_query(conn, q.c_str());\n"
               << "        }\n";
        }
        ss << "        mysql_close(conn);\n"
           << "    }\n";
    } else {
        ss << "    // No init needed for JSONL\n";
    }
    ss << "}\n\n";

    for (const auto& slice : program->slices) {
        ss << generateSlice(slice) << "\n";
    }

    ss << "// Raw UI View HTML Pages\n";
    for (const auto& view : program->views) {
        ss << "const char* HTML_" << view->name << " = R\"HTML(\n";
        ss << generateHTMLContent(view);
        ss << "\n)HTML\";\n\n";
    }

    if (!program->views.empty()) {
        ss << "const char* HTML_CONTENT = HTML_" << program->views[0]->name << ";\n\n";
    } else {
        ss << "const char* HTML_CONTENT = \"No view defined\";\n\n";
    }

    if (includeMain) {
        ss << "int main() {\n";
        ss << "    initDatabase();\n";
        ss << "    int server_fd, client_fd;\n";
        ss << "    struct sockaddr_in address;\n";
        ss << "    int opt = 1;\n";
        ss << "    int addrlen = sizeof(address);\n";
        ss << "    int port = 8080;\n\n";
        
        ss << "    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) return 1;\n";
        ss << "    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));\n";
        ss << "    address.sin_family = AF_INET;\n";
        ss << "    address.sin_addr.s_addr = INADDR_ANY;\n";
        ss << "    address.sin_port = htons(port);\n\n";
        
        ss << "    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;\n";
        ss << "    if (listen(server_fd, 5) < 0) return 1;\n\n";
        
        ss << "    std::cout << \"🏎️  [Hexagen Server] App running at http://localhost:\" << port << std::endl;\n\n";
        
        ss << "    while (true) {\n";
        ss << "        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;\n";
                ss << "        std::string req = readHttpRequest(client_fd);\n";
        ss << "        if (!req.empty()) {\n";        
        // Dynamic multi-view routing
        bool isFirstRoute = true;
        if (program->views.empty()) {
            ss << "            if (req.rfind(\"GET / \", 0) == 0 || req.rfind(\"GET /index.html\", 0) == 0 || req.rfind(\"GET /home \", 0) == 0) {\n";
            ss << "                std::string html = HTML_CONTENT;\n";
            ss << "                std::stringstream resp;\n";
            ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
            ss << "                     << \"Content-Type: text/html; charset=utf-8\\r\\n\"\n";
            ss << "                     << \"Content-Length: \" << html.length() << \"\\r\\n\\r\\n\"\n";
            ss << "                     << html;\n";
            ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "            }\n";
            isFirstRoute = false;
        } else {
            for (const auto& view : program->views) {
                std::string viewNameLower = view->name;
                std::transform(viewNameLower.begin(), viewNameLower.end(), viewNameLower.begin(), ::tolower);
                
                if (isFirstRoute) {
                    ss << "            if (req.rfind(\"GET /" << viewNameLower << " \", 0) == 0 || req.rfind(\"GET / \", 0) == 0 || req.rfind(\"GET /index.html\", 0) == 0) {\n";
                    isFirstRoute = false;
                } else {
                    ss << "            else if (req.rfind(\"GET /" << viewNameLower << " \", 0) == 0) {\n";
                }
                ss << "                std::string html = HTML_" << view->name << ";\n";
                ss << "                std::stringstream resp;\n";
                ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
                ss << "                     << \"Content-Type: text/html; charset=utf-8\\r\\n\"\n";
                ss << "                     << \"Content-Length: \" << html.length() << \"\\r\\n\\r\\n\"\n";
                ss << "                     << html;\n";
                ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                ss << "            }\n";
            }
        }

        // Route serving database tables queries
        for (const auto& slice : program->slices) {
            if (isFirstRoute) {
                ss << "            if (req.find(\"GET /api/" << slice->name << "\") != std::string::npos) {\n";
                isFirstRoute = false;
            } else {
                ss << "            else if (req.find(\"GET /api/" << slice->name << "\") != std::string::npos) {\n";
            }
            ss << "                std::string json = " << slice->name << "::getAllAsJSON(req);\n";
            ss << "                std::stringstream resp;\n";
            ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
            ss << "                     << \"Content-Type: application/json\\r\\n\"\n";
            ss << "                     << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
            ss << "                     << \"Content-Length: \" << json.length() << \"\\r\\n\\r\\n\"\n";
            ss << "                     << json;\n";
            ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "            }\n";
        }

        // Routing for API endpoints
        if (!program->apis.empty()) {
            for (const auto& r : program->apis[0]->routes) {
                if (isFirstRoute) {
                    ss << "            if (req.find(\"" << r->method << " " << r->path << "\") != std::string::npos) {\n";
                    isFirstRoute = false;
                } else {
                    ss << "            else if (req.find(\"" << r->method << " " << r->path << "\") != std::string::npos) {\n";
                }
                if (r->isSecure) {
                    ss << "                if (req.find(\"Authorization: Bearer hexagen_token_123\") == std::string::npos) {\n";
                    ss << "                    std::string msg = \"{\\\"status\\\":\\\"error\\\",\\\"message\\\":\\\"Unauthorized\\\"}\";\n";
                    ss << "                    std::stringstream resp;\n";
                    ss << "                    resp << \"HTTP/1.1 401 Unauthorized\\r\\n\"\n";
                    ss << "                         << \"Content-Type: application/json\\r\\n\"\n";
                    ss << "                         << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
                    ss << "                         << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
                    ss << "                         << msg;\n";
                    ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                    ss << "                    close(client_fd);\n";
                    ss << "                    continue;\n";
                    ss << "                }\n";
                }
                ss << "                size_t bodyPos = req.find(\"\\r\\n\\r\\n\");\n";
                ss << "                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : \"\";\n";
                
                ss << "                std::cout << \"[HTTP Endpoint] Invoked " << r->path << " -> Running " << r->targetAction << "\" << std::endl;\n";
                
                size_t dotPos = r->targetAction.find('.');
                std::string sliceName = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                std::string actionName = (dotPos != std::string::npos) ? r->targetAction.substr(dotPos + 1) : r->targetAction;

                if (!sliceName.empty()) {
                    if (r->method == "DELETE") {
                        std::string firstField = "";
                        for (const auto& s : program->slices) {
                            if (s->name == sliceName) {
                                if (!s->fields.empty()) {
                                    firstField = s->fields[0]->name;
                                }
                            }
                        }
                        if (!firstField.empty()) {
                            ss << "                std::string valToDelete = getJSONVal(body, \"" << firstField << "\");\n";
                            ss << "                " << sliceName << "::deleteRecord(\"" << firstField << "\", valToDelete);\n";
                        }
                        ss << "                " << sliceName << " instance;\n";
                        ss << "                instance." << actionName << "();\n";
                    } else {
                        ss << "                " << sliceName << " instance;\n";
                        for (const auto& slice : program->slices) {
                            if (slice->name == sliceName) {
                                for (const auto& field : slice->fields) {
                                    ss << "                instance." << field->name << " = ";
                                                                        if (field->type == DataType::INT || field->type == DataType::RELATION) {
                                        ss << "safeStoi(getJSONVal(body, \"" << field->name << "\"));\n";
                                    } else if (field->type == DataType::STRING) {
                                        ss << "getJSONVal(body, \"" << field->name << "\");\n";
                                    } else if (field->type == DataType::FLOAT) {
                                        ss << "safeStod(getJSONVal(body, \"" << field->name << "\"));\n";
                                    } else if (field->type == DataType::BOOL) {
                                        ss << "(getJSONVal(body, \"" << field->name << "\") == \"true\");\n";
                                    }
                                }
                            }
                        }
                        ss << "                instance.save();\n";
                        ss << "                instance." << actionName << "();\n";
                    }
                }
                
                ss << "                std::stringstream json;\n";
                ss << "                json << \"{\\n\"\n";
                ss << "                     << \"  \\\"status\\\": \\\"success\\\",\\n\"\n";
                ss << "                     << \"  \\\"message\\\": \\\"Action " << r->targetAction << " executed successfully!\\\"\\n\"\n";
                ss << "                     << \"}\";\n";
                
                ss << "                std::stringstream resp;\n";
                ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
                ss << "                     << \"Content-Type: application/json\\r\\n\"\n";
                ss << "                     << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
                ss << "                     << \"Content-Length: \" << json.str().length() << \"\\r\\n\\r\\n\"\n";
                ss << "                     << json.str();\n";
                ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                ss << "            }\n";
            }
        }
        
        ss << "            else {\n";
        ss << "                std::string msg = \"Not Found\";\n";
        ss << "                std::stringstream resp;\n";
        ss << "                resp << \"HTTP/1.1 404 Not Found\\r\\n\"\n";
        ss << "                     << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
        ss << "                     << msg;\n";
        ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
        ss << "            }\n";

        ss << "        }\n";
        ss << "        close(client_fd);\n";
        ss << "    }\n";
        ss << "    close(server_fd);\n";
        ss << "    return 0;\n";
        ss << "}\n";
    }

    return ss.str();
}
