#include "codegen.hpp"
#include "webview_content.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

// Decode escape sequences into their literal characters. Used when emitting raw
// C++ from `cpp "..."` blocks, where the lexer preserves escapes verbatim but the
// output must be actual C++ source (e.g. \" must become a real ").
static std::string decodeEscapes(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            char n = in[i + 1];
            switch (n) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"'; break;
                case '\'': out += '\''; break;
                case '\\': out += '\\'; break;
                default: out += n; break;
            }
            ++i;
        } else {
            out += in[i];
        }
    }
    return out;
}

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
        std::string callName = callStmt->actionName;
        size_t dotPos = callName.find('.');
        if (dotPos != std::string::npos) {
            callName.replace(dotPos, 1, "::");
        }
        ss << "        " << callName << "(";
        for (size_t i = 0; i < callStmt->arguments.size(); ++i) {
            ss << generateExpression(callStmt->arguments[i]);
            if (i + 1 < callStmt->arguments.size()) ss << ", ";
        }
        ss << ");\n";
    } else if (auto enqueueStmt = std::dynamic_pointer_cast<ASTEnqueueStatement>(stmt)) {
        ss << "        {\n";
        ss << "            auto jobInst = std::make_shared<Job_" << enqueueStmt->jobName << ">();\n";
        for (const auto& arg : enqueueStmt->arguments) {
            ss << "            jobInst->" << arg.first << " = " << generateExpression(arg.second) << ";\n";
        }
        // Serialize all job fields so the job is durable (persisted + recoverable).
        std::shared_ptr<ASTJob> jdef;
        for (const auto& j : program->jobs) if (j->name == enqueueStmt->jobName) jdef = j;
        ss << "            std::stringstream _aj; _aj << \"{\";\n";
        if (jdef) {
            for (size_t i = 0; i < jdef->fields.size(); ++i) {
                const auto& f = jdef->fields[i];
                ss << "            _aj << \"\\\"" << f->name << "\\\":\";\n";
                if (f->type == DataType::STRING) {
                    ss << "            _aj << \"\\\"\" << jobInst->" << f->name << " << \"\\\"\";\n";
                } else if (f->type == DataType::BOOL) {
                    ss << "            _aj << (jobInst->" << f->name << " ? \"true\" : \"false\");\n";
                } else {
                    ss << "            _aj << jobInst->" << f->name << ";\n";
                }
                if (i + 1 < jdef->fields.size()) ss << "            _aj << \",\";\n";
            }
        }
        ss << "            _aj << \"}\";\n";
        std::string entry = "";
        if (jdef) {
            for (const auto& a : jdef->actions) if (a->name == "Run") entry = "Run";
            if (entry.empty() && !jdef->actions.empty()) entry = jdef->actions[0]->name;
        }
        if (entry.empty()) entry = "Run";
        ss << "            enqueue_persisted_job(\"" << enqueueStmt->jobName << "\", _aj.str(), [jobInst]() { jobInst->" << entry << "(); });\n";
        ss << "        }\n";
    } else if (auto cppStmt = std::dynamic_pointer_cast<ASTCppStatement>(stmt)) {
        ss << "        " << decodeEscapes(cppStmt->code) << "\n";
    }
    return ss.str();
}

static std::shared_ptr<ASTSlice> findUserSlice(const std::shared_ptr<ASTProgram>& program) {
    for (const auto& slice : program->slices) {
        std::string nameLower = slice->name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower == "usuario" || nameLower == "user") {
            return slice;
        }
    }
    return nullptr;
}

static std::string getEmailFieldName(const std::shared_ptr<ASTSlice>& slice) {
    for (const auto& field : slice->fields) {
        std::string nameLower = field->name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower == "email" || nameLower == "correo" || nameLower == "username" || nameLower == "usuario") {
            return field->name;
        }
    }
    return "";
}

static std::string getPasswordFieldName(const std::shared_ptr<ASTSlice>& slice) {
    for (const auto& field : slice->fields) {
        std::string nameLower = field->name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower == "contrasena" || nameLower == "password" || nameLower == "clave" || nameLower == "pass") {
            return field->name;
        }
    }
    return "";
}

static std::string getRoleFieldName(const std::shared_ptr<ASTSlice>& slice) {
    for (const auto& field : slice->fields) {
        std::string nameLower = field->name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower == "rol" || nameLower == "role") {
            return field->name;
        }
    }
    return "";
}

std::string CodeGenerator::generateField(std::shared_ptr<ASTField> field) {
    std::stringstream ss;
    ss << "    " << dataTypeToString(field->type) << " " << field->name << ";\n";
    return ss.str();
}

std::string CodeGenerator::generateActionImpl(const std::string& className, std::shared_ptr<ASTAction> action) {
    std::stringstream ss;
    ss << "void " << className << "::" << action->name << "() {\n";
    for (const auto& stmt : action->statements) {
        ss << generateStatement(stmt);
    }
    ss << "}\n";
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

    // -------------------------------------------------------------
    // Changeset validation: collects ALL errors (field -> message)
    // before any write, Ecto-style. Empty map => valid.
    // -------------------------------------------------------------
    {
        auto fieldType = [&](const std::string& fname) -> DataType {
            for (const auto& f : slice->fields) if (f->name == fname) return f->type;
            return DataType::UNKNOWN;
        };
        ss << "    std::map<std::string, std::string> validateChangeset() {\n";
        ss << "        std::map<std::string, std::string> errors;\n";
        for (const auto& v : slice->validations) {
            if (v->args.empty()) continue;
            const std::string& fld = v->args[0];
            DataType ft = fieldType(fld);
            if (ft == DataType::UNKNOWN) continue; // unknown field, skip safely
            if (v->rule == "required") {
                if (ft == DataType::STRING) {
                    ss << "        if (" << fld << ".empty()) errors[\"" << fld << "\"] = \"is required\";\n";
                }
            } else if (v->rule == "length" && v->args.size() >= 3) {
                if (ft == DataType::STRING) {
                    ss << "        if (errors.find(\"" << fld << "\") == errors.end() && (" << fld << ".size() < " << v->args[1]
                       << " || " << fld << ".size() > " << v->args[2] << ")) errors[\"" << fld
                       << "\"] = \"must be between " << v->args[1] << " and " << v->args[2] << " characters\";\n";
                }
            } else if (v->rule == "format" && v->args.size() >= 2) {
                if (ft == DataType::STRING && (v->args[1] == "email")) {
                    ss << "        if (errors.find(\"" << fld << "\") == errors.end() && !isValidEmail(" << fld
                       << ")) errors[\"" << fld << "\"] = \"has invalid format\";\n";
                }
            } else if (v->rule == "min" && v->args.size() >= 2) {
                if (ft == DataType::INT || ft == DataType::FLOAT || ft == DataType::RELATION) {
                    ss << "        if (" << fld << " < " << v->args[1] << ") errors[\"" << fld
                       << "\"] = \"must be at least " << v->args[1] << "\";\n";
                }
            } else if (v->rule == "max" && v->args.size() >= 2) {
                if (ft == DataType::INT || ft == DataType::FLOAT || ft == DataType::RELATION) {
                    ss << "        if (" << fld << " > " << v->args[1] << ") errors[\"" << fld
                       << "\"] = \"must be at most " << v->args[1] << "\";\n";
                }
            }
        }
        ss << "        return errors;\n";
        ss << "    }\n\n";
    }

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
        ss << "        releaseSQLiteConn(db);\n";
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
        ss << "        releasePGConn(conn);\n";
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
        ss << "        releaseMySQLConn(conn);\n";
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
        ss << "        releaseSQLiteConn(db);\n";
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
        ss << "            releasePGConn(conn);\n";
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
        ss << "        releasePGConn(conn);\n";
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
        ss << "        releaseMySQLConn(conn);\n";
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
        ss << "        releaseSQLiteConn(db);\n";
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
        ss << "        releasePGConn(conn);\n";
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
        ss << "        releaseMySQLConn(conn);\n";
    } else {
        ss << "        deleteRecord_JSONL(key, value);\n";
    }
    ss << "    }\n\n";

    // Inject findUser/findUser_JSONL if slice is Usuario or User
    {
        std::string nameLower = slice->name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower == "usuario" || nameLower == "user") {
            std::string emailField = getEmailFieldName(slice);
            std::string passwordField = getPasswordFieldName(slice);
            if (!emailField.empty() && !passwordField.empty()) {
                // Generate findUser_JSONL
                ss << "    static bool findUser_JSONL(const std::string& emailVal, " << slice->name << "& user) {\n";
                ss << "        std::ifstream infile(\"db_" << slice->name << ".jsonl\");\n";
                ss << "        if (infile.is_open()) {\n";
                ss << "            std::string line;\n";
                ss << "            while (std::getline(infile, line)) {\n";
                ss << "                if (line.empty()) continue;\n";
                ss << "                if (getJSONVal(line, \"" << emailField << "\") == emailVal) {\n";
                for (const auto& f : slice->fields) {
                    if (f->type == DataType::STRING) {
                        ss << "                    user." << f->name << " = getJSONVal(line, \"" << f->name << "\");\n";
                    } else if (f->type == DataType::INT || f->type == DataType::RELATION) {
                        ss << "                    user." << f->name << " = safeStoi(getJSONVal(line, \"" << f->name << "\"));\n";
                    } else if (f->type == DataType::FLOAT) {
                        ss << "                    user." << f->name << " = safeStod(getJSONVal(line, \"" << f->name << "\"));\n";
                    } else if (f->type == DataType::BOOL) {
                        ss << "                    user." << f->name << " = (getJSONVal(line, \"" << f->name << "\") == \"true\");\n";
                    }
                }
                ss << "                    infile.close();\n";
                ss << "                    return true;\n";
                ss << "                }\n";
                ss << "            }\n";
                ss << "            infile.close();\n";
                ss << "        }\n";
                ss << "        return false;\n";
                ss << "    }\n\n";

                // Generate findUser
                ss << "    static bool findUser(const std::string& emailVal, " << slice->name << "& user) {\n";
                if (dbType == "sqlite") {
                    ss << "        sqlite3* db = getSQLiteConn();\n";
                    ss << "        if (!db) return findUser_JSONL(emailVal, user);\n";
                    ss << "        std::string query = \"SELECT * FROM \\\"" << slice->name << "\\\" WHERE \\\"" << emailField << "\\\" = ?;\";\n";
                    ss << "        sqlite3_stmt* stmt;\n";
                    ss << "        bool found = false;\n";
                    ss << "        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {\n";
                    ss << "            sqlite3_bind_text(stmt, 1, emailVal.c_str(), -1, SQLITE_TRANSIENT);\n";
                    ss << "            if (sqlite3_step(stmt) == SQLITE_ROW) {\n";
                    for (size_t i = 0; i < slice->fields.size(); ++i) {
                        const auto& f = slice->fields[i];
                        int colIdx = i + 1; // 0 is id
                        if (f->type == DataType::STRING) {
                            ss << "                user." << f->name << " = (const char*)sqlite3_column_text(stmt, " << colIdx << ");\n";
                        } else if (f->type == DataType::INT || f->type == DataType::RELATION) {
                            ss << "                user." << f->name << " = sqlite3_column_int(stmt, " << colIdx << ");\n";
                        } else if (f->type == DataType::FLOAT) {
                            ss << "                user." << f->name << " = sqlite3_column_double(stmt, " << colIdx << ");\n";
                        } else if (f->type == DataType::BOOL) {
                            ss << "                user." << f->name << " = (sqlite3_column_int(stmt, " << colIdx << ") != 0);\n";
                        }
                    }
                    ss << "                found = true;\n";
                    ss << "            }\n";
                    ss << "            sqlite3_finalize(stmt);\n";
                    ss << "        }\n";
                    ss << "        releaseSQLiteConn(db);\n";
                    ss << "        return found;\n";
                } else if (dbType == "postgres" || dbType == "postgresql") {
                    ss << "        PGconn* conn = getPGConn();\n";
                    ss << "        if (!conn) return findUser_JSONL(emailVal, user);\n";
                    ss << "        const char* paramValues[1];\n";
                    ss << "        paramValues[0] = emailVal.c_str();\n";
                    ss << "        std::string query = \"SELECT * FROM \\\"" << slice->name << "\\\" WHERE \\\"" << emailField << "\\\" = $1;\";\n";
                    ss << "        PGresult* res = PQexecParams(conn, query.c_str(), 1, NULL, paramValues, NULL, NULL, 0);\n";
                    ss << "        bool found = false;\n";
                    ss << "        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {\n";
                    for (size_t i = 0; i < slice->fields.size(); ++i) {
                        const auto& f = slice->fields[i];
                        ss << "            {\n";
                        ss << "                int colIdx = PQfnumber(res, \"" << f->name << "\");\n";
                        ss << "                if (!PQgetisnull(res, 0, colIdx)) {\n";
                        ss << "                    std::string val = PQgetvalue(res, 0, colIdx);\n";
                        if (f->type == DataType::STRING) {
                            ss << "                    user." << f->name << " = val;\n";
                        } else if (f->type == DataType::INT || f->type == DataType::RELATION) {
                            ss << "                    user." << f->name << " = safeStoi(val);\n";
                        } else if (f->type == DataType::FLOAT) {
                            ss << "                    user." << f->name << " = safeStod(val);\n";
                        } else if (f->type == DataType::BOOL) {
                            ss << "                    user." << f->name << " = (val == \"t\" || val == \"true\" || val == \"1\");\n";
                        }
                        ss << "                }\n";
                        ss << "            }\n";
                    }
                    ss << "            found = true;\n";
                    ss << "        }\n";
                    ss << "        PQclear(res);\n";
                    ss << "        releasePGConn(conn);\n";
                    ss << "        return found;\n";
                } else if (dbType == "mysql") {
                    ss << "        MYSQL* conn = getMySQLConn();\n";
                    ss << "        if (!conn) return findUser_JSONL(emailVal, user);\n";
                    ss << "        char* escaped = new char[emailVal.length() * 2 + 1];\n";
                    ss << "        mysql_real_escape_string(conn, escaped, emailVal.c_str(), emailVal.length());\n";
                    ss << "        std::string query = \"SELECT * FROM `\" + std::string(\"" << slice->name << "\") + \"` WHERE `\" + std::string(\"" << emailField << "\") + \"` = '\" + std::string(escaped) + \"';\";\n";
                    ss << "        delete[] escaped;\n";
                    ss << "        bool found = false;\n";
                    ss << "        if (mysql_query(conn, query.c_str()) == 0) {\n";
                    ss << "            MYSQL_RES* result = mysql_store_result(conn);\n";
                    ss << "            if (result && mysql_num_rows(result) > 0) {\n";
                    ss << "                MYSQL_ROW row = mysql_fetch_row(result);\n";
                    ss << "                int num_fields = mysql_num_fields(result);\n";
                    ss << "                MYSQL_FIELD* fields = mysql_fetch_fields(result);\n";
                    for (size_t i = 0; i < slice->fields.size(); ++i) {
                        const auto& f = slice->fields[i];
                        ss << "                {\n";
                        ss << "                    int colIdx = -1;\n";
                        ss << "                    for (int k = 0; k < num_fields; ++k) {\n";
                        ss << "                        if (std::string(fields[k].name) == \"" << f->name << "\") { colIdx = k; break; }\n";
                        ss << "                    }\n";
                        ss << "                    if (colIdx != -1 && row[colIdx] != nullptr) {\n";
                        if (f->type == DataType::STRING) {
                            ss << "                        user." << f->name << " = row[colIdx];\n";
                        } else if (f->type == DataType::INT || f->type == DataType::RELATION) {
                            ss << "                        user." << f->name << " = safeStoi(row[colIdx]);\n";
                        } else if (f->type == DataType::FLOAT) {
                            ss << "                        user." << f->name << " = safeStod(row[colIdx]);\n";
                        } else if (f->type == DataType::BOOL) {
                            ss << "                        user." << f->name << " = (std::string(row[colIdx]) == \"1\" || std::string(row[colIdx]) == \"true\");\n";
                        }
                        ss << "                    }\n";
                        ss << "                }\n";
                    }
                    ss << "                found = true;\n";
                    ss << "                mysql_free_result(result);\n";
                    ss << "            }\n";
                    ss << "        }\n";
                    ss << "        releaseMySQLConn(conn);\n";
                    ss << "        return found;\n";
                } else {
                    ss << "        return findUser_JSONL(emailVal, user);\n";
                }
                ss << "    }\n\n";
            }
        }
    }

    for (const auto& action : slice->actions) {
        ss << "    void " << action->name << "();\n";
    }

    ss << "};\n";
    return ss.str();
}

std::string CodeGenerator::generateJob(std::shared_ptr<ASTJob> job) {
    std::stringstream ss;
    ss << "struct Job_" << job->name << " {\n";
    for (const auto& field : job->fields) {
        ss << "    " << dataTypeToString(field->type) << " " << field->name << ";\n";
    }
    ss << "\n";
    for (const auto& action : job->actions) {
        ss << "    void " << action->name << "();\n";
    }
    ss << "};\n";
    return ss.str();
}

std::string CodeGenerator::generateHTMLContent(std::shared_ptr<ASTView> view) {
    bool hasHtml = false;
    for (const auto& elem : view->elements) {
        if (elem->type == "html") {
            hasHtml = true;
            break;
        }
    }
    std::string customStyle = "";
    if (std::filesystem::exists("style.css")) {
        std::ifstream f("style.css");
        if (f.is_open()) {
            std::stringstream buffer;
            buffer << f.rdbuf();
            customStyle = buffer.str();
            f.close();
        }
    }
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
       << "    <style>\n";
    if (program->css != "none") {
        ss << "        :root {\n"
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
           << "        .data-table td { font-size: 0.95rem; }\n";
    }
    if (!customStyle.empty()) {
        ss << "        /* Custom Styles Overrides */\n" << customStyle << "\n";
    }
    ss << "    </style>\n"
       << "</head>\n"
       << "<body>\n";

    if (program->css != "none") {
        ss << "    <main class=\"container\">\n"
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
    } else {
        ss << "            <div id=\"hexa-root\">\n";
    }

    for (const auto& elem : view->elements) {
        if (elem->type == "input") {
            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " class=\"" + elem->className + "\"";
                ss << "                <div" << cls << ">\n";
                ss << "                    <label" << cls << ">" << elem->label << "</label>\n";
                ss << "                    <input type=\"text\"" << cls << " id=\"input-" << elem->name << "\" name=\"" << elem->name << "\">\n";
                ss << "                </div>\n";
            } else {
                ss << "                <div class=\"form-group\">\n";
                ss << "                    <label class=\"form-label\">" << elem->label << "</label>\n";
                ss << "                    <input type=\"text\" class=\"form-input\" id=\"input-" << elem->name << "\" name=\"" << elem->name << "\">\n";
                ss << "                </div>\n";
            }
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

            std::string clickAttr = "";
            if (isNavigationView) {
                clickAttr = "onclick=\"window.location.href = '/" + viewTarget + "'\"";
            } else {
                std::string apiEndpoint = "/execute";
                if (!program->apis.empty()) {
                    for (const auto& r : program->allRoutes()) {
                        if (r->targetAction == elem->targetAction) {
                            apiEndpoint = r->path;
                            break;
                        }
                    }
                }
                clickAttr = "onclick=\"triggerAction('" + apiEndpoint + "')\"";
            }

            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " class=\"" + elem->className + "\"";
                ss << "                <button" << cls << " " << clickAttr << ">" << elem->label << "</button>\n";
            } else {
                ss << "                <button class=\"btn\" " << clickAttr << ">" << elem->label << "</button>\n";
            }
        } else if (elem->type == "table") {
            // Add action column if there is delete route
            bool hasDeleteRoute = false;
            std::string deleteEndpoint = "";
            if (!program->apis.empty()) {
                for (const auto& r : program->allRoutes()) {
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

            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " class=\"" + elem->className + "\"";
                ss << "                <div" << cls << ">\n";
                ss << "                    <table>\n";
                ss << "                        <thead>\n";
                ss << "                            <tr>\n";
                for (const auto& col : elem->columns) {
                    ss << "                                <th>" << col << "</th>\n";
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
            } else {
                ss << "                <div class=\"table-container\">\n";
                ss << "                    <table class=\"data-table\">\n";
                ss << "                        <thead>\n";
                ss << "                            <tr>\n";
                for (const auto& col : elem->columns) {
                    ss << "                                <th>" << col << "</th>\n";
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
        } else if (elem->type == "html") {
            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " class=\"" + elem->className + "\"";
                ss << "                <div" << cls << ">" << elem->label << "</div>\n";
            } else {
                ss << elem->label << "\n";
            }
        }
    }

    ss << "            </div>\n";
    if (program->css != "none") {
        ss << "            <div class=\"result-panel\" id=\"result-panel\">\n"
           << "                <div class=\"result-title\" id=\"result-title\">Respuesta de la API C++</div>\n"
           << "                <pre class=\"result-code\"><code id=\"result-code\"></code></pre>\n"
           << "            </div>\n"
           << "        </section>\n"
           << "    </main>\n";
    } else {
        ss << "            <div id=\"result-panel\" style=\"display:none;\">\n"
           << "                <div id=\"result-title\">Respuesta de la API C++</div>\n"
           << "                <pre><code id=\"result-code\"></code></pre>\n"
           << "            </div>\n";
    }
    ss << "    <script>\n";

    // Refresh dynamic tables script
    ss << "        async function refreshTables() {\n";
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            bool hasDeleteRoute = false;
            std::string deleteEndpoint = "";
            if (!program->apis.empty()) {
                for (const auto& r : program->allRoutes()) {
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
    ss << "                        'Authorization': 'Bearer ' + (localStorage.getItem('hexagen_token') || 'hexagen_token_123')\n";
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
       << "                        'Authorization': 'Bearer ' + (localStorage.getItem('hexagen_token') || 'hexagen_token_123')\n"
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
    
    ss << "        // LiveView Client-side Script (Phase 2)\n"
       << "        const liveSocket = new WebSocket('ws://' + window.location.host + '/live');\n"
       << "        liveSocket.onmessage = function(event) {\n"
       << "            try {\n"
       << "                const msg = JSON.parse(event.data);\n"
       << "                if (msg.type === 'patch' && Array.isArray(msg.patches)) {\n"
       << "                    // LiveView DOM patching: apply minimal server-computed diffs\n"
       << "                    msg.patches.forEach(p => {\n"
       << "                        const el = document.querySelector('[hg-id=\"' + p.id + '\"]');\n"
       << "                        if (el && el.innerHTML !== p.html) el.innerHTML = p.html;\n"
       << "                    });\n"
       << "                } else if (msg.event === 'action') {\n"
       << "                    refreshTables();\n"
       << "                } else if (msg.event === 'input_change') {\n"
       << "                    const input = document.getElementById('input-' + msg.field);\n"
       << "                    if (input && input.value !== msg.value) {\n"
       << "                        input.value = msg.value;\n"
       << "                    }\n"
       << "                }\n"
       << "            } catch(e) {}\n"
       << "        };\n"
       << "        function setupLiveEvents() {\n"
       << "            document.querySelectorAll('.form-input').forEach(input => {\n"
       << "                input.addEventListener('input', () => {\n"
       << "                    liveSocket.send(JSON.stringify({\n"
       << "                        event: 'input_change',\n"
       << "                        field: input.name,\n"
       << "                        value: input.value\n"
       << "                    }));\n"
       << "                });\n"
       << "            });\n"
       << "        }\n"
       << "        liveSocket.onopen = setupLiveEvents;\n\n";
     
    ss << "        window.onload = () => { refreshTables(); setupLiveEvents(); };\n"
       << "    </script>\n"
       << "</body>\n"
       << "</html>\n";

    return ss.str();
}

void CodeGenerator::generateReactFrontend() {
    namespace fs = std::filesystem;
    try {
        fs::create_directories("frontend/src/pages");
    } catch (...) {
        std::cerr << "Error creating directory frontend/src/pages" << std::endl;
        return;
    }

    // package.json
    {
        std::ofstream file("frontend/package.json");
        if (file.is_open()) {
            file << R"JSON({
  "name": "hexagen-frontend",
  "private": true,
  "version": "0.0.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "preview": "vite preview"
  },
  "dependencies": {
    "react": "^18.2.0",
    "react-dom": "^18.2.0",
    "react-router-dom": "^6.22.3"
  },
  "devDependencies": {
    "@types/react": "^18.2.66",
    "@types/react-dom": "^18.2.22",
    "@vitejs/plugin-react": "^4.2.1",
    "autoprefixer": "^10.4.19",
    "postcss": "^8.4.38",
    "tailwindcss": "^3.4.1",
    "typescript": "^5.2.2",
    "vite": "^5.2.0"
  }
})JSON";
            file.close();
        }
    }

    // tsconfig.json
    {
        std::ofstream file("frontend/tsconfig.json");
        if (file.is_open()) {
            file << R"JSON({
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "lib": ["DOM", "DOM.Iterable", "ES2020"],
    "module": "ESNext",
    "skipLibCheck": true,
    "moduleResolution": "bundler",
    "allowImportingTsExtensions": true,
    "resolveJsonModule": true,
    "isolatedModules": true,
    "noEmit": true,
    "jsx": "react-jsx",
    "strict": true,
    "noUnusedLocals": false,
    "noUnusedParameters": false,
    "noFallthroughCasesInSwitch": true
  },
  "include": ["src"]
})JSON";
            file.close();
        }
    }

    // vite.config.ts
    {
        std::ofstream file("frontend/vite.config.ts");
        if (file.is_open()) {
            file << R"TS(import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 3000,
    proxy: {
      '/api': 'http://localhost:8080'
    }
  }
});
)TS";
            file.close();
        }
    }

    // tailwind.config.js
    {
        std::ofstream file("frontend/tailwind.config.js");
        if (file.is_open()) {
            file << R"JS(/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {},
  },
  plugins: [],
}
)JS";
            file.close();
        }
    }

    // postcss.config.js
    {
        std::ofstream file("frontend/postcss.config.js");
        if (file.is_open()) {
            file << R"JS(export default {
  plugins: {
    tailwindcss: {},
    autoprefixer: {},
  },
}
)JS";
            file.close();
        }
    }

    // index.html
    {
        std::ofstream file("frontend/index.html");
        if (file.is_open()) {
            file << R"HTML(<!DOCTYPE html>
<html lang="es">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Hexagen React App</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/main.tsx"></script>
  </body>
</html>
)HTML";
            file.close();
        }
    }

    // src/main.tsx
    {
        std::ofstream file("frontend/src/main.tsx");
        if (file.is_open()) {
            file << R"TS(import React from 'react';
import ReactDOM from 'react-dom/client';
import App from './App';
import './index.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
)TS";
            file.close();
        }
    }

    // src/index.css
    {
        std::ofstream file("frontend/src/index.css");
        if (file.is_open()) {
            file << R"CSS(@tailwind base;
@tailwind components;
@tailwind utilities;

:root {
    --bg-color: #0b0f19;
    --card-bg: rgba(20, 30, 55, 0.45);
    --border-color: rgba(255, 255, 255, 0.08);
    --primary-glow: #00f2fe;
    --secondary-glow: #4facfe;
    --text-color: #f3f4f6;
    --text-muted: #9ca3af;
}
)CSS";
            // Check if local style.css exists in the directory where hf was run, and merge it
            if (std::filesystem::exists("style.css")) {
                std::ifstream custom("style.css");
                if (custom.is_open()) {
                    file << "\n/* Custom Styles Overrides */\n";
                    file << custom.rdbuf();
                    custom.close();
                }
            }
            file.close();
        }
    }

    // src/App.tsx
    {
        std::ofstream file("frontend/src/App.tsx");
        if (file.is_open()) {
            file << "import { BrowserRouter as Router, Routes, Route } from 'react-router-dom';\n";
            for (const auto& view : program->views) {
                file << "import " << view->name << " from './pages/" << view->name << "';\n";
            }
            file << "\nexport default function App() {\n";
            file << "    return (\n";
            file << "        <Router>\n";
            file << "            <Routes>\n";
            if (!program->views.empty()) {
                file << "                <Route path=\"/\" element={<" << program->views[0]->name << " />} />\n";
            }
            for (const auto& view : program->views) {
                std::string pathLower = view->name;
                std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);
                file << "                <Route path=\"/" << pathLower << "\" element={<" << view->name << " />} />\n";
            }
            file << "            </Routes>\n";
            file << "        </Router>\n";
            file << "    );\n";
            file << "}\n";
            file.close();
        }
    }

    // Generate each view
    for (const auto& view : program->views) {
        generateReactPage(view);
    }

    std::cerr << "[Hexagen] Building React frontend..." << std::endl;
    if (!fs::exists("frontend/node_modules")) {
        std::cerr << "[Hexagen] Installing frontend dependencies (npm install)..." << std::endl;
        std::system("cd frontend && npm install >&2");
    }
    std::system("cd frontend && npm run build >&2");
}

void CodeGenerator::generateReactPage(std::shared_ptr<ASTView> view) {
    std::ofstream file("frontend/src/pages/" + view->name + ".tsx");
    if (!file.is_open()) return;

    file << "import React, { useState, useEffect } from 'react';\n";
    file << "import { useNavigate } from 'react-router-dom';\n\n";
    file << "export default function " << view->name << "() {\n";
    file << "    const navigate = useNavigate();\n";

    // Detect inputs and create state variables
    for (const auto& elem : view->elements) {
        if (elem->type == "input") {
            file << "    const [" << elem->name << ", set" << elem->name << "] = useState('');\n";
        }
    }

    // Detect tables and create state variables and fetch triggers
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            file << "    const [" << elem->label << "Rows, set" << elem->label << "Rows] = useState<any[]>([]);\n";
            file << "    const refresh" << elem->label << " = async () => {\n";
            file << "        try {\n";
            file << "            const res = await fetch('/api/" << elem->label << "');\n";
            file << "            const data = await res.json();\n";
            file << "            set" << elem->label << "Rows(data);\n";
            file << "        } catch(err) {}\n";
            file << "    };\n";
        }
    }

    // Global result block state
    file << "    const [result, setResult] = useState<any>(null);\n\n";

    // useEffect to populate tables on mount
    file << "    useEffect(() => {\n";
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            file << "        refresh" << elem->label << "();\n";
        }
    }
    file << "    }, []);\n\n";

    // Handle action click functions
    for (const auto& elem : view->elements) {
        if (elem->type == "button") {
            size_t dotPos = elem->targetAction.find('.');
            if (dotPos != std::string::npos) {
                std::string sliceName = elem->targetAction.substr(0, dotPos);
                std::string actionName = elem->targetAction.substr(dotPos + 1);
                
                file << "    const handle" << sliceName << "_" << actionName << " = async () => {\n";
                file << "        const payload = {\n";
                for (const auto& inputElem : view->elements) {
                    if (inputElem->type == "input") {
                        bool isNum = false;
                        for (const auto& s : program->slices) {
                            if (s->name == sliceName) {
                                for (const auto& f : s->fields) {
                                    if (f->name == inputElem->name && (f->type == DataType::INT || f->type == DataType::FLOAT || f->type == DataType::RELATION)) {
                                        isNum = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (isNum) {
                            file << "            " << inputElem->name << ": Number(" << inputElem->name << ") || 0,\n";
                        } else {
                            file << "            " << inputElem->name << ": " << inputElem->name << ",\n";
                        }
                    }
                }
                file << "        };\n";
                file << "        try {\n";
                file << "            const res = await fetch('/api/" << sliceName << "', {\n";
                file << "                method: 'POST',\n";
                file << "                headers: { 'Content-Type': 'application/json' },\n";
                file << "                body: JSON.stringify(payload)\n";
                file << "            });\n";
                file << "            const data = await res.json();\n";
                file << "            setResult(data);\n";
                for (const auto& tbl : view->elements) {
                    if (tbl->type == "table") {
                        file << "            refresh" << tbl->label << "();\n";
                    }
                }
                file << "        } catch(err) {}\n";
                file << "    };\n\n";
            }
        }
    }

    // Delete actions
    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            bool hasDeleteRoute = false;
            std::string deleteEndpoint = "";
            if (!program->apis.empty()) {
                for (const auto& r : program->allRoutes()) {
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
                std::string keyCol = elem->columns.empty() ? "id" : elem->columns[0];
                file << "    const handleDelete_" << elem->label << " = async (idVal: any) => {\n";
                file << "        if (!confirm('¿Seguro que deseas eliminar este registro?')) return;\n";
                file << "        try {\n";
                file << "            const res = await fetch('/api/" << elem->label << "', {\n";
                file << "                method: 'DELETE',\n";
                file << "                headers: { 'Content-Type': 'application/json' },\n";
                file << "                body: JSON.stringify({ " << keyCol << ": idVal })\n";
                file << "            });\n";
                file << "            refresh" << elem->label << "();\n";
                file << "        } catch(err) {}\n";
                file << "    };\n\n";
            }
        }
    }    // Render component
    file << "    return (\n";
    if (program->css == "none") {
        file << "        <>\n";
        for (const auto& elem : view->elements) {
            if (elem->type == "input") {
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "            <div className=\"max-w-md mx-auto mb-4\">\n";
                file << "                <label className=\"block text-xs font-semibold uppercase text-gray-400 mb-2\">" << elem->name << "</label>\n";
                file << "                <input type=\"text\" value={" << elem->name << "} onChange={(e) => set" << elem->name << "(e.target.value)}" << cls << " />\n";
                file << "            </div>\n";
            } else if (elem->type == "button") {
                size_t dotPos = elem->targetAction.find('.');
                std::string clickHandler = "";
                if (dotPos != std::string::npos) {
                    std::string sliceName = elem->targetAction.substr(0, dotPos);
                    std::string actionName = elem->targetAction.substr(dotPos + 1);
                    clickHandler = "handle" + sliceName + "_" + actionName;
                } else {
                    std::string pathLower = elem->targetAction;
                    std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);
                    clickHandler = "() => navigate('/" + pathLower + "')";
                }
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "            <div className=\"max-w-md mx-auto mb-4\">\n";
                file << "                <button onClick={" << clickHandler << "}" << cls << ">" << elem->label << "</button>\n";
                file << "            </div>\n";
            } else if (elem->type == "html") {
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "            <div" << cls << " dangerouslySetInnerHTML={{ __html: `" << elem->label << "` }} />\n";
            }
        }
        
        file << "            {result && (\n";
        file << "                <div>\n";
        file << "                    <div>API Response</div>\n";
        file << "                    <pre>{JSON.stringify(result, null, 2)}</pre>\n";
        file << "                </div>\n";
        file << "            )}\n";
    } else {
        file << "        <div className=\"min-h-screen bg-[var(--bg-color)] text-[var(--text-color)] flex flex-col justify-center items-center relative overflow-hidden font-sans\">\n";
        file << "            <div className=\"absolute w-[300px] h-[300px] bg-gradient-to-r from-[var(--primary-glow)] to-transparent rounded-full top-[10%] left-[15%] opacity-15 blur-[80px]\" />\n";
        file << "            <div className=\"absolute w-[350px] h-[350px] bg-gradient-to-r from-[var(--secondary-glow)] to-transparent rounded-full bottom-[15%] right-[15%] opacity-15 blur-[80px]\" />\n";
        file << "            \n";
        file << "            <main className=\"w-full max-w-[550px] p-8 z-10\">\n";
        file << "                <section className=\"bg-[var(--card-bg)] backdrop-blur-[20px] border border-[var(--border-color)] rounded-[24px] p-10 shadow-[0_20px_50px_rgba(0,0,0,0.3)]\">\n";
        file << "                    <div className=\"text-center mb-8\">\n";
        
        std::string mainTitle = view->name;
        std::string subTitle = "Hexagen Compiled UI";
        for (const auto& elem : view->elements) {
            if (elem->type == "title") {
                subTitle = elem->label;
            }
        }
        file << "                        <h1 className=\"text-3xl font-extrabold bg-gradient-to-r from-white to-[var(--primary-glow)] bg-clip-text text-transparent mb-2\">" << mainTitle << "</h1>\n";
        file << "                        <p className=\"text-sm text-gray-400\">" << subTitle << "</p>\n";
        file << "                    </div>\n";
        
        file << "                    <div className=\"space-y-6\">\n";

        for (const auto& elem : view->elements) {
            if (elem->type == "input") {
                std::string cls = elem->className.empty() ? "" : " " + elem->className;
                file << "                        <div>\n";
                file << "                            <label className=\"block text-xs font-semibold uppercase text-gray-400 mb-2\">" << elem->name << "</label>\n";
                file << "                            <input type=\"text\" value={" << elem->name << "} onChange={(e) => set" << elem->name << "(e.target.value)} className=\"w-full bg-white/5 border border-[var(--border-color)] rounded-xl px-4 py-3 text-white focus:outline-none focus:border-[var(--primary-glow)] focus:bg-white/10 transition" << cls << "\" />\n";
                file << "                        </div>\n";
            } else if (elem->type == "button") {
                size_t dotPos = elem->targetAction.find('.');
                std::string cls = elem->className.empty() ? "" : " " + elem->className;
                if (dotPos != std::string::npos) {
                    std::string sliceName = elem->targetAction.substr(0, dotPos);
                    std::string actionName = elem->targetAction.substr(dotPos + 1);
                    file << "                        <button onClick={handle" << sliceName << "_" << actionName << "} className=\"w-full bg-gradient-to-r from-[var(--secondary-glow)] to-[var(--primary-glow)] text-[var(--bg-color)] py-4 rounded-xl font-bold hover:scale-[1.02] transition active:scale-[0.98]" << cls << "\">" << elem->label << "</button>\n";
                } else {
                    std::string pathLower = elem->targetAction;
                    std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);
                    file << "                        <button onClick={() => navigate('/" << pathLower << "')} className=\"w-full bg-white/5 border border-[var(--border-color)] py-4 rounded-xl font-bold hover:bg-white/10 transition" << cls << "\">" << elem->label << "</button>\n";
                }
            } else if (elem->type == "html") {
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "                        <div" << cls << " dangerouslySetInnerHTML={{ __html: `" << elem->label << "` }} />\n";
            }
        }
        
        file << "                    </div>\n";

        file << "                    {result && (\n";
        file << "                        <div className=\"mt-8 bg-black/25 rounded-xl border border-white/5 p-5\">\n";
        file << "                            <div className=\"text-xs font-semibold text-[var(--primary-glow)] mb-2 uppercase\">API Response</div>\n";
        file << "                            <pre className=\"font-mono text-xs text-[#e5e7eb] overflow-x-auto\">{JSON.stringify(result, null, 2)}</pre>\n";
        file << "                        </div>\n";
        file << "                    )}\n";
    }

    for (const auto& elem : view->elements) {
        if (elem->type == "table") {
            bool hasDeleteRoute = false;
            if (!program->apis.empty()) {
                for (const auto& r : program->allRoutes()) {
                    if (r->method == "DELETE") {
                        size_t dotPos = r->targetAction.find('.');
                        std::string targetSlice = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                        if (targetSlice == elem->label) {
                            hasDeleteRoute = true;
                            break;
                        }
                    }
                }
            }

            if (program->css == "none") {
                std::string cls = elem->className.empty() ? "" : " className=\"" + elem->className + "\"";
                file << "                    <div" << cls << ">\n";
                file << "                        <table>\n";
                file << "                            <thead>\n";
                file << "                                <tr>\n";
                for (const auto& col : elem->columns) {
                    file << "                                    <th>" << col << "</th>\n";
                }
                if (hasDeleteRoute) {
                    file << "                                    <th>Acciones</th>\n";
                }
                file << "                                </tr>\n";
                file << "                            </thead>\n";
                file << "                            <tbody>\n";
                file << "                                {" << elem->label << "Rows.map((row: any, idx: number) => (\n";
                file << "                                    <tr key={idx}>\n";
                for (const auto& col : elem->columns) {
                    file << "                                        <td>{row." << col << "}</td>\n";
                }
                if (hasDeleteRoute) {
                    std::string keyCol = elem->columns.empty() ? "id" : elem->columns[0];
                    file << "                                        <td>\n";
                    file << "                                            <button onClick={() => handleDelete_" << elem->label << "(row." << keyCol << ")}>Eliminar</button>\n";
                    file << "                                        </td>\n";
                }
                file << "                                    </tr>\n";
                file << "                                ))}\n";
                file << "                            </tbody>\n";
                file << "                        </table>\n";
                file << "                    </div>\n";
            } else {
                std::string cls = elem->className.empty() ? "" : " " + elem->className;
                file << "                    <div className=\"mt-8 bg-black/20 rounded-xl border border-white/10 overflow-hidden" << cls << "\">\n";
                file << "                        <table className=\"w-full text-left border-collapse\">\n";
                file << "                            <thead>\n";
                file << "                                <tr className=\"bg-white/5 text-xs font-semibold text-gray-400 uppercase\">\n";
                for (const auto& col : elem->columns) {
                    file << "                                    <th className=\"p-3\">" << col << "</th>\n";
                }
                if (hasDeleteRoute) {
                    file << "                                    <th className=\"p-3\">Acciones</th>\n";
                }
                file << "                                </tr>\n";
                file << "                            </thead>\n";
                file << "                            <tbody>\n";
                file << "                                {" << elem->label << "Rows.map((row: any, idx: number) => (\n";
                file << "                                    <tr key={idx} className=\"border-b border-white/10\">\n";
                for (const auto& col : elem->columns) {
                    file << "                                        <td className=\"p-3 text-sm\">{row." << col << "}</td>\n";
                }
                if (hasDeleteRoute) {
                    std::string keyCol = elem->columns.empty() ? "id" : elem->columns[0];
                    file << "                                        <td className=\"p-3 text-sm\">\n";
                    file << "                                            <button onClick={() => handleDelete_" << elem->label << "(row." << keyCol << ")} className=\"px-3 py-1 text-xs font-semibold rounded bg-gradient-to-r from-red-500 to-rose-600 text-white hover:scale-105 active:scale-95 transition\">Eliminar</button>\n";
                    file << "                                        </td>\n";
                }
                file << "                                    </tr>\n";
                file << "                                ))}\n";
                file << "                            </tbody>\n";
                file << "                        </table>\n";
                file << "                    </div>\n";
            }
        }
    }

    if (program->css == "none") {
        file << "        </>\n";
    } else {
        file << "                </section>\n";
        file << "            </main>\n";
        file << "        </div>\n";
    }
    file << "    );\n";
    file << "}\n";
}

std::string CodeGenerator::generateSourceCode(bool includeMain) {
    if (program->target == "desktop") {
        std::ofstream wvFile("webview.h");
        if (wvFile.is_open()) {
            wvFile << WEBVIEW_H_CONTENT;
            wvFile.close();
        }
    }
    if (program->frontend == "react") {
        generateReactFrontend();
    }
    bool hasCors = false;
    bool hasRateLimit = false;
    bool hasLogger = false;
    int rateLimitLimit = 0;
    int rateLimitWindow = 0;
    if (!program->apis.empty()) {
        for (const auto& mw : program->allMiddlewares()) {
            if (mw->name == "cors") {
                hasCors = true;
            } else if (mw->name == "logger") {
                hasLogger = true;
            } else if (mw->name == "rate_limit") {
                hasRateLimit = true;
                if (mw->arguments.size() >= 2) {
                    try {
                        rateLimitLimit = std::stoi(mw->arguments[0]);
                        rateLimitWindow = std::stoi(mw->arguments[1]);
                    } catch (...) {}
                }
            }
        }
    }

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
    ss << "#include <mutex>\n";
    ss << "#include <set>\n";
    ss << "#include <chrono>\n";
    ss << "#include <sys/socket.h>\n";
    ss << "#include <netinet/in.h>\n";
    ss << "#include <unistd.h>\n";
    ss << "#include <cstring>\n";
    ss << "#include <fstream>\n";
    ss << "#include <iomanip>\n";
    ss << "#include <sys/stat.h>\n";
    ss << "#include <sys/types.h>\n";
    ss << "#include <functional>\n";
    ss << "#include <queue>\n";
    ss << "#include <unordered_map>\n";
    ss << "#include <map>\n";
    ss << "#include <random>\n";
    ss << "#include <cstdlib>\n";
    ss << "#include <ctime>\n";
    ss << "#include <atomic>\n";
    ss << "#include <arpa/inet.h>\n";
    ss << "#include <condition_variable>\n";
    ss << "#include <coroutine>\n";
    ss << "\n// C++20 Coroutine Async server components\n"
       << "struct Task {\n"
       << "    struct promise_type {\n"
       << "        Task get_return_object() { return {}; }\n"
       << "        std::suspend_never initial_suspend() { return {}; }\n"
       << "        std::suspend_never final_suspend() noexcept { return {}; }\n"
       << "        void return_void() {}\n"
       << "        void unhandled_exception() {}\n"
       << "    };\n"
       << "};\n\n"
       << "struct AsyncRead {\n"
       << "    int fd;\n"
       << "    std::string& out_req;\n"
       << "    bool await_ready() noexcept { return false; }\n"
       << "    void await_suspend(std::coroutine_handle<> h) noexcept {\n"
       << "        std::thread([this, h]() {\n"
       << "            char buffer[4096];\n"
       << "            std::string req;\n"
       << "            while (true) {\n"
       << "                int valread = recv(fd, buffer, sizeof(buffer) - 1, 0);\n"
       << "                if (valread <= 0) break;\n"
       << "                buffer[valread] = '\\0';\n"
       << "                req += buffer;\n"
       << "                if (req.find(\"\\r\\n\\r\\n\") != std::string::npos) break;\n"
       << "                if (req.find(\"Content-Length:\") != std::string::npos) {\n"
       << "                    size_t pos = req.find(\"Content-Length:\");\n"
       << "                    size_t end = req.find(\"\\r\\n\", pos);\n"
       << "                    if (end != std::string::npos) {\n"
       << "                        std::string lenStr = req.substr(pos + 15, end - (pos + 15));\n"
       << "                        size_t len = std::stoul(lenStr);\n"
       << "                        size_t bodyPos = req.find(\"\\r\\n\\r\\n\");\n"
       << "                        if (bodyPos != std::string::npos && req.length() >= bodyPos + 4 + len) {\n"
       << "                            break;\n"
       << "                        }\n"
       << "                    }\n"
       << "                }\n"
       << "            }\n"
       << "            out_req = req;\n"
       << "            h.resume();\n"
       << "        }).detach();\n"
       << "    }\n"
       << "    void await_resume() noexcept {}\n"
       << "};\n\n";
    if (program->target == "desktop") {
        ss << "#include \"webview.h\"\n";
    }
    if (std::filesystem::exists(".hexagen_modules")) {
        ss << "// Auto-included Hexagen Modules\n";
        for (const auto& entry : std::filesystem::recursive_directory_iterator(".hexagen_modules")) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                if (ext == ".h" || ext == ".hpp") {
                    ss << "#include \"" << entry.path().filename().string() << "\"\n";
                }
            }
        }
    }
    ss << "\n";

    ss << "// Asynchronous Job Queue + durability + retry + supervision (OTP/Oban-style)\n"
       << "std::string base64_encode(const std::string& in);\n"
       << "std::string base64_decode(const std::string& in);\n"
       << "std::string getJSONVal(const std::string& json, const std::string& field);\n"
       << "void dispatch_job(const std::string& name, const std::string& argsJson);\n\n"
       << "struct PendingJob {\n"
       << "    std::string id;\n"
       << "    std::string name;\n"
       << "    std::string args;\n"
       << "    int attempts = 0;\n"
       << "    std::function<void()> run;\n"
       << "};\n"
       << "std::queue<PendingJob> global_job_queue;\n"
       << "std::mutex global_job_queue_mutex;\n"
       << "std::condition_variable global_job_queue_cv;\n"
       << "std::mutex jobs_file_mutex;\n"
       << "static const char* JOBS_FILE = \"jobs_pending.jsonl\";\n\n"
       << "std::string makeJobId() {\n"
       << "    static long counter = 0;\n"
       << "    std::lock_guard<std::mutex> lk(jobs_file_mutex);\n"
       << "    long n = ++counter;\n"
       << "    auto now = std::chrono::steady_clock::now().time_since_epoch().count();\n"
       << "    return std::to_string(now) + \"-\" + std::to_string(n);\n"
       << "}\n\n"
       << "void persistJob(const std::string& id, const std::string& name, const std::string& args, int attempts) {\n"
       << "    std::lock_guard<std::mutex> lk(jobs_file_mutex);\n"
       << "    std::ofstream f(JOBS_FILE, std::ios::app);\n"
       << "    if (f.is_open()) f << \"{\\\"id\\\":\\\"\" << id << \"\\\",\\\"name\\\":\\\"\" << name\n"
       << "                        << \"\\\",\\\"args\\\":\\\"\" << base64_encode(args) << \"\\\",\\\"attempts\\\":\" << attempts << \"}\\n\";\n"
       << "}\n\n"
       << "void removeJob(const std::string& id) {\n"
       << "    std::lock_guard<std::mutex> lk(jobs_file_mutex);\n"
       << "    std::ifstream in(JOBS_FILE);\n"
       << "    std::vector<std::string> keep; std::string line;\n"
       << "    while (std::getline(in, line)) { if (line.empty()) continue; if (getJSONVal(line, \"id\") != id) keep.push_back(line); }\n"
       << "    in.close();\n"
       << "    std::ofstream out(JOBS_FILE, std::ios::trunc);\n"
       << "    for (auto& l : keep) out << l << \"\\n\";\n"
       << "}\n\n"
       << "void enqueue_persisted_job(const std::string& name, const std::string& args, std::function<void()> run) {\n"
       << "    std::string id = makeJobId();\n"
       << "    persistJob(id, name, args, 0);\n"
       << "    { std::lock_guard<std::mutex> lock(global_job_queue_mutex); global_job_queue.push({id, name, args, 0, run}); }\n"
       << "    global_job_queue_cv.notify_one();\n"
       << "}\n\n"
       << "void enqueue_background_task(std::function<void()> task) {\n"
       << "    { std::lock_guard<std::mutex> lock(global_job_queue_mutex); global_job_queue.push({\"\", \"\", \"\", 0, task}); }\n"
       << "    global_job_queue_cv.notify_one();\n"
       << "}\n\n"
       << "void run_job_worker() {\n"
       << "    const int maxAttempts = 3;\n"
       << "    while (true) {\n"
       << "        PendingJob job;\n"
       << "        {\n"
       << "            std::unique_lock<std::mutex> lock(global_job_queue_mutex);\n"
       << "            global_job_queue_cv.wait(lock, [] { return !global_job_queue.empty(); });\n"
       << "            job = global_job_queue.front();\n"
       << "            global_job_queue.pop();\n"
       << "        }\n"
       << "        if (!job.run) continue;\n"
       << "        bool ok = false;\n"
       << "        for (int attempt = job.attempts + 1; attempt <= maxAttempts && !ok; ++attempt) {\n"
       << "            try { job.run(); ok = true; }\n"
       << "            catch (const std::exception& e) {\n"
       << "                std::cerr << \"[Job] '\" << job.name << \"' attempt \" << attempt << \"/\" << maxAttempts << \" failed: \" << e.what() << std::endl;\n"
       << "                if (attempt < maxAttempts) std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));\n"
       << "            }\n"
       << "            catch (...) {\n"
       << "                std::cerr << \"[Job] '\" << job.name << \"' attempt \" << attempt << \" failed (unknown)\" << std::endl;\n"
       << "                if (attempt < maxAttempts) std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));\n"
       << "            }\n"
       << "        }\n"
       << "        if (!ok) std::cerr << \"[Job] '\" << job.name << \"' permanently failed after \" << maxAttempts << \" attempts\" << std::endl;\n"
       << "        if (!job.id.empty()) removeJob(job.id);\n"
       << "    }\n"
       << "}\n\n"
       << "// Supervisor: keep N workers alive; restart any that exit unexpectedly.\n"
       << "void start_job_supervisor(int n) {\n"
       << "    for (int i = 0; i < n; ++i) {\n"
       << "        std::thread([i]() {\n"
       << "            while (true) {\n"
       << "                std::thread w(run_job_worker);\n"
       << "                w.join();\n"
       << "                std::cerr << \"[Supervisor] job worker \" << i << \" exited; restarting\" << std::endl;\n"
       << "                std::this_thread::sleep_for(std::chrono::milliseconds(100));\n"
       << "            }\n"
       << "        }).detach();\n"
       << "    }\n"
       << "}\n\n"
       << "// Crash recovery: replay jobs persisted by a previous run.\n"
       << "void recover_persisted_jobs() {\n"
       << "    std::vector<PendingJob> recovered;\n"
       << "    {\n"
       << "        std::lock_guard<std::mutex> lk(jobs_file_mutex);\n"
       << "        std::ifstream in(JOBS_FILE);\n"
       << "        if (!in.is_open()) return;\n"
       << "        std::string line;\n"
       << "        while (std::getline(in, line)) {\n"
       << "            if (line.empty()) continue;\n"
       << "            PendingJob j;\n"
       << "            j.id = getJSONVal(line, \"id\");\n"
       << "            j.name = getJSONVal(line, \"name\");\n"
       << "            j.args = base64_decode(getJSONVal(line, \"args\"));\n"
       << "            j.attempts = std::atoi(getJSONVal(line, \"attempts\").c_str());\n"
       << "            recovered.push_back(j);\n"
       << "        }\n"
       << "    }\n"
       << "    for (auto& j : recovered) {\n"
       << "        std::string nm = j.name, ar = j.args;\n"
       << "        j.run = [nm, ar]() { dispatch_job(nm, ar); };\n"
       << "        { std::lock_guard<std::mutex> lock(global_job_queue_mutex); global_job_queue.push(j); }\n"
       << "        global_job_queue_cv.notify_one();\n"
       << "    }\n"
       << "    if (!recovered.empty()) std::cerr << \"[Supervisor] recovered \" << recovered.size() << \" persisted job(s)\" << std::endl;\n"
       << "}\n\n";

    // GenServer: a stateful in-memory process (actor model). Each instance owns a
    // dedicated thread with a message mailbox; state is mutated only by that thread,
    // so it is safe to cast() messages concurrently. Handler exceptions are isolated.
    ss << "template <typename State>\n"
       << "class GenServer {\n"
       << "public:\n"
       << "    using Handler = std::function<void(State&, const std::string&)>;\n"
       << "    GenServer(State initial, Handler handler)\n"
       << "        : state_(initial), handler_(handler), running_(true) {\n"
       << "        worker_ = std::thread([this]() { loop(); });\n"
       << "    }\n"
       << "    ~GenServer() { stop(); }\n"
       << "    void cast(const std::string& msg) {\n"
       << "        { std::lock_guard<std::mutex> lk(mtx_); mailbox_.push(msg); }\n"
       << "        cv_.notify_one();\n"
       << "    }\n"
       << "    State get() { std::lock_guard<std::mutex> lk(stateMtx_); return state_; }\n"
       << "    void stop() {\n"
       << "        if (!running_.exchange(false)) return;\n"
       << "        cv_.notify_one();\n"
       << "        if (worker_.joinable()) worker_.join();\n"
       << "    }\n"
       << "private:\n"
       << "    void loop() {\n"
       << "        while (running_) {\n"
       << "            std::string msg;\n"
       << "            {\n"
       << "                std::unique_lock<std::mutex> lk(mtx_);\n"
       << "                cv_.wait(lk, [this]() { return !mailbox_.empty() || !running_; });\n"
       << "                if (!running_ && mailbox_.empty()) break;\n"
       << "                msg = mailbox_.front(); mailbox_.pop();\n"
       << "            }\n"
       << "            try {\n"
       << "                std::lock_guard<std::mutex> lk(stateMtx_);\n"
       << "                handler_(state_, msg);\n"
       << "            } catch (const std::exception& e) {\n"
       << "                std::cerr << \"[GenServer] handler error: \" << e.what() << std::endl;\n"
       << "            } catch (...) {\n"
       << "                std::cerr << \"[GenServer] handler error (unknown)\" << std::endl;\n"
       << "            }\n"
       << "        }\n"
       << "    }\n"
       << "    State state_;\n"
       << "    Handler handler_;\n"
       << "    std::queue<std::string> mailbox_;\n"
       << "    std::mutex mtx_, stateMtx_;\n"
       << "    std::condition_variable cv_;\n"
       << "    std::atomic<bool> running_;\n"
       << "    std::thread worker_;\n"
       << "};\n\n";

    ss << "// IP-based Rate Limiting Middleware State & Checker\n"
       << "struct RateLimitEntry {\n"
       << "    int request_count;\n"
       << "    std::chrono::steady_clock::time_point window_start;\n"
       << "};\n\n"
       << "std::unordered_map<std::string, RateLimitEntry> rate_limit_map;\n"
       << "std::mutex rate_limit_mutex;\n\n"
       << "bool check_rate_limit(const std::string& ip, int limit, int window_seconds) {\n"
       << "    std::lock_guard<std::mutex> lock(rate_limit_mutex);\n"
       << "    auto now = std::chrono::steady_clock::now();\n"
       << "    auto it = rate_limit_map.find(ip);\n"
       << "    if (it == rate_limit_map.end()) {\n"
       << "        rate_limit_map[ip] = {1, now};\n"
       << "        return true;\n"
       << "    }\n"
       << "    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.window_start).count();\n"
       << "    if (elapsed >= window_seconds) {\n"
       << "        it->second.request_count = 1;\n"
       << "        it->second.window_start = now;\n"
       << "        return true;\n"
       << "    }\n"
       << "    if (it->second.request_count >= limit) {\n"
       << "        return false;\n"
       << "    }\n"
       << "    it->second.request_count++;\n"
       << "    return true;\n"
       << "}\n\n";

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

    // Split a flat JSON array string "[{..},{..}]" into top-level object strings,
    // respecting quotes/escapes. Used by association preloading.
    ss << "std::vector<std::string> __splitJsonObjects(const std::string& arr) {\n"
       << "    std::vector<std::string> out;\n"
       << "    int depth = 0; bool inStr = false; bool esc = false; size_t start = std::string::npos;\n"
       << "    for (size_t i = 0; i < arr.size(); ++i) {\n"
       << "        char c = arr[i];\n"
       << "        if (inStr) {\n"
       << "            if (esc) esc = false;\n"
       << "            else if (c == '\\\\') esc = true;\n"
       << "            else if (c == '\"') inStr = false;\n"
       << "            continue;\n"
       << "        }\n"
       << "        if (c == '\"') { inStr = true; continue; }\n"
       << "        if (c == '{') { if (depth == 0) start = i; depth++; }\n"
       << "        else if (c == '}') { depth--; if (depth == 0 && start != std::string::npos) { out.push_back(arr.substr(start, i - start + 1)); start = std::string::npos; } }\n"
       << "    }\n"
       << "    return out;\n"
       << "}\n\n";

    // Basic email format validator used by changeset format(field, email) rules.
    ss << "bool isValidEmail(const std::string& s) {\n"
       << "    size_t at = s.find('@');\n"
       << "    if (at == std::string::npos || at == 0) return false;\n"
       << "    size_t dot = s.find('.', at);\n"
       << "    if (dot == std::string::npos || dot == at + 1) return false;\n"
       << "    if (dot + 1 >= s.size()) return false;\n"
       << "    if (s.find(' ') != std::string::npos) return false;\n"
       << "    return true;\n"
       << "}\n\n";

    // Dynamic route matcher: compares the request's "METHOD /target" line against a
    // route pattern that may contain :params (e.g. /api/leads/:id), filling the
    // captured params map. Query string is ignored. Returns false on method or
    // structural mismatch.
    ss << "std::vector<std::string> splitPathSegments(const std::string& s) {\n"
       << "    std::vector<std::string> out;\n"
       << "    size_t i = 0;\n"
       << "    while (i < s.size()) {\n"
       << "        if (s[i] == '/') { i++; continue; }\n"
       << "        size_t j = s.find('/', i);\n"
       << "        if (j == std::string::npos) j = s.size();\n"
       << "        out.push_back(s.substr(i, j - i));\n"
       << "        i = j;\n"
       << "    }\n"
       << "    return out;\n"
       << "}\n\n"
       << "bool matchDynamicRoute(const std::string& req, const std::string& method,\n"
       << "                       const std::string& pattern,\n"
       << "                       std::map<std::string, std::string>& params) {\n"
       << "    params.clear();\n"
       << "    size_t firstLineEnd = req.find('\\n');\n"
       << "    std::string reqLine = (firstLineEnd == std::string::npos) ? req : req.substr(0, firstLineEnd);\n"
       << "    size_t sp1 = reqLine.find(' ');\n"
       << "    if (sp1 == std::string::npos) return false;\n"
       << "    if (reqLine.substr(0, sp1) != method) return false;\n"
       << "    size_t sp2 = reqLine.find(' ', sp1 + 1);\n"
       << "    std::string target = (sp2 == std::string::npos) ? reqLine.substr(sp1 + 1) : reqLine.substr(sp1 + 1, sp2 - sp1 - 1);\n"
       << "    size_t qPos = target.find('?');\n"
       << "    if (qPos != std::string::npos) target = target.substr(0, qPos);\n"
       << "    std::vector<std::string> pSeg = splitPathSegments(pattern);\n"
       << "    std::vector<std::string> tSeg = splitPathSegments(target);\n"
       << "    if (pSeg.size() != tSeg.size()) return false;\n"
       << "    for (size_t i = 0; i < pSeg.size(); ++i) {\n"
       << "        if (!pSeg[i].empty() && pSeg[i][0] == ':') {\n"
       << "            params[pSeg[i].substr(1)] = tSeg[i];\n"
       << "        } else if (pSeg[i] != tSeg[i]) {\n"
       << "            return false;\n"
       << "        }\n"
       << "    }\n"
       << "    return true;\n"
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
       << "std::string sha256_bytes(const std::string& str) {\n"
       << "    unsigned int h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;\n"
       << "    unsigned int h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;\n"
       << "    unsigned int k[64] = {\n"
       << "        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,\n"
       << "        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,\n"
       << "        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,\n"
       << "        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,\n"
       << "        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,\n"
       << "        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,\n"
       << "        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,\n"
       << "        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2\n"
       << "    };\n"
       << "    std::vector<unsigned char> buf(str.begin(), str.end());\n"
       << "    uint64_t orig_len_bits = (uint64_t)buf.size() * 8;\n"
       << "    buf.push_back(0x80);\n"
       << "    while ((buf.size() * 8) % 512 != 448) {\n"
       << "        buf.push_back(0x00);\n"
       << "    }\n"
       << "    for (int i = 7; i >= 0; i--) {\n"
       << "        buf.push_back((unsigned char)((orig_len_bits >> (i * 8)) & 0xFF));\n"
       << "    }\n"
       << "    auto rotr = [](unsigned int x, unsigned int n) {\n"
       << "        return (x >> n) | (x << (32 - n));\n"
       << "    };\n"
       << "    for (size_t chunk = 0; chunk < buf.size() / 64; ++chunk) {\n"
       << "        unsigned int w[64];\n"
       << "        for (int i = 0; i < 16; ++i) {\n"
       << "            w[i] = (buf[chunk * 64 + i * 4] << 24) |\n"
       << "                   (buf[chunk * 64 + i * 4 + 1] << 16) |\n"
       << "                   (buf[chunk * 64 + i * 4 + 2] << 8) |\n"
       << "                   (buf[chunk * 64 + i * 4 + 3]);\n"
       << "        }\n"
       << "        for (int i = 16; i < 64; ++i) {\n"
       << "            unsigned int s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);\n"
       << "            unsigned int s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);\n"
       << "            w[i] = w[i - 16] + s0 + w[i - 7] + s1;\n"
       << "        }\n"
       << "        unsigned int a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;\n"
       << "        for (int i = 0; i < 64; ++i) {\n"
       << "            unsigned int S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);\n"
       << "            unsigned int ch = (e & f) ^ (~e & g);\n"
       << "            unsigned int temp1 = h + S1 + ch + k[i] + w[i];\n"
       << "            unsigned int S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);\n"
       << "            unsigned int maj = (a & b) ^ (a & c) ^ (b & c);\n"
       << "            unsigned int temp2 = S0 + maj;\n"
       << "            h = g;\n"
       << "            g = f;\n"
       << "            f = e;\n"
       << "            e = d + temp1;\n"
       << "            d = c;\n"
       << "            c = b;\n"
       << "            b = a;\n"
       << "            a = temp1 + temp2;\n"
       << "        }\n"
       << "        h0 += a; h1 += b; h2 += c; h3 += d;\n"
       << "        h4 += e; h5 += f; h6 += g; h7 += h;\n"
       << "    }\n"
       << "    std::string out; out.resize(32);\n"
       << "    unsigned int hs[8] = {h0, h1, h2, h3, h4, h5, h6, h7};\n"
       << "    for (int i = 0; i < 8; ++i) {\n"
       << "        out[i*4]   = (char)((hs[i] >> 24) & 0xFF);\n"
       << "        out[i*4+1] = (char)((hs[i] >> 16) & 0xFF);\n"
       << "        out[i*4+2] = (char)((hs[i] >> 8) & 0xFF);\n"
       << "        out[i*4+3] = (char)(hs[i] & 0xFF);\n"
       << "    }\n"
       << "    return out;\n"
       << "}\n\n"
       << "std::string sha256(const std::string& str) {\n"
       << "    std::string d = sha256_bytes(str);\n"
       << "    std::stringstream ss_hex; ss_hex << std::hex << std::setfill('0');\n"
       << "    for (unsigned char c : d) ss_hex << std::setw(2) << (int)c;\n"
       << "    return ss_hex.str();\n"
       << "}\n\n"
       << "static const std::string b64_chars = \n"
       << "             \"ABCDEFGHIJKLMNOPQRSTUVWXYZ\"\n"
       << "             \"abcdefghijklmnopqrstuvwxyz\"\n"
       << "             \"0123456789+/\";\n\n"
       << "std::string base64_encode(const std::string& in) {\n"
       << "    std::string out;\n"
       << "    int val = 0, valb = -6;\n"
       << "    for (unsigned char c : in) {\n"
       << "        val = (val << 8) + c;\n"
       << "        valb += 8;\n"
       << "        while (valb >= 0) {\n"
       << "            out.push_back(b64_chars[(val >> valb) & 0x3F]);\n"
       << "            valb -= 6;\n"
       << "        }\n"
       << "    }\n"
       << "    if (valb > -6) out.push_back(b64_chars[((val << 8) >> (valb + 8)) & 0x3F]);\n"
       << "    while (out.size() % 4) out.push_back('=');\n"
       << "    return out;\n"
       << "}\n\n"
       << "std::string base64url_encode(const std::string& in) {\n"
       << "    std::string out = base64_encode(in);\n"
       << "    for (char &c : out) {\n"
       << "        if (c == '+') c = '-';\n"
       << "        else if (c == '/') c = '_';\n"
       << "    }\n"
       << "    while (!out.empty() && out.back() == '=') {\n"
       << "        out.pop_back();\n"
       << "    }\n"
       << "    return out;\n"
       << "}\n\n"
       << "std::string base64_decode(const std::string& in) {\n"
       << "    std::vector<int> T(256, -1);\n"
       << "    for (int i = 0; i < 64; i++) T[b64_chars[i]] = i;\n"
       << "    std::string out;\n"
       << "    int val = 0, valb = -8;\n"
       << "    for (unsigned char c : in) {\n"
       << "        if (T[c] == -1) continue;\n"
       << "        val = (val << 6) + T[c];\n"
       << "        valb += 6;\n"
       << "        if (valb >= 0) {\n"
       << "            out.push_back(char((val >> valb) & 0xFF));\n"
       << "            valb -= 8;\n"
       << "        }\n"
       << "    }\n"
       << "    return out;\n"
       << "}\n\n"
       << "std::string base64url_decode(std::string in) {\n"
       << "    for (char &c : in) {\n"
       << "        if (c == '-') c = '+';\n"
       << "        else if (c == '_') c = '/';\n"
       << "    }\n"
       << "    while (in.size() % 4) {\n"
       << "        in.push_back('=');\n"
       << "    }\n"
       << "    return base64_decode(in);\n"
       << "}\n\n"
       // Constant-time string comparison to avoid timing attacks on token/hash checks.
       << "bool constTimeEq(const std::string& a, const std::string& b) {\n"
       << "    if (a.size() != b.size()) return false;\n"
       << "    unsigned char diff = 0;\n"
       << "    for (size_t i = 0; i < a.size(); ++i) diff |= (unsigned char)(a[i] ^ b[i]);\n"
       << "    return diff == 0;\n"
       << "}\n\n"
       // HMAC-SHA256 (RFC 2104) built on the raw SHA-256 digest. Block size = 64 bytes.
       << "std::string hmac_sha256(const std::string& key, const std::string& msg) {\n"
       << "    std::string k = key;\n"
       << "    if (k.size() > 64) k = sha256_bytes(k);\n"
       << "    if (k.size() < 64) k.resize(64, '\\0');\n"
       << "    std::string o_pad(64, 0), i_pad(64, 0);\n"
       << "    for (int i = 0; i < 64; ++i) {\n"
       << "        o_pad[i] = (char)((unsigned char)k[i] ^ 0x5c);\n"
       << "        i_pad[i] = (char)((unsigned char)k[i] ^ 0x36);\n"
       << "    }\n"
       << "    return sha256_bytes(o_pad + sha256_bytes(i_pad + msg));\n"
       << "}\n\n"
       // PBKDF2-HMAC-SHA256 (RFC 8018) key derivation.
       << "std::string pbkdf2_sha256(const std::string& password, const std::string& salt, int iterations, int dkLen) {\n"
       << "    std::string dk;\n"
       << "    int hLen = 32;\n"
       << "    int blocks = (dkLen + hLen - 1) / hLen;\n"
       << "    for (int b = 1; b <= blocks; ++b) {\n"
       << "        std::string saltBlock = salt;\n"
       << "        saltBlock.push_back((char)((b >> 24) & 0xFF));\n"
       << "        saltBlock.push_back((char)((b >> 16) & 0xFF));\n"
       << "        saltBlock.push_back((char)((b >> 8) & 0xFF));\n"
       << "        saltBlock.push_back((char)(b & 0xFF));\n"
       << "        std::string u = hmac_sha256(password, saltBlock);\n"
       << "        std::string t = u;\n"
       << "        for (int i = 1; i < iterations; ++i) {\n"
       << "            u = hmac_sha256(password, u);\n"
       << "            for (size_t j = 0; j < t.size(); ++j) t[j] = (char)((unsigned char)t[j] ^ (unsigned char)u[j]);\n"
       << "        }\n"
       << "        dk += t;\n"
       << "    }\n"
       << "    return dk.substr(0, dkLen);\n"
       << "}\n\n"
       // Hash a password with a random 16-byte salt. Output: pbkdf2$iters$saltB64$hashB64.
       << "std::string hashPassword(const std::string& password) {\n"
       << "    int iterations = 100000;\n"
       << "    std::string salt; salt.resize(16);\n"
       << "    std::random_device rd;\n"
       << "    for (int i = 0; i < 16; ++i) salt[i] = (char)(rd() & 0xFF);\n"
       << "    std::string dk = pbkdf2_sha256(password, salt, iterations, 32);\n"
       << "    return \"pbkdf2$\" + std::to_string(iterations) + \"$\" + base64_encode(salt) + \"$\" + base64_encode(dk);\n"
       << "}\n\n"
       // Verify a password against a stored hash. Falls back to legacy sha256(hex) so
       // existing user records keep working after upgrading the framework.
       << "bool verifyPassword(const std::string& password, const std::string& stored) {\n"
       << "    if (stored.rfind(\"pbkdf2$\", 0) == 0) {\n"
       << "        size_t p1 = stored.find('$', 7);\n"
       << "        if (p1 == std::string::npos) return false;\n"
       << "        size_t p2 = stored.find('$', p1 + 1);\n"
       << "        if (p2 == std::string::npos) return false;\n"
       << "        int iterations = std::atoi(stored.substr(7, p1 - 7).c_str());\n"
       << "        std::string salt = base64_decode(stored.substr(p1 + 1, p2 - p1 - 1));\n"
       << "        std::string expected = base64_decode(stored.substr(p2 + 1));\n"
       << "        std::string dk = pbkdf2_sha256(password, salt, iterations, (int)expected.size());\n"
       << "        return constTimeEq(dk, expected);\n"
       << "    }\n"
       << "    return constTimeEq(sha256(password), stored);\n"
       << "}\n\n"
       // Standard JWT (HS256): base64url(header).base64url(payload).base64url(HMAC).
       << "std::string generateSessionToken(const std::string& payload) {\n"
       << "    std::string secret = getEnvOr(\"JWT_SECRET\", \"hexagen_secret_key_2026\");\n"
       << "    std::string encHeader = base64url_encode(\"{\\\"alg\\\":\\\"HS256\\\",\\\"typ\\\":\\\"JWT\\\"}\");\n"
       << "    std::string encPayload = base64url_encode(payload);\n"
       << "    std::string signingInput = encHeader + \".\" + encPayload;\n"
       << "    std::string signature = base64url_encode(hmac_sha256(secret, signingInput));\n"
       << "    return signingInput + \".\" + signature;\n"
       << "}\n\n"
       << "bool verifySessionToken(const std::string& token, std::string& payloadOut) {\n"
       << "    std::string secret = getEnvOr(\"JWT_SECRET\", \"hexagen_secret_key_2026\");\n"
       << "    size_t dot1 = token.find('.');\n"
       << "    if (dot1 == std::string::npos) return false;\n"
       << "    size_t dot2 = token.find('.', dot1 + 1);\n"
       << "    if (dot2 == std::string::npos) return false;\n"
       << "    std::string signingInput = token.substr(0, dot2);\n"
       << "    std::string signature = token.substr(dot2 + 1);\n"
       << "    std::string expected = base64url_encode(hmac_sha256(secret, signingInput));\n"
       << "    if (!constTimeEq(signature, expected)) return false;\n"
       << "    payloadOut = base64url_decode(token.substr(dot1 + 1, dot2 - dot1 - 1));\n"
       << "    return true;\n"
       << "}\n\n"
       << "std::string getHeaderValue(const std::string& req, const std::string& headerName) {\n"
       << "    std::string reqLower = req;\n"
       << "    size_t bodyPos = reqLower.find(\"\\r\\n\\r\\n\");\n"
       << "    if (bodyPos != std::string::npos) {\n"
       << "        reqLower = reqLower.substr(0, bodyPos);\n"
       << "    }\n"
       << "    for (char &c : reqLower) {\n"
       << "        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';\n"
       << "    }\n"
       << "    std::string target = headerName;\n"
       << "    for (char &c : target) {\n"
       << "        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';\n"
       << "    }\n"
       << "    target += \":\";\n"
       << "    size_t pos = reqLower.find(target);\n"
       << "    if (pos == std::string::npos) return \"\";\n"
       << "    size_t valStart = pos + target.length();\n"
       << "    while (valStart < reqLower.length() && (reqLower[valStart] == ' ' || reqLower[valStart] == '\\t')) valStart++;\n"
       << "    size_t origStart = valStart;\n"
       << "    size_t origEnd = req.find(\"\\r\\n\", origStart);\n"
       << "    if (origEnd == std::string::npos) origEnd = req.length();\n"
       << "    std::string val = req.substr(origStart, origEnd - origStart);\n"
       << "    while (!val.empty() && (val.back() == ' ' || val.back() == '\\t' || val.back() == '\\r')) {\n"
       << "        val.pop_back();\n"
       << "    }\n"
       << "    return val;\n"
       << "}\n\n"
       << "struct MultipartPart {\n"
       << "    std::string name;\n"
       << "    std::string filename;\n"
       << "    std::string contentType;\n"
       << "    std::string data;\n"
       << "};\n\n"
       << "std::vector<MultipartPart> parseMultipart(const std::string& body, const std::string& boundary) {\n"
       << "    std::vector<MultipartPart> parts;\n"
       << "    if (boundary.empty()) return parts;\n"
       << "    size_t pos = 0;\n"
       << "    while (true) {\n"
       << "        size_t partStart = body.find(boundary, pos);\n"
       << "        if (partStart == std::string::npos) break;\n"
       << "        partStart += boundary.length();\n"
       << "        if (partStart + 2 <= body.length() && body.substr(partStart, 2) == \"--\") {\n"
       << "            break;\n"
       << "        }\n"
       << "        if (partStart + 2 <= body.length() && body.substr(partStart, 2) == \"\\r\\n\") {\n"
       << "            partStart += 2;\n"
       << "        } else if (partStart + 1 <= body.length() && body[partStart] == '\\n') {\n"
       << "            partStart += 1;\n"
       << "        }\n"
       << "        size_t partEnd = body.find(boundary, partStart);\n"
       << "        if (partEnd == std::string::npos) break;\n"
       << "        size_t contentLen = partEnd - partStart;\n"
       << "        if (contentLen >= 2 && body[partEnd - 2] == '\\r' && body[partEnd - 1] == '\\n') {\n"
       << "            contentLen -= 2;\n"
       << "        } else if (contentLen >= 1 && body[partEnd - 1] == '\\n') {\n"
       << "            contentLen -= 1;\n"
       << "        }\n"
       << "        if (contentLen >= 1 && body[partEnd - contentLen - 1] == '\\r') {\n"
       << "            contentLen -= 1;\n"
       << "        }\n"
       << "        std::string partContent = body.substr(partStart, contentLen);\n"
       << "        size_t headerEnd = partContent.find(\"\\r\\n\\r\\n\");\n"
       << "        size_t headerEndLen = 4;\n"
       << "        if (headerEnd == std::string::npos) {\n"
       << "            headerEnd = partContent.find(\"\\n\\n\");\n"
       << "            headerEndLen = 2;\n"
       << "        }\n"
       << "        if (headerEnd != std::string::npos) {\n"
       << "            std::string headers = partContent.substr(0, headerEnd);\n"
       << "            std::string data = partContent.substr(headerEnd + headerEndLen);\n"
       << "            MultipartPart part;\n"
       << "            part.data = data;\n"
       << "            size_t cdPos = headers.find(\"Content-Disposition:\");\n"
       << "            if (cdPos != std::string::npos) {\n"
       << "                size_t cdEnd = headers.find(\"\\n\", cdPos);\n"
       << "                std::string cdLine = headers.substr(cdPos, cdEnd == std::string::npos ? std::string::npos : cdEnd - cdPos);\n"
       << "                size_t namePos = cdLine.find(\"name=\\\"\");\n"
       << "                if (namePos != std::string::npos) {\n"
       << "                    size_t nameEnd = cdLine.find(\"\\\"\", namePos + 6);\n"
       << "                    if (nameEnd != std::string::npos) {\n"
       << "                        part.name = cdLine.substr(namePos + 6, nameEnd - (namePos + 6));\n"
       << "                    }\n"
       << "                }\n"
       << "                size_t filePos = cdLine.find(\"filename=\\\"\");\n"
       << "                if (filePos != std::string::npos) {\n"
       << "                    size_t fileEnd = cdLine.find(\"\\\"\", filePos + 10);\n"
       << "                    if (fileEnd != std::string::npos) {\n"
       << "                        part.filename = cdLine.substr(filePos + 10, fileEnd - (filePos + 10));\n"
       << "                    }\n"
       << "                }\n"
       << "            }\n"
       << "            size_t ctPos = headers.find(\"Content-Type:\");\n"
       << "            if (ctPos != std::string::npos) {\n"
       << "                size_t ctEnd = headers.find(\"\\n\", ctPos);\n"
       << "                std::string ctLine = headers.substr(ctPos, ctEnd == std::string::npos ? std::string::npos : ctEnd - ctPos);\n"
       << "                size_t ctValStart = ctLine.find(\":\") + 1;\n"
       << "                while (ctValStart < ctLine.length() && (ctLine[ctValStart] == ' ' || ctLine[ctValStart] == '\\t' || ctLine[ctValStart] == '\\r')) ctValStart++;\n"
       << "                part.contentType = ctLine.substr(ctValStart);\n"
       << "                while (!part.contentType.empty() && (part.contentType.back() == '\\r' || part.contentType.back() == ' ' || part.contentType.back() == '\\t')) {\n"
       << "                    part.contentType.pop_back();\n"
       << "                }\n"
       << "            }\n"
       << "            parts.push_back(part);\n"
       << "        }\n"
       << "        pos = partEnd;\n"
       << "    }\n"
       << "    return parts;\n"
       << "}\n\n"
       << "std::string getMultipartVal(const std::vector<MultipartPart>& parts, const std::string& fieldName) {\n"
       << "    for (const auto& part : parts) {\n"
       << "        if (part.name == fieldName) {\n"
       << "            if (!part.filename.empty()) {\n"
       << "                mkdir(\"public\", 0777);\n"
       << "                mkdir(\"public/uploads\", 0777);\n"
       << "                std::string safeName = part.filename;\n"
       << "                for (char &c : safeName) {\n"
       << "                    if (c == '/' || c == '\\\\' || c == '?' || c == '*' || c == ':' || c == '|') c = '_';\n"
       << "                }\n"
       << "                std::string timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());\n"
       << "                std::string savedName = timestamp + \"_\" + safeName;\n"
       << "                std::string filePath = \"public/uploads/\" + savedName;\n"
       << "                std::ofstream outfile(filePath, std::ios::binary);\n"
       << "                if (outfile.is_open()) {\n"
       << "                    outfile.write(part.data.data(), part.data.size());\n"
       << "                    outfile.close();\n"
       << "                    return \"/public/uploads/\" + savedName;\n"
       << "                }\n"
       << "                return \"\";\n"
       << "            } else {\n"
       << "                return part.data;\n"
       << "            }\n"
       << "        }\n"
       << "    }\n"
       << "    return \"\";\n"
       << "}\n\n"
       << "std::string base64_encode(const std::vector<unsigned char>& data) {\n"
       << "    static const char* s = \"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/\";\n"
       << "    std::string out;\n"
       << "    int i = 0;\n"
       << "    int val = 0;\n"
       << "    for (unsigned char c : data) {\n"
       << "        val = (val << 8) + c;\n"
       << "        i += 8;\n"
       << "        while (i >= 6) {\n"
       << "            out.push_back(s[(val >> (i - 6)) & 0x3F]);\n"
       << "            i -= 6;\n"
       << "        }\n"
       << "    }\n"
       << "    if (i > 0) {\n"
       << "        out.push_back(s[(val << (6 - i)) & 0x3F]);\n"
       << "    }\n"
       << "    while (out.size() % 4) {\n"
       << "        out.push_back('=');\n"
       << "    }\n"
       << "    return out;\n"
       << "}\n\n"
       << "std::vector<unsigned char> sha1(const std::string& str) {\n"
       << "    unsigned int h0 = 0x67452301;\n"
       << "    unsigned int h1 = 0xEFCDAB89;\n"
       << "    unsigned int h2 = 0x98BADCFE;\n"
       << "    unsigned int h3 = 0x10325476;\n"
       << "    unsigned int h4 = 0xC3D2E1F0;\n"
       << "    std::vector<unsigned char> buf(str.begin(), str.end());\n"
       << "    uint64_t orig_len_bits = (uint64_t)buf.size() * 8;\n"
       << "    buf.push_back(0x80);\n"
       << "    while ((buf.size() * 8) % 512 != 448) {\n"
       << "        buf.push_back(0x00);\n"
       << "    }\n"
       << "    for (int i = 7; i >= 0; i--) {\n"
       << "        buf.push_back((unsigned char)((orig_len_bits >> (i * 8)) & 0xFF));\n"
       << "    }\n"
       << "    auto leftrotate = [](unsigned int value, unsigned int bits) {\n"
       << "        return (value << bits) | (value >> (32 - bits));\n"
       << "    };\n"
       << "    for (size_t chunk = 0; chunk < buf.size() / 64; ++chunk) {\n"
       << "        unsigned int w[80];\n"
       << "        for (int i = 0; i < 16; ++i) {\n"
       << "            w[i] = (buf[chunk * 64 + i * 4] << 24) |\n"
       << "                   (buf[chunk * 64 + i * 4 + 1] << 16) |\n"
       << "                   (buf[chunk * 64 + i * 4 + 2] << 8) |\n"
       << "                   (buf[chunk * 64 + i * 4 + 3]);\n"
       << "        }\n"
       << "        for (int i = 16; i < 80; ++i) {\n"
       << "            w[i] = leftrotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);\n"
       << "        }\n"
       << "        unsigned int a = h0, b = h1, c = h2, d = h3, e = h4;\n"
       << "        for (int i = 0; i < 80; ++i) {\n"
       << "            unsigned int f, k;\n"
       << "            if (i < 20) {\n"
       << "                f = (b & c) | ((~b) & d);\n"
       << "                k = 0x5A827999;\n"
       << "            } else if (i < 40) {\n"
       << "                f = b ^ c ^ d;\n"
       << "                k = 0x6ED9EBA1;\n"
       << "            } else if (i < 60) {\n"
       << "                f = (b & c) | (b & d) | (c & d);\n"
       << "                k = 0x8F1BBCDC;\n"
       << "            } else {\n"
       << "                f = b ^ c ^ d;\n"
       << "                k = 0xCA62C1D6;\n"
       << "            }\n"
       << "            unsigned int temp = leftrotate(a, 5) + f + e + k + w[i];\n"
       << "            e = d; d = c; c = leftrotate(b, 30); b = a; a = temp;\n"
       << "        }\n"
       << "        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;\n"
       << "    }\n"
       << "    std::vector<unsigned char> hash(20);\n"
       << "    hash[0] = (h0 >> 24) & 0xFF; hash[1] = (h0 >> 16) & 0xFF; hash[2] = (h0 >> 8) & 0xFF; hash[3] = h0 & 0xFF;\n"
       << "    hash[4] = (h1 >> 24) & 0xFF; hash[5] = (h1 >> 16) & 0xFF; hash[6] = (h1 >> 8) & 0xFF; hash[7] = h1 & 0xFF;\n"
       << "    hash[8] = (h2 >> 24) & 0xFF; hash[9] = (h2 >> 16) & 0xFF; hash[10] = (h2 >> 8) & 0xFF; hash[11] = h2 & 0xFF;\n"
       << "    hash[12] = (h3 >> 24) & 0xFF; hash[13] = (h3 >> 16) & 0xFF; hash[14] = (h3 >> 8) & 0xFF; hash[15] = h3 & 0xFF;\n"
       << "    hash[16] = (h4 >> 24) & 0xFF; hash[17] = (h4 >> 16) & 0xFF; hash[18] = (h4 >> 8) & 0xFF; hash[19] = h4 & 0xFF;\n"
       << "    return hash;\n"
       << "}\n\n"
       << "std::string getWebSocketAcceptKey(const std::string& key) {\n"
       << "    std::string concat = key + \"258EAFA5-E914-47DA-95CA-C5AB0DC85B11\";\n"
       << "    return base64_encode(sha1(concat));\n"
       << "}\n\n"
       << "std::set<int> active_ws_clients;\n"
       << "std::mutex ws_clients_mutex;\n\n"
       << "void register_ws_client(int fd) {\n"
       << "    std::lock_guard<std::mutex> lock(ws_clients_mutex);\n"
       << "    active_ws_clients.insert(fd);\n"
       << "}\n\n"
       << "void unregister_ws_client(int fd) {\n"
       << "    std::lock_guard<std::mutex> lock(ws_clients_mutex);\n"
       << "    active_ws_clients.erase(fd);\n"
       << "    close(fd);\n"
       << "}\n\n"
       << "void sendWebSocketFrame(int client_fd, const std::string& message) {\n"
       << "    std::vector<unsigned char> frame;\n"
       << "    frame.push_back(0x81);\n"
       << "    size_t len = message.length();\n"
       << "    if (len < 126) {\n"
       << "        frame.push_back((unsigned char)len);\n"
       << "    } else if (len <= 65535) {\n"
       << "        frame.push_back(126);\n"
       << "        frame.push_back((len >> 8) & 0xFF);\n"
       << "        frame.push_back(len & 0xFF);\n"
       << "    } else {\n"
       << "        frame.push_back(127);\n"
       << "        for (int i = 7; i >= 0; --i) {\n"
       << "            frame.push_back((len >> (i * 8)) & 0xFF);\n"
       << "        }\n"
       << "    }\n"
       << "    frame.insert(frame.end(), message.begin(), message.end());\n"
       << "    send(client_fd, frame.data(), frame.size(), 0);\n"
       << "}\n\n"
       << "void broadcast_ws_message(const std::string& message, int sender_fd = -1) {\n"
       << "    std::lock_guard<std::mutex> lock(ws_clients_mutex);\n"
       << "    for (int fd : active_ws_clients) {\n"
       << "        if (fd != sender_fd) {\n"
       << "            sendWebSocketFrame(fd, message);\n"
       << "        }\n"
       << "    }\n"
       << "}\n\n"
       // ---- PubSub: topic-based channels over WebSocket ----
       << "std::map<std::string, std::set<int>> ws_topics;\n"
       << "std::mutex ws_topics_mutex;\n\n"
       << "void subscribe_topic(const std::string& topic, int fd) {\n"
       << "    std::lock_guard<std::mutex> lk(ws_topics_mutex);\n"
       << "    ws_topics[topic].insert(fd);\n"
       << "}\n\n"
       << "void unsubscribe_all_topics(int fd) {\n"
       << "    std::lock_guard<std::mutex> lk(ws_topics_mutex);\n"
       << "    for (auto& kv : ws_topics) kv.second.erase(fd);\n"
       << "}\n\n"
       << "int publish_topic(const std::string& topic, const std::string& message) {\n"
       << "    std::vector<int> targets;\n"
       << "    { std::lock_guard<std::mutex> lk(ws_topics_mutex);\n"
       << "      auto it = ws_topics.find(topic);\n"
       << "      if (it != ws_topics.end()) targets.assign(it->second.begin(), it->second.end()); }\n"
       << "    int sent = 0;\n"
       << "    for (int fd : targets) { sendWebSocketFrame(fd, message); sent++; }\n"
       << "    return sent;\n"
       << "}\n\n"
       // ---- Server-side DOM diffing (region-based, minimal patches) ----
       // Live regions are delimited in templates by <!--hg:NAME--> ... <!--/hg:NAME-->.
       // Only regions whose inner content changed are emitted as patches.
       << "std::map<std::string, std::string> extractLiveRegions(const std::string& html) {\n"
       << "    std::map<std::string, std::string> regions;\n"
       << "    size_t pos = 0;\n"
       << "    const std::string open = \"<!--hg:\";\n"
       << "    while ((pos = html.find(open, pos)) != std::string::npos) {\n"
       << "        size_t nameStart = pos + open.size();\n"
       << "        size_t nameEnd = html.find(\"-->\", nameStart);\n"
       << "        if (nameEnd == std::string::npos) break;\n"
       << "        std::string name = html.substr(nameStart, nameEnd - nameStart);\n"
       << "        size_t contentStart = nameEnd + 3;\n"
       << "        std::string close = \"<!--/hg:\" + name + \"-->\";\n"
       << "        size_t contentEnd = html.find(close, contentStart);\n"
       << "        if (contentEnd == std::string::npos) { pos = contentStart; continue; }\n"
       << "        regions[name] = html.substr(contentStart, contentEnd - contentStart);\n"
       << "        pos = contentEnd + close.size();\n"
       << "    }\n"
       << "    return regions;\n"
       << "}\n\n"
       << "std::string jsonEscape(const std::string& s) {\n"
       << "    std::string o; o.reserve(s.size() + 8);\n"
       << "    for (char c : s) {\n"
       << "        switch (c) {\n"
       << "            case '\"': o += \"\\\\\\\"\"; break;\n"
       << "            case '\\\\': o += \"\\\\\\\\\"; break;\n"
       << "            case '\\n': o += \"\\\\n\"; break;\n"
       << "            case '\\r': o += \"\\\\r\"; break;\n"
       << "            case '\\t': o += \"\\\\t\"; break;\n"
       << "            default: o += c;\n"
       << "        }\n"
       << "    }\n"
       << "    return o;\n"
       << "}\n\n"
       << "std::string computeDomPatches(const std::string& oldHtml, const std::string& newHtml) {\n"
       << "    auto oldR = extractLiveRegions(oldHtml);\n"
       << "    auto newR = extractLiveRegions(newHtml);\n"
       << "    std::stringstream js;\n"
       << "    js << \"{\\\"type\\\":\\\"patch\\\",\\\"patches\\\":[\";\n"
       << "    bool first = true;\n"
       << "    for (auto& kv : newR) {\n"
       << "        auto oit = oldR.find(kv.first);\n"
       << "        if (oit == oldR.end() || oit->second != kv.second) {\n"
       << "            if (!first) js << \",\";\n"
       << "            first = false;\n"
       << "            js << \"{\\\"id\\\":\\\"\" << jsonEscape(kv.first) << \"\\\",\\\"html\\\":\\\"\" << jsonEscape(kv.second) << \"\\\"}\";\n"
       << "        }\n"
       << "    }\n"
       << "    js << \"]}\";\n"
       << "    return js.str();\n"
       << "}\n\n"
       << "int publish_dom_patch(const std::string& oldHtml, const std::string& newHtml) {\n"
       << "    std::string patch = computeDomPatches(oldHtml, newHtml);\n"
       << "    broadcast_ws_message(patch);\n"
       << "    return (int)patch.size();\n"
       << "}\n\n"
       << "bool readWebSocketFrame(int client_fd, std::string& out_message) {\n"
       << "    unsigned char header[2];\n"
       << "    int n = recv(client_fd, header, 2, 0);\n"
       << "    if (n <= 0) return false;\n"
       << "    unsigned char opcode = header[0] & 0x0F;\n"
       << "    bool masked = (header[1] & 0x80) != 0;\n"
       << "    uint64_t payload_len = header[1] & 0x7F;\n"
       << "    if (opcode == 0x8) return false;\n"
       << "    if (payload_len == 126) {\n"
       << "        unsigned char ext_len[2];\n"
       << "        if (recv(client_fd, ext_len, 2, 0) != 2) return false;\n"
       << "        payload_len = (ext_len[0] << 8) | ext_len[1];\n"
       << "    } else if (payload_len == 127) {\n"
       << "        unsigned char ext_len[8];\n"
       << "        if (recv(client_fd, ext_len, 8, 0) != 8) return false;\n"
       << "        payload_len = 0;\n"
       << "        for (int i = 0; i < 8; ++i) {\n"
       << "            payload_len = (payload_len << 8) | ext_len[i];\n"
       << "        }\n"
       << "    }\n"
       << "    unsigned char masking_key[4] = {0};\n"
       << "    if (masked) {\n"
       << "        if (recv(client_fd, masking_key, 4, 0) != 4) return false;\n"
       << "    }\n"
       << "    std::vector<char> payload(payload_len);\n"
       << "    if (payload_len > 0) {\n"
       << "        size_t total_received = 0;\n"
       << "        while (total_received < payload_len) {\n"
       << "            int rec = recv(client_fd, payload.data() + total_received, payload_len - total_received, 0);\n"
       << "            if (rec <= 0) return false;\n"
       << "            total_received += rec;\n"
       << "        }\n"
       << "        if (masked) {\n"
       << "            for (size_t i = 0; i < payload_len; ++i) {\n"
       << "                payload[i] ^= masking_key[i % 4];\n"
       << "            }\n"
       << "        }\n"
       << "    }\n"
       << "    if (opcode == 0x1) {\n"
       << "        out_message = std::string(payload.begin(), payload.end());\n"
       << "    }\n"
       << "    return true;\n"
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
    // Generic thread-safe connection pool reused across concurrent requests,
    // avoiding a fresh connect/disconnect per query under load. Pool size is
    // configurable via DB_POOL_SIZE (default 8).
    if (program->dbType == "sqlite" || program->dbType == "postgres" ||
        program->dbType == "postgresql" || program->dbType == "mysql") {
        ss << "template <typename T>\n"
           << "class ConnPool {\n"
           << "public:\n"
           << "    void init(std::function<T()> f, int maxSz) { factory_ = f; maxSize_ = (maxSz > 0 ? maxSz : 1); }\n"
           << "    T acquire() {\n"
           << "        std::unique_lock<std::mutex> lk(mtx_);\n"
           << "        if (!idle_.empty()) { T c = idle_.front(); idle_.pop(); return c; }\n"
           << "        if (created_ < maxSize_) {\n"
           << "            created_++;\n"
           << "            lk.unlock();\n"
           << "            T c = factory_();\n"
           << "            if (!c) { std::lock_guard<std::mutex> g(mtx_); created_--; }\n"
           << "            return c;\n"
           << "        }\n"
           << "        cv_.wait(lk, [&]{ return !idle_.empty(); });\n"
           << "        T c = idle_.front(); idle_.pop(); return c;\n"
           << "    }\n"
           << "    void release(T c) {\n"
           << "        if (!c) return;\n"
           << "        std::lock_guard<std::mutex> lk(mtx_);\n"
           << "        idle_.push(c);\n"
           << "        cv_.notify_one();\n"
           << "    }\n"
           << "private:\n"
           << "    std::queue<T> idle_;\n"
           << "    std::mutex mtx_;\n"
           << "    std::condition_variable cv_;\n"
           << "    std::function<T()> factory_;\n"
           << "    int maxSize_ = 8;\n"
           << "    int created_ = 0;\n"
           << "};\n\n";
    }
    if (program->dbType == "sqlite") {
        ss << "sqlite3* createSQLiteConn() {\n"
           << "    std::string dbName = getEnvOr(\"DB_NAME\", \"vortex_db.db\");\n"
           << "    sqlite3* db = nullptr;\n"
           << "    int rc = sqlite3_open(dbName.c_str(), &db);\n"
           << "    if (rc != SQLITE_OK) {\n"
           << "        std::cerr << \"[SQLite] Can't open database: \" << sqlite3_errmsg(db) << std::endl;\n"
           << "        if (db) sqlite3_close(db);\n"
           << "        return nullptr;\n"
           << "    }\n"
           << "    return db;\n"
           << "}\n\n"
           << "ConnPool<sqlite3*>& sqlitePool() {\n"
           << "    static ConnPool<sqlite3*>* p = []{ auto* x = new ConnPool<sqlite3*>(); x->init(createSQLiteConn, std::atoi(getEnvOr(\"DB_POOL_SIZE\", \"8\"))); return x; }();\n"
           << "    return *p;\n"
           << "}\n"
           << "sqlite3* getSQLiteConn() { return sqlitePool().acquire(); }\n"
           << "void releaseSQLiteConn(sqlite3* c) { sqlitePool().release(c); }\n\n";
    } else if (program->dbType == "postgres" || program->dbType == "postgresql") {
        ss << "PGconn* createPGConn() {\n"
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
           << "}\n\n"
           << "ConnPool<PGconn*>& pgPool() {\n"
           << "    static ConnPool<PGconn*>* p = []{ auto* x = new ConnPool<PGconn*>(); x->init(createPGConn, std::atoi(getEnvOr(\"DB_POOL_SIZE\", \"8\"))); return x; }();\n"
           << "    return *p;\n"
           << "}\n"
           << "PGconn* getPGConn() { return pgPool().acquire(); }\n"
           << "void releasePGConn(PGconn* c) { pgPool().release(c); }\n\n";
    } else if (program->dbType == "mysql") {
        ss << "MYSQL* createMySQLConn() {\n"
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
           << "}\n\n"
           << "ConnPool<MYSQL*>& mysqlPool() {\n"
           << "    static ConnPool<MYSQL*>* p = []{ auto* x = new ConnPool<MYSQL*>(); x->init(createMySQLConn, std::atoi(getEnvOr(\"DB_POOL_SIZE\", \"8\"))); return x; }();\n"
           << "    return *p;\n"
           << "}\n"
           << "MYSQL* getMySQLConn() { return mysqlPool().acquire(); }\n"
           << "void releaseMySQLConn(MYSQL* c) { mysqlPool().release(c); }\n\n";
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
        ss << "        releaseSQLiteConn(db);\n"
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
        ss << "        releasePGConn(conn);\n"
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
        ss << "        releaseMySQLConn(conn);\n"
           << "    }\n";
    } else {
        ss << "    // No init needed for JSONL\n";
    }
    ss << "}\n\n";

    for (const auto& job : program->jobs) {
        ss << "struct Job_" << job->name << ";\n";
    }
    if (!program->jobs.empty()) ss << "\n";

    for (const auto& slice : program->slices) {
        ss << generateSlice(slice) << "\n";
    }

    // -------------------------------------------------------------
    // Association preloading (anti N+1). Emitted AFTER all slice
    // classes so cross-slice static calls resolve. For each slice
    // with relation() fields, applyPreloads_<Slice> embeds related
    // records (loaded once) when the request carries ?_preload=field.
    // Backend-agnostic: operates on each slice's getAllAsJSON output.
    // -------------------------------------------------------------
    ss << "// Association preloading helpers\n";
    for (const auto& slice : program->slices) {
        bool hasRel = false;
        for (const auto& f : slice->fields) if (f->type == DataType::RELATION && !f->relatedSlice.empty()) hasRel = true;
        if (!hasRel) continue;

        ss << "std::string applyPreloads_" << slice->name << "(const std::string& arr, const std::string& req) {\n";
        ss << "    std::string pre = getQueryParam(req, \"_preload\");\n";
        ss << "    if (pre.empty()) return arr;\n";
        ss << "    std::string preKey = \",\" + pre + \",\";\n";
        ss << "    std::vector<std::string> objs = __splitJsonObjects(arr);\n";
        for (const auto& f : slice->fields) {
            if (f->type != DataType::RELATION || f->relatedSlice.empty()) continue;
            // Determine the related slice's key field: prefer a field named "id".
            std::string keyField = "id";
            for (const auto& rs : program->slices) {
                if (rs->name == f->relatedSlice) {
                    bool hasId = false;
                    for (const auto& rf : rs->fields) if (rf->name == "id") hasId = true;
                    if (!hasId && !rs->fields.empty()) keyField = rs->fields[0]->name;
                }
            }
            ss << "    if (preKey.find(\"," << f->name << ",\") != std::string::npos) {\n";
            ss << "        std::map<std::string, std::string> rel;\n";
            ss << "        std::vector<std::string> robjs = __splitJsonObjects(" << f->relatedSlice << "::getAllAsJSON(\"\"));\n";
            ss << "        for (auto& ro : robjs) rel[getJSONVal(ro, \"" << keyField << "\")] = ro;\n";
            ss << "        for (auto& o : objs) {\n";
            ss << "            std::string fk = getJSONVal(o, \"" << f->name << "\");\n";
            ss << "            std::string emb = rel.count(fk) ? rel[fk] : std::string(\"null\");\n";
            ss << "            size_t lb = o.rfind('}');\n";
            ss << "            if (lb != std::string::npos) o = o.substr(0, lb) + \",\\\"" << f->name << "_data\\\":\" + emb + \"}\";\n";
            ss << "        }\n";
            ss << "    }\n";
        }
        ss << "    std::string result = \"[\";\n";
        ss << "    for (size_t i = 0; i < objs.size(); ++i) { if (i) result += \",\"; result += objs[i]; }\n";
        ss << "    result += \"]\";\n";
        ss << "    return result;\n";
        ss << "}\n\n";
    }

    for (const auto& job : program->jobs) {
        ss << generateJob(job) << "\n";
    }

    ss << "// Action Implementations\n";
    for (const auto& slice : program->slices) {
        for (const auto& action : slice->actions) {
            ss << generateActionImpl(slice->name, action) << "\n";
        }
    }
    for (const auto& job : program->jobs) {
        for (const auto& action : job->actions) {
            ss << generateActionImpl("Job_" + job->name, action) << "\n";
        }
    }

    // dispatch_job: reconstruct a persisted job from its serialized args and run it.
    // Used by crash recovery (recover_persisted_jobs) to rebuild closures.
    ss << "void dispatch_job(const std::string& name, const std::string& argsJson) {\n";
    ss << "    (void)argsJson;\n";
    {
        bool firstJob = true;
        for (const auto& job : program->jobs) {
            ss << "    " << (firstJob ? "if" : "else if") << " (name == \"" << job->name << "\") {\n";
            firstJob = false;
            ss << "        auto inst = std::make_shared<Job_" << job->name << ">();\n";
            for (const auto& f : job->fields) {
                if (f->type == DataType::STRING) {
                    ss << "        inst->" << f->name << " = getJSONVal(argsJson, \"" << f->name << "\");\n";
                } else if (f->type == DataType::INT || f->type == DataType::RELATION) {
                    ss << "        inst->" << f->name << " = safeStoi(getJSONVal(argsJson, \"" << f->name << "\"));\n";
                } else if (f->type == DataType::FLOAT) {
                    ss << "        inst->" << f->name << " = safeStod(getJSONVal(argsJson, \"" << f->name << "\"));\n";
                } else if (f->type == DataType::BOOL) {
                    ss << "        inst->" << f->name << " = (getJSONVal(argsJson, \"" << f->name << "\") == \"true\");\n";
                }
            }
            std::string entry = "";
            for (const auto& a : job->actions) { if (a->name == "Run") entry = "Run"; }
            if (entry.empty() && !job->actions.empty()) entry = job->actions[0]->name;
            if (!entry.empty()) ss << "        inst->" << entry << "();\n";
            ss << "    }\n";
        }
    }
    ss << "}\n\n";

    ss << "// Raw UI View HTML Pages\n";
    for (const auto& view : program->views) {
        ss << "const char* HTML_" << view->name << " = R\"HTML(\n";
        ss << generateHTMLContent(view);
        ss << "\n)HTML\";\n\n";
    }

    ss << "// AutoCRUD Admin Portal HTML\n";
    ss << "const char* HTML_ADMIN = R\"HTML(\n";
    ss << generateAdminHTML();
    ss << "\n)HTML\";\n\n";

    if (!program->views.empty()) {
        ss << "const char* HTML_CONTENT = HTML_" << program->views[0]->name << ";\n\n";
    } else {
        ss << "const char* HTML_CONTENT = \"No view defined\";\n\n";
    }

    if (includeMain) {
        ss << "Task handle_client(int client_fd, struct sockaddr_in address, int addrlen) {\n";
        ss << "    std::string req;\n";
        ss << "    co_await AsyncRead{client_fd, req};\n";
        ss << "    if (!req.empty()) {\n";
        ss << "        std::map<std::string, std::string> pathParams; // captured dynamic route params (:id)\n";
        ss << "        try {\n";
        if (hasLogger) {
            // logger middleware: emit a one-line access log per request.
            ss << "            {\n";
            ss << "                size_t _lf = req.find('\\n');\n";
            ss << "                std::string _line = (_lf == std::string::npos) ? req : req.substr(0, _lf);\n";
            ss << "                while (!_line.empty() && (_line.back() == '\\r' || _line.back() == '\\n')) _line.pop_back();\n";
            ss << "                std::time_t _t = std::time(nullptr);\n";
            ss << "                char _tb[32]; std::strftime(_tb, sizeof(_tb), \"%Y-%m-%d %H:%M:%S\", std::localtime(&_t));\n";
            ss << "                std::cout << \"[\" << _tb << \"] \" << inet_ntoa(address.sin_addr) << \" \" << _line << std::endl;\n";
            ss << "            }\n";
        }
        if (program->frontend == "react") {
            ss << "            bool handled_static = false;\n";
            ss << "            if (req.rfind(\"GET /\", 0) == 0 && req.rfind(\"GET /api/\", 0) != 0) {\n";
            ss << "                size_t space = req.find(' ', 4);\n";
            ss << "                std::string path = (space != std::string::npos) ? req.substr(4, space - 4) : \"\";\n";
            ss << "                if (path == \"/\" || path.empty() || path.find(\".\") == std::string::npos) path = \"/index.html\";\n";
            ss << "                std::string fullPath = \"frontend/dist\" + path;\n";
            ss << "                std::ifstream file(fullPath, std::ios::binary);\n";
            ss << "                if (!file.is_open() && path != \"/index.html\") {\n";
            ss << "                    fullPath = \"frontend/dist/index.html\";\n";
            ss << "                    file.open(fullPath, std::ios::binary);\n";
            ss << "                }\n";
            ss << "                if (file.is_open()) {\n";
            ss << "                    file.seekg(0, std::ios::end);\n";
            ss << "                    size_t size = file.tellg();\n";
            ss << "                    file.seekg(0, std::ios::beg);\n";
            ss << "                    std::vector<char> fileBuf(size);\n";
            ss << "                    file.read(fileBuf.data(), size);\n";
            ss << "                    file.close();\n";
            ss << "                    std::string contentType = \"application/octet-stream\";\n";
            ss << "                    if (fullPath.find(\".html\") != std::string::npos) contentType = \"text/html; charset=utf-8\";\n";
            ss << "                    else if (fullPath.find(\".js\") != std::string::npos) contentType = \"application/javascript; charset=utf-8\";\n";
            ss << "                    else if (fullPath.find(\".css\") != std::string::npos) contentType = \"text/css; charset=utf-8\";\n";
            ss << "                    else if (fullPath.find(\".png\") != std::string::npos) contentType = \"image/png\";\n";
            ss << "                    else if (fullPath.find(\".jpg\") != std::string::npos || fullPath.find(\".jpeg\") != std::string::npos) contentType = \"image/jpeg\";\n";
            ss << "                    else if (fullPath.find(\".svg\") != std::string::npos) contentType = \"image/svg+xml\";\n";
            ss << "                    else if (fullPath.find(\".json\") != std::string::npos) contentType = \"application/json\";\n";
            ss << "                    std::stringstream resp;\n";
            ss << "                    resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
            ss << "                         << \"Content-Type: \" << contentType << \"\\r\\n\"\n";
            ss << "                         << \"Content-Length: \" << size << \"\\r\\n\\r\\n\";\n";
            ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "                    send(client_fd, fileBuf.data(), size, 0);\n";
            ss << "                    handled_static = true;\n";
            ss << "                }\n";
            ss << "            }\n";
            ss << "            if (handled_static) {\n";
            ss << "                close(client_fd);\n";
            ss << "                co_return;\n";
            ss << "            }\n";
        }
        if (hasCors) {
            ss << "            if (req.rfind(\"OPTIONS \", 0) == 0) {\n";
            ss << "                std::stringstream resp;\n";
            ss << "                resp << \"HTTP/1.1 204 No Content\\r\\n\"\n";
            ss << "                     << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
            ss << "                     << \"Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\\r\\n\"\n";
            ss << "                     << \"Access-Control-Allow-Headers: Content-Type, Authorization\\r\\n\"\n";
            ss << "                     << \"Content-Length: 0\\r\\n\\r\\n\";\n";
            ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "                close(client_fd);\n";
            ss << "                co_return;\n";
            ss << "            }\n";
        }
        if (hasRateLimit) {
            ss << "            bool is_api_req = (req.find(\"/api/\") != std::string::npos);\n";
            if (!program->apis.empty()) {
                ss << "            if (!is_api_req) {\n";
                for (const auto& r : program->allRoutes()) {
                    if (r->method != "WEBSOCKET") {
                        ss << "                {\n";
                        ss << "                    size_t pos = req.find(\" \" \"" << r->path << "\");\n";
                        ss << "                    if (pos != std::string::npos) {\n";
                        ss << "                        char next_char = req[pos + " << (r->path.length() + 1) << "];\n";
                        ss << "                        if (next_char == ' ' || next_char == '?' || next_char == '/') is_api_req = true;\n";
                        ss << "                    }\n";
                        ss << "                }\n";
                    }
                }
                ss << "            }\n";
            }
            ss << "            if (is_api_req) {\n";
            ss << "                std::string client_ip = inet_ntoa(address.sin_addr);\n";
            ss << "                if (!check_rate_limit(client_ip, " << rateLimitLimit << ", " << rateLimitWindow << ")) {\n";
            ss << "                    std::string msg = \"{\\\"status\\\":\\\"error\\\",\\\"message\\\":\\\"Rate limit exceeded. Try again later.\\\"}\";\n";
            ss << "                    std::stringstream resp;\n";
            ss << "                    resp << \"HTTP/1.1 429 Too Many Requests\\r\\n\"\n";
            ss << "                         << \"Content-Type: application/json\\r\\n\"\n";
            ss << "                         << \"Access-Control-Allow-Origin: *\\r\\n\";\n";
            if (hasCors) {
                ss << "                    resp << \"Access-Control-Allow-Methods: *\\r\\n\"\n";
                ss << "                         << \"Access-Control-Allow-Headers: *\\r\\n\";\n";
            }
            ss << "                    resp << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
            ss << "                         << msg;\n";
            ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "                    close(client_fd);\n";
            ss << "                    co_return;\n";
            ss << "                }\n";
            ss << "            }\n";
        }
        ss << "            bool wsUpgraded = false;\n";
        ss << "            std::string upgradeHeader = getHeaderValue(req, \"Upgrade\");\n";
        ss << "            for (char &c : upgradeHeader) { if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; }\n";
        ss << "            if (upgradeHeader == \"websocket\") {\n";
        ss << "                if (req.find(\"GET /live \") != std::string::npos) {\n";
        ss << "                    std::string wsKey = getHeaderValue(req, \"Sec-WebSocket-Key\");\n";
        ss << "                    if (!wsKey.empty()) {\n";
        ss << "                        std::string acceptKey = getWebSocketAcceptKey(wsKey);\n";
        ss << "                        std::stringstream handshake;\n";
        ss << "                        handshake << \"HTTP/1.1 101 Switching Protocols\\r\\n\"\n";
        ss << "                                  << \"Upgrade: websocket\\r\\n\"\n";
        ss << "                                  << \"Connection: Upgrade\\r\\n\"\n";
        ss << "                                  << \"Sec-WebSocket-Accept: \" << acceptKey << \"\\r\\n\\r\\n\";\n";
        ss << "                        send(client_fd, handshake.str().c_str(), handshake.str().length(), 0);\n";
        ss << "                        wsUpgraded = true;\n";
        ss << "                        std::thread([client_fd]() {\n";
        ss << "                            register_ws_client(client_fd);\n";
        ss << "                            std::string msg;\n";
        ss << "                            while (readWebSocketFrame(client_fd, msg)) {\n";
        ss << "                                if (!msg.empty()) {\n";
        ss << "                                    std::string _act = getJSONVal(msg, \"action\");\n";
        ss << "                                    if (_act == \"subscribe\") {\n";
        ss << "                                        subscribe_topic(getJSONVal(msg, \"topic\"), client_fd);\n";
        ss << "                                        sendWebSocketFrame(client_fd, std::string(\"{\\\"type\\\":\\\"subscribed\\\",\\\"topic\\\":\\\"\") + getJSONVal(msg, \"topic\") + \"\\\"}\");\n";
        ss << "                                    } else if (_act == \"publish\") {\n";
        ss << "                                        publish_topic(getJSONVal(msg, \"topic\"), msg);\n";
        ss << "                                    } else {\n";
        ss << "                                        std::cout << \"[LiveView WS] Received: \" << msg << std::endl;\n";
        ss << "                                        broadcast_ws_message(msg, client_fd);\n";
        ss << "                                    }\n";
        ss << "                                }\n";
        ss << "                            }\n";
        ss << "                            unsubscribe_all_topics(client_fd);\n";
        ss << "                            unregister_ws_client(client_fd);\n";
        ss << "                        }).detach();\n";
        ss << "                    }\n";
        ss << "                }\n";

        for (const auto& api : program->apis) {
            for (const auto& r : api->routes) {
                if (r->method == "WEBSOCKET") {
                    ss << "                if (req.find(\"GET " << r->path << " \") != std::string::npos) {\n";
                    ss << "                    std::string wsKey = getHeaderValue(req, \"Sec-WebSocket-Key\");\n";
                    ss << "                    if (!wsKey.empty()) {\n";
                    ss << "                        std::string acceptKey = getWebSocketAcceptKey(wsKey);\n";
                    ss << "                        std::stringstream handshake;\n";
                    ss << "                        handshake << \"HTTP/1.1 101 Switching Protocols\\r\\n\"\n";
                    ss << "                                  << \"Upgrade: websocket\\r\\n\"\n";
                    ss << "                                  << \"Connection: Upgrade\\r\\n\"\n";
                    ss << "                                  << \"Sec-WebSocket-Accept: \" << acceptKey << \"\\r\\n\\r\\n\";\n";
                    ss << "                        send(client_fd, handshake.str().c_str(), handshake.str().length(), 0);\n";
                    ss << "                        wsUpgraded = true;\n";
                    ss << "                        std::thread([client_fd]() {\n";
                    ss << "                            register_ws_client(client_fd);\n";
                    ss << "                            std::string msg;\n";
                    ss << "                            while (readWebSocketFrame(client_fd, msg)) {\n";
                    ss << "                                if (!msg.empty()) {\n";
                    ss << "                                    std::cout << \"[WebSocket] Received: \" << msg << std::endl;\n";

                    size_t dotPos = r->targetAction.find('.');
                    std::string sliceName = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                    std::string actionName = (dotPos != std::string::npos) ? r->targetAction.substr(dotPos + 1) : r->targetAction;
                    if (!sliceName.empty()) {
                        ss << "                                    " << sliceName << " instance;\n";
                        for (const auto& slice : program->slices) {
                            if (slice->name == sliceName) {
                                for (const auto& field : slice->fields) {
                                    ss << "                                    {\n";
                                    ss << "                                        std::string val = getJSONVal(msg, \"" << field->name << "\");\n";
                                    if (field->type == DataType::INT || field->type == DataType::RELATION) {
                                        ss << "                                        if (!val.empty()) instance." << field->name << " = safeStoi(val);\n";
                                    } else if (field->type == DataType::STRING) {
                                        ss << "                                        if (!val.empty()) instance." << field->name << " = val;\n";
                                    } else if (field->type == DataType::FLOAT) {
                                        ss << "                                        if (!val.empty()) instance." << field->name << " = safeStod(val);\n";
                                    } else if (field->type == DataType::BOOL) {
                                        ss << "                                        if (!val.empty()) instance." << field->name << " = (val == \"true\" || val == \"1\");\n";
                                    }
                                    ss << "                                    }\n";
                                }
                            }
                        }
                        ss << "                                    instance.save();\n";
                        ss << "                                    instance." << actionName << "();\n";
                    }

                    ss << "                                    broadcast_ws_message(msg, client_fd);\n";
                    ss << "                                }\n";
                    ss << "                            }\n";
                    ss << "                            unregister_ws_client(client_fd);\n";
                    ss << "                        }).detach();\n";
                    ss << "                    }\n";
                    ss << "                }\n";
                }
            }
        }

        ss << "            }\n";
        ss << "            if (wsUpgraded) co_return;\n";

        // Dynamic multi-view routing
        bool isFirstRoute = true;

        // Admin Portal Page
        ss << "            if (req.rfind(\"GET /admin \", 0) == 0 || req.find(\"GET /admin?\") != std::string::npos) {\n";
        ss << "                std::string html = HTML_ADMIN;\n";
        ss << "                std::stringstream resp;\n";
        ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
        ss << "                     << \"Content-Type: text/html; charset=utf-8\\r\\n\"\n";
        ss << "                     << \"Content-Length: \" << html.length() << \"\\r\\n\\r\\n\"\n";
        ss << "                     << html;\n";
        ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
        ss << "            }\n";
        isFirstRoute = false;

        // Admin CRUD APIs
        for (const auto& slice : program->slices) {
            ss << "            else if (req.find(\"GET /api/admin/" << slice->name << "\") != std::string::npos) {\n";
            ss << "                std::string json = " << slice->name << "::getAllAsJSON(req);\n";
            ss << "                std::stringstream resp;\n";
            ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
            ss << "                     << \"Content-Type: application/json\\r\\n\"\n";
            ss << "                     << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
            ss << "                     << \"Content-Length: \" << json.length() << \"\\r\\n\\r\\n\"\n";
            ss << "                     << json;\n";
            ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "            }\n";

            ss << "            else if (req.find(\"POST /api/admin/" << slice->name << "\") != std::string::npos) {\n";
            ss << "                size_t bodyPos = req.find(\"\\r\\n\\r\\n\");\n";
            ss << "                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : \"\";\n";
            ss << "                " << slice->name << " instance;\n";
            for (const auto& field : slice->fields) {
                ss << "                {\n";
                ss << "                    std::string val = getJSONVal(body, \"" << field->name << "\");\n";
                if (field->type == DataType::INT || field->type == DataType::RELATION) {
                    ss << "                    if (!val.empty()) instance." << field->name << " = safeStoi(val);\n";
                } else if (field->type == DataType::STRING) {
                    ss << "                    if (!val.empty()) instance." << field->name << " = val;\n";
                } else if (field->type == DataType::FLOAT) {
                    ss << "                    if (!val.empty()) instance." << field->name << " = safeStod(val);\n";
                } else if (field->type == DataType::BOOL) {
                    ss << "                    if (!val.empty()) instance." << field->name << " = (val == \"true\" || val == \"1\");\n";
                }
                ss << "                }\n";
            }
            ss << "                instance.save();\n";
            ss << "                std::string msg = \"{\\\"status\\\":\\\"success\\\"}\";\n";
            ss << "                std::stringstream resp;\n";
            ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
            ss << "                     << \"Content-Type: application/json\\r\\n\"\n";
            ss << "                     << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
            ss << "                     << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
            ss << "                     << msg;\n";
            ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "            }\n";

            ss << "            else if (req.find(\"DELETE /api/admin/" << slice->name << "\") != std::string::npos) {\n";
            ss << "                size_t bodyPos = req.find(\"\\r\\n\\r\\n\");\n";
            ss << "                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : \"\";\n";
            std::string firstField = "";
            if (!slice->fields.empty()) {
                firstField = slice->fields[0]->name;
            }
            if (!firstField.empty()) {
                ss << "                std::string valToDelete = getJSONVal(body, \"" << firstField << "\");\n";
                ss << "                " << slice->name << "::deleteRecord(\"" << firstField << "\", valToDelete);\n";
            }
            ss << "                std::string msg = \"{\\\"status\\\":\\\"success\\\"}\";\n";
            ss << "                std::stringstream resp;\n";
            ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
            ss << "                     << \"Content-Type: application/json\\r\\n\"\n";
            ss << "                     << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
            ss << "                     << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
            ss << "                     << msg;\n";
            ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "            }\n";
        }

        if (program->views.empty()) {
            ss << "            else if (req.rfind(\"GET / \", 0) == 0 || req.rfind(\"GET /index.html\", 0) == 0 || req.rfind(\"GET /home \", 0) == 0) {\n";
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
            bool isFirstView = true;
            for (const auto& view : program->views) {
                std::string viewNameLower = view->name;
                std::transform(viewNameLower.begin(), viewNameLower.end(), viewNameLower.begin(), ::tolower);
                
                if (isFirstView) {
                    ss << "            else if (req.rfind(\"GET /" << viewNameLower << " \", 0) == 0 || req.rfind(\"GET / \", 0) == 0 || req.rfind(\"GET /index.html\", 0) == 0) {\n";
                    isFirstView = false;
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

        // Static file serving for uploads (Pillar 4)
        ss << "            else if (req.rfind(\"GET /public/uploads/\", 0) == 0) {\n";
        ss << "                size_t space = req.find(' ', 4);\n";
        ss << "                std::string path = (space != std::string::npos) ? req.substr(4, space - 4) : \"\";\n";
        ss << "                if (!path.empty() && path[0] == '/') path = path.substr(1);\n";
        ss << "                std::ifstream file(path, std::ios::binary);\n";
        ss << "                if (file.is_open()) {\n";
        ss << "                    file.seekg(0, std::ios::end);\n";
        ss << "                    size_t size = file.tellg();\n";
        ss << "                    file.seekg(0, std::ios::beg);\n";
        ss << "                    std::vector<char> fileBuf(size);\n";
        ss << "                    file.read(fileBuf.data(), size);\n";
        ss << "                    file.close();\n";
        ss << "                    std::string contentType = \"application/octet-stream\";\n";
        ss << "                    if (path.find(\".png\") != std::string::npos) contentType = \"image/png\";\n";
        ss << "                    else if (path.find(\".jpg\") != std::string::npos || path.find(\".jpeg\") != std::string::npos) contentType = \"image/jpeg\";\n";
        ss << "                    else if (path.find(\".gif\") != std::string::npos) contentType = \"image/gif\";\n";
        ss << "                    std::stringstream resp;\n";
        ss << "                    resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
        ss << "                         << \"Content-Type: \" << contentType << \"\\r\\n\"\n";
        ss << "                         << \"Content-Length: \" << size << \"\\r\\n\"\n";
        ss << "                         << \"Access-Control-Allow-Origin: *\\r\\n\\r\\n\";\n";
        ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
        ss << "                    send(client_fd, fileBuf.data(), size, 0);\n";
        ss << "                } else {\n";
        ss << "                    std::string msg = \"File Not Found\";\n";
        ss << "                    std::stringstream resp;\n";
        ss << "                    resp << \"HTTP/1.1 404 Not Found\\r\\n\"\n";
        ss << "                         << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
        ss << "                         << msg;\n";
        ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
        ss << "                }\n";
        ss << "            }\n";

        // Route serving database tables queries
        for (const auto& slice : program->slices) {
            if (isFirstRoute) {
                ss << "            if (req.find(\"GET /api/" << slice->name << "\") != std::string::npos) {\n";
                isFirstRoute = false;
            } else {
                ss << "            else if (req.find(\"GET /api/" << slice->name << "\") != std::string::npos) {\n";
            }
            {
                bool _hasRel = false;
                for (const auto& f : slice->fields) if (f->type == DataType::RELATION && !f->relatedSlice.empty()) _hasRel = true;
                if (_hasRel)
                    ss << "                std::string json = applyPreloads_" << slice->name << "(" << slice->name << "::getAllAsJSON(req), req);\n";
                else
                    ss << "                std::string json = " << slice->name << "::getAllAsJSON(req);\n";
            }
            ss << "                std::stringstream resp;\n";
            ss << "                resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
            ss << "                     << \"Content-Type: application/json\\r\\n\"\n";
            ss << "                     << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
            ss << "                     << \"Content-Length: \" << json.length() << \"\\r\\n\\r\\n\"\n";
            ss << "                     << json;\n";
            ss << "                send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
            ss << "            }\n";
        }

        // Generate register and login endpoints if User slice is present
        {
            std::shared_ptr<ASTSlice> userSlice = findUserSlice(program);
            if (userSlice) {
                std::string emailField = getEmailFieldName(userSlice);
                std::string passwordField = getPasswordFieldName(userSlice);
                std::string roleField = getRoleFieldName(userSlice);
                if (!emailField.empty() && !passwordField.empty()) {
                    if (isFirstRoute) {
                        ss << "            if (req.find(\"POST /api/signup\") != std::string::npos || req.find(\"POST /api/" << userSlice->name << "/signup\") != std::string::npos) {\n";
                        isFirstRoute = false;
                    } else {
                        ss << "            else if (req.find(\"POST /api/signup\") != std::string::npos || req.find(\"POST /api/" << userSlice->name << "/signup\") != std::string::npos) {\n";
                    }
                    ss << "                size_t bodyPos = req.find(\"\\r\\n\\r\\n\");\n";
                    ss << "                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : \"\";\n";
                    ss << "                std::string emailVal = getJSONVal(body, \"" << emailField << "\");\n";
                    ss << "                std::string passVal = getJSONVal(body, \"" << passwordField << "\");\n";
                    ss << "                if (emailVal.empty() || passVal.empty()) {\n";
                    ss << "                    std::string msg = \"{\\\"status\\\":\\\"error\\\",\\\"message\\\":\\\"Missing email or password\\\"}\";\n";
                    ss << "                    std::stringstream resp;\n";
                    ss << "                    resp << \"HTTP/1.1 400 Bad Request\\r\\n\"\n";
                    ss << "                         << \"Content-Type: application/json\\r\\n\"\n";
                    ss << "                         << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
                    ss << "                         << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
                    ss << "                         << msg;\n";
                    ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                    ss << "                } else {\n";
                    ss << "                    " << userSlice->name << " existing;\n";
                    ss << "                    if (" << userSlice->name << "::findUser(emailVal, existing)) {\n";
                    ss << "                        std::string msg = \"{\\\"status\\\":\\\"error\\\",\\\"message\\\":\\\"User already exists\\\"}\";\n";
                    ss << "                        std::stringstream resp;\n";
                    ss << "                        resp << \"HTTP/1.1 400 Bad Request\\r\\n\"\n";
                    ss << "                             << \"Content-Type: application/json\\r\\n\"\n";
                    ss << "                             << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
                    ss << "                             << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
                    ss << "                             << msg;\n";
                    ss << "                        send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                    ss << "                    } else {\n";
                    ss << "                        " << userSlice->name << " user;\n";
                    for (const auto& f : userSlice->fields) {
                        if (f->name == passwordField) {
                            ss << "                        user." << f->name << " = hashPassword(passVal);\n";
                        } else if (f->name == emailField) {
                            ss << "                        user." << f->name << " = emailVal;\n";
                        } else {
                            if (f->type == DataType::INT || f->type == DataType::RELATION) {
                                ss << "                        user." << f->name << " = safeStoi(getJSONVal(body, \"" << f->name << "\"));\n";
                            } else if (f->type == DataType::STRING) {
                                ss << "                        user." << f->name << " = getJSONVal(body, \"" << f->name << "\");\n";
                            } else if (f->type == DataType::FLOAT) {
                                ss << "                        user." << f->name << " = safeStod(getJSONVal(body, \"" << f->name << "\"));\n";
                            } else if (f->type == DataType::BOOL) {
                                ss << "                        user." << f->name << " = (getJSONVal(body, \"" << f->name << "\") == \"true\");\n";
                            }
                        }
                    }
                    ss << "                        user.save();\n";
                    ss << "                        std::string msg = \"{\\\"status\\\":\\\"success\\\",\\\"message\\\":\\\"User registered successfully\\\"}\";\n";
                    ss << "                        std::stringstream resp;\n";
                    ss << "                        resp << \"HTTP/1.1 201 Created\\r\\n\"\n";
                    ss << "                             << \"Content-Type: application/json\\r\\n\"\n";
                    ss << "                             << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
                    ss << "                             << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
                    ss << "                             << msg;\n";
                    ss << "                        send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                    ss << "                    }\n";
                    ss << "                }\n";
                    ss << "            }\n";

                    ss << "            else if (req.find(\"POST /api/login\") != std::string::npos || req.find(\"POST /api/" << userSlice->name << "/login\") != std::string::npos) {\n";
                    ss << "                size_t bodyPos = req.find(\"\\r\\n\\r\\n\");\n";
                    ss << "                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : \"\";\n";
                    ss << "                std::string emailVal = getJSONVal(body, \"" << emailField << "\");\n";
                    ss << "                std::string passVal = getJSONVal(body, \"" << passwordField << "\");\n";
                    ss << "                " << userSlice->name << " user;\n";
                    ss << "                if (emailVal.empty() || passVal.empty() || !" << userSlice->name << "::findUser(emailVal, user) || !verifyPassword(passVal, user." << passwordField << ")) {\n";
                    ss << "                    std::string msg = \"{\\\"status\\\":\\\"error\\\",\\\"message\\\":\\\"Invalid email or password\\\"}\";\n";
                    ss << "                    std::stringstream resp;\n";
                    ss << "                    resp << \"HTTP/1.1 401 Unauthorized\\r\\n\"\n";
                    ss << "                         << \"Content-Type: application/json\\r\\n\"\n";
                    ss << "                         << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
                    ss << "                         << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
                    ss << "                         << msg;\n";
                    ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                    ss << "                } else {\n";
                    ss << "                    std::stringstream payload;\n";
                    ss << "                    payload << \"{\\\"email\\\":\\\"\" << emailVal << \"\\\"\";\n";
                    if (!roleField.empty()) {
                        ss << "                    payload << \",\\\"rol\\\":\\\"\" << user." << roleField << " << \"\\\"\";\n";
                    }
                    ss << "                    payload << \"}\";\n";
                    ss << "                    std::string token = generateSessionToken(payload.str());\n";
                    ss << "                    std::stringstream msg;\n";
                    ss << "                    msg << \"{\\\"status\\\":\\\"success\\\",\\\"token\\\":\\\"\" << token << \"\\\"}\";\n";
                    ss << "                    std::stringstream resp;\n";
                    ss << "                    resp << \"HTTP/1.1 200 OK\\r\\n\"\n";
                    ss << "                         << \"Content-Type: application/json\\r\\n\"\n";
                    ss << "                         << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
                    ss << "                         << \"Content-Length: \" << msg.str().length() << \"\\r\\n\\r\\n\"\n";
                    ss << "                         << msg.str();\n";
                    ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                    ss << "                }\n";
                    ss << "            }\n";
                }
            }
        }

        // Routing for API endpoints
        if (!program->apis.empty()) {
            for (const auto& r : program->allRoutes()) {
                bool isDynamic = (r->path.find(':') != std::string::npos);
                // Use exact segment matching for ALL routes (not substring find), so a
                // route like "/" no longer shadows "/api/leads/:id". matchDynamicRoute
                // handles static patterns (no :params) as plain segment equality.
                std::string cond = "matchDynamicRoute(req, \"" + r->method + "\", \"" + r->path + "\", pathParams)";
                if (isFirstRoute) {
                    ss << "            if (" << cond << ") {\n";
                    isFirstRoute = false;
                } else {
                    ss << "            else if (" << cond << ") {\n";
                }
                // Expose captured path params as typed-friendly locals and bind them
                // to matching slice fields below.
                std::vector<std::string> pathParamNames;
                if (isDynamic) {
                    std::vector<std::string> segs;
                    {
                        size_t i = 0;
                        while (i < r->path.size()) {
                            if (r->path[i] == '/') { i++; continue; }
                            size_t j = r->path.find('/', i);
                            if (j == std::string::npos) j = r->path.size();
                            segs.push_back(r->path.substr(i, j - i));
                            i = j;
                        }
                    }
                    for (const auto& seg : segs) {
                        if (!seg.empty() && seg[0] == ':') {
                            std::string pname = seg.substr(1);
                            pathParamNames.push_back(pname);
                            ss << "                std::string param_" << pname
                               << " = pathParams.count(\"" << pname << "\") ? pathParams[\"" << pname << "\"] : \"\";\n";
                        }
                    }
                }
                if (r->isSecure) {
                    ss << "                std::string authHeader = getHeaderValue(req, \"Authorization\");\n";
                    ss << "                bool isAuth = false;\n";
                    ss << "                std::string tokenPayload;\n";
                    ss << "                if (authHeader.rfind(\"Bearer \", 0) == 0) {\n";
                    ss << "                    std::string token = authHeader.substr(7);\n";
                    ss << "                    isAuth = verifySessionToken(token, tokenPayload);\n";
                    ss << "                }\n";
                    ss << "                if (!isAuth) {\n";
                    ss << "                    std::string msg = \"{\\\"status\\\":\\\"error\\\",\\\"message\\\":\\\"Unauthorized\\\"}\";\n";
                    ss << "                    std::stringstream resp;\n";
                    ss << "                    resp << \"HTTP/1.1 401 Unauthorized\\r\\n\"\n";
                    ss << "                         << \"Content-Type: application/json\\r\\n\"\n";
                    ss << "                         << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
                    ss << "                         << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
                    ss << "                         << msg;\n";
                    ss << "                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                    ss << "                    close(client_fd);\n";
                    ss << "                    co_return;\n";
                    ss << "                }\n";
                }
                ss << "                size_t bodyPos = req.find(\"\\r\\n\\r\\n\");\n";
                ss << "                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : \"\";\n";
                ss << "                bool isMultipart = false;\n";
                ss << "                std::vector<MultipartPart> mpParts;\n";
                ss << "                std::string contentType = getHeaderValue(req, \"Content-Type\");\n";
                ss << "                if (contentType.find(\"multipart/form-data\") != std::string::npos) {\n";
                ss << "                    size_t bPos = contentType.find(\"boundary=\");\n";
                ss << "                    if (bPos != std::string::npos) {\n";
                ss << "                        std::string boundary = contentType.substr(bPos + 9);\n";
                ss << "                        if (!boundary.empty() && boundary.front() == '\"') boundary = boundary.substr(1);\n";
                ss << "                        if (!boundary.empty() && boundary.back() == '\"') boundary.pop_back();\n";
                ss << "                        boundary = \"--\" + boundary;\n";
                ss << "                        mpParts = parseMultipart(body, boundary);\n";
                ss << "                        isMultipart = true;\n";
                ss << "                    }\n";
                ss << "                }\n";
                
                ss << "                std::cout << \"[HTTP Endpoint] Invoked " << r->path << " -> Running " << r->targetAction << "\" << std::endl;\n";
                
                size_t dotPos = r->targetAction.find('.');
                std::string sliceName = (dotPos != std::string::npos) ? r->targetAction.substr(0, dotPos) : "";
                std::string actionName = (dotPos != std::string::npos) ? r->targetAction.substr(dotPos + 1) : r->targetAction;

                if (!sliceName.empty()) {
                    if (r->method == "DELETE") {
                        // Prefer deleting by a path param (e.g. DELETE /api/leads/:id)
                        // matching a field name; otherwise fall back to the body.
                        std::string delField = "";
                        std::string delValueExpr = "";
                        for (const auto& s : program->slices) {
                            if (s->name == sliceName) {
                                for (const auto& f : s->fields) {
                                    for (const auto& pn : pathParamNames) {
                                        if (pn == f->name) {
                                            delField = f->name;
                                            delValueExpr = "param_" + pn;
                                        }
                                    }
                                }
                                if (delField.empty() && !s->fields.empty()) {
                                    delField = s->fields[0]->name;
                                    delValueExpr = "getJSONVal(body, \"" + delField + "\")";
                                }
                            }
                        }
                        if (!delField.empty()) {
                            ss << "                std::string valToDelete = " << delValueExpr << ";\n";
                            ss << "                " << sliceName << "::deleteRecord(\"" << delField << "\", valToDelete);\n";
                        }
                        ss << "                " << sliceName << " instance;\n";
                        ss << "                instance." << actionName << "();\n";
                    } else {
                        ss << "                " << sliceName << " instance;\n";
                        for (const auto& slice : program->slices) {
                            if (slice->name == sliceName) {
                                for (const auto& field : slice->fields) {
                                    bool isPathParam = false;
                                    for (const auto& pn : pathParamNames) {
                                        if (pn == field->name) isPathParam = true;
                                    }
                                    ss << "                {\n";
                                    if (isPathParam) {
                                        ss << "                    std::string val = param_" << field->name << ";\n";
                                    } else {
                                        ss << "                    std::string val = isMultipart ? getMultipartVal(mpParts, \"" << field->name << "\") : getJSONVal(body, \"" << field->name << "\");\n";
                                    }
                                    if (field->type == DataType::INT || field->type == DataType::RELATION) {
                                        ss << "                    instance." << field->name << " = safeStoi(val);\n";
                                    } else if (field->type == DataType::STRING) {
                                        ss << "                    instance." << field->name << " = val;\n";
                                    } else if (field->type == DataType::FLOAT) {
                                        ss << "                    instance." << field->name << " = safeStod(val);\n";
                                    } else if (field->type == DataType::BOOL) {
                                        ss << "                    instance." << field->name << " = (val == \"true\" || val == \"1\");\n";
                                    }
                                    ss << "                }\n";
                                }
                            }
                        }
                        // Changeset validation: reject with 422 + all errors before writing.
                        ss << "                {\n";
                        ss << "                    auto _cs = instance.validateChangeset();\n";
                        ss << "                    if (!_cs.empty()) {\n";
                        ss << "                        std::stringstream ej;\n";
                        ss << "                        ej << \"{\\\"status\\\":\\\"error\\\",\\\"errors\\\":{\";\n";
                        ss << "                        bool _ef = true;\n";
                        ss << "                        for (auto& kv : _cs) { if (!_ef) ej << \",\"; _ef = false; ej << \"\\\"\" << kv.first << \"\\\":\\\"\" << kv.second << \"\\\"\"; }\n";
                        ss << "                        ej << \"}}\";\n";
                        ss << "                        std::string msg = ej.str();\n";
                        ss << "                        std::stringstream resp;\n";
                        ss << "                        resp << \"HTTP/1.1 422 Unprocessable Entity\\r\\n\" << \"Content-Type: application/json\\r\\n\" << \"Access-Control-Allow-Origin: *\\r\\n\" << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\" << msg;\n";
                        ss << "                        send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
                        ss << "                        close(client_fd);\n";
                        ss << "                        co_return;\n";
                        ss << "                    }\n";
                        ss << "                }\n";
                        ss << "                instance.save();\n";
                        ss << "                instance." << actionName << "();\n";
                        ss << "                broadcast_ws_message(\"{\\\"event\\\": \\\"action\\\", \\\"target\\\": \\\"" << sliceName << "." << actionName << "\\\"}\");\n";
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

        // Global exception handler: any uncaught std::exception (or unknown throw)
        // during request dispatch becomes a clean HTTP 500 instead of crashing the
        // whole server process.
        ss << "        } catch (const std::exception& ex) {\n";
        ss << "            std::cerr << \"[Hexagen 500] Unhandled exception: \" << ex.what() << std::endl;\n";
        ss << "            std::string msg = std::string(\"{\\\"status\\\":\\\"error\\\",\\\"message\\\":\\\"Internal Server Error\\\"}\");\n";
        ss << "            std::stringstream resp;\n";
        ss << "            resp << \"HTTP/1.1 500 Internal Server Error\\r\\n\"\n";
        ss << "                 << \"Content-Type: application/json\\r\\n\"\n";
        ss << "                 << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
        ss << "                 << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
        ss << "                 << msg;\n";
        ss << "            send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
        ss << "        } catch (...) {\n";
        ss << "            std::cerr << \"[Hexagen 500] Unhandled non-standard exception\" << std::endl;\n";
        ss << "            std::string msg = std::string(\"{\\\"status\\\":\\\"error\\\",\\\"message\\\":\\\"Internal Server Error\\\"}\");\n";
        ss << "            std::stringstream resp;\n";
        ss << "            resp << \"HTTP/1.1 500 Internal Server Error\\r\\n\"\n";
        ss << "                 << \"Content-Type: application/json\\r\\n\"\n";
        ss << "                 << \"Access-Control-Allow-Origin: *\\r\\n\"\n";
        ss << "                 << \"Content-Length: \" << msg.length() << \"\\r\\n\\r\\n\"\n";
        ss << "                 << msg;\n";
        ss << "            send(client_fd, resp.str().c_str(), resp.str().length(), 0);\n";
        ss << "        }\n";

        ss << "    }\n";
        ss << "    close(client_fd);\n";
        ss << "}\n\n";

        ss << "int main() {\n";
        ss << "    initDatabase();\n";
        ss << "    // Background jobs: recover persisted jobs, then start a supervised worker pool\n";
        ss << "    recover_persisted_jobs();\n";
        ss << "    start_job_supervisor(std::atoi(getEnvOr(\"JOB_WORKERS\", \"4\")));\n";
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

        if (program->target == "desktop") {
            ss << "    std::thread([server_fd, address, addrlen, port]() mutable {\n";
            ss << "        int client_fd;\n";
            ss << "        while (true) {\n";
            ss << "            if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;\n";
            ss << "            handle_client(client_fd, address, addrlen);\n";
            ss << "        }\n";
            ss << "    }).detach();\n\n";
            ss << "    // Start webview event loop\n";
            ss << "    try {\n";
            ss << "        webview::webview w(true, nullptr);\n";
            ss << "        w.set_title(\"Hexagen Desktop App\");\n";
            ss << "        w.set_size(1024, 768, WEBVIEW_HINT_NONE);\n";
            ss << "        w.navigate(\"http://localhost:\" + std::to_string(port));\n";
            ss << "        w.run();\n";
            ss << "    } catch (const std::exception& e) {\n";
            ss << "        std::cerr << \"WebView error: \" << e.what() << std::endl;\n";
            ss << "    }\n";
        } else {
            ss << "    while (true) {\n";
            ss << "        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;\n";
            ss << "        handle_client(client_fd, address, addrlen);\n";
            ss << "    }\n";
        }
        ss << "    close(server_fd);\n";
        ss << "    return 0;\n";
        ss << "}\n";
    }

    return ss.str();
}

std::string CodeGenerator::generateAdminHTML() {
    std::stringstream ss;
    ss << R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Hexagen Admin Portal</title>
<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">
<style>
body {
    margin: 0;
    font-family: 'Outfit', sans-serif;
    background: #0b0f19;
    color: #f3f4f6;
    display: flex;
    height: 100vh;
}
sidebar {
    width: 260px;
    background: #111827;
    border-right: 1px solid #1f2937;
    padding: 24px;
    display: flex;
    flex-direction: column;
}
sidebar h2 {
    font-size: 20px;
    margin: 0 0 24px 0;
    background: linear-gradient(135deg, #00f2fe 0%, #4facfe 100%);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    font-weight: 800;
}
.slice-btn {
    padding: 12px 16px;
    border-radius: 12px;
    cursor: pointer;
    margin-bottom: 8px;
    transition: all 0.2s;
    background: transparent;
    border: none;
    color: #9ca3af;
    text-align: left;
    font-size: 16px;
    width: 100%;
}
.slice-btn:hover, .slice-btn.active {
    background: rgba(0, 242, 254, 0.1);
    color: #00f2fe;
}
content {
    flex: 1;
    padding: 40px;
    overflow-y: auto;
}
.card {
    background: rgba(17, 24, 39, 0.7);
    backdrop-filter: blur(10px);
    border: 1px solid rgba(255,255,255,0.05);
    border-radius: 24px;
    padding: 32px;
    box-shadow: 0 20px 40px rgba(0,0,0,0.3);
}
table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 24px;
}
th, td {
    padding: 16px;
    text-align: left;
    border-bottom: 1px solid #1f2937;
}
th {
    color: #9ca3af;
    font-weight: 600;
}
.btn {
    background: linear-gradient(135deg, #00f2fe 0%, #4facfe 100%);
    border: none;
    color: white;
    padding: 10px 20px;
    border-radius: 12px;
    cursor: pointer;
    font-weight: 600;
    transition: transform 0.2s;
}
.btn:hover {
    transform: scale(1.03);
}
.btn-danger {
    background: linear-gradient(135deg, #f87171 0%, #ef4444 100%);
}
.modal {
    display: none;
    position: fixed;
    top: 0; left: 0; width: 100%; height: 100%;
    background: rgba(0,0,0,0.6);
    justify-content: center; align-items: center;
    backdrop-filter: blur(5px);
}
.modal-content {
    background: #111827;
    border: 1px solid #1f2937;
    padding: 32px;
    border-radius: 24px;
    width: 400px;
}
.input-group {
    margin-bottom: 16px;
}
.input-group label {
    display: block; margin-bottom: 8px; color: #9ca3af;
}
.input-group input {
    width: 100%; padding: 10px; border-radius: 8px; border: 1px solid #1f2937; background: #1f2937; color: white; box-sizing: border-box;
}
</style>
</head>
<body>
<sidebar>
    <h2>Hexagen Admin</h2>
    <div id="slice-list"></div>
</sidebar>
<content>
    <div class="card" id="main-card">
        <h1 id="slice-title">Welcome to Admin Portal</h1>
        <p>Select a slice from the sidebar to manage database records.</p>
    </div>
</content>

<div class="modal" id="add-modal">
    <div class="modal-content">
        <h3 style="margin-top:0;">Add New Record</h3>
        <form id="add-form"></form>
        <div style="display:flex; justify-content: flex-end; gap: 12px; margin-top: 24px;">
            <button class="btn btn-danger" onclick="closeModal()">Cancel</button>
            <button class="btn" onclick="submitRecord()">Save</button>
        </div>
    </div>
</div>

<script>
)HTML";

    ss << "const slices = {\n";
    for (const auto& slice : program->slices) {
        ss << "    \"" << slice->name << "\": [\n";
        for (size_t i = 0; i < slice->fields.size(); ++i) {
            ss << "        { \"name\": \"" << slice->fields[i]->name << "\", \"type\": \"" << dataTypeToString(slice->fields[i]->type) << "\" }";
            if (i + 1 < slice->fields.size()) ss << ",";
            ss << "\n";
        }
        ss << "    ],\n";
    }
    ss << "};\n";

    ss << R"HTML(
let activeSlice = '';

function renderSidebar() {
    const list = document.getElementById('slice-list');
    list.innerHTML = '';
    Object.keys(slices).forEach(s => {
        const btn = document.createElement('button');
        btn.className = 'slice-btn';
        btn.innerText = s;
        btn.onclick = () => selectSlice(s);
        list.appendChild(btn);
    });
}

async function selectSlice(name) {
    activeSlice = name;
    document.querySelectorAll('.slice-btn').forEach(btn => {
        btn.classList.toggle('active', btn.innerText === name);
    });
    
    const fields = slices[name];
    const card = document.getElementById('main-card');
    card.innerHTML = `
        <div style="display:flex; justify-content:space-between; align-items:center;">
            <h1 style="margin:0;">${name}</h1>
            <button class="btn" onclick="openAddModal()">Add Record</button>
        </div>
        <div style="overflow-x:auto;">
            <table>
                <thead>
                    <tr>
                        ${fields.map(f => `<th>${f.name}</th>`).join('')}
                        <th>Actions</th>
                    </tr>
                </thead>
                <tbody id="table-body"></tbody>
            </table>
        </div>
    `;
    loadTableData();
}

async function loadTableData() {
    const res = await fetch('/api/admin/' + activeSlice);
    const data = await res.json();
    const tbody = document.getElementById('table-body');
    tbody.innerHTML = '';
    data.forEach(row => {
        const tr = document.createElement('tr');
        const fields = slices[activeSlice];
        tr.innerHTML = fields.map(f => `<td>${row[f.name]}</td>`).join('') + 
            `<td><button class="btn btn-danger" onclick="deleteRecord('${row[fields[0].name]}')">Delete</button></td>`;
        tbody.appendChild(tr);
    });
}

function openAddModal() {
    const form = document.getElementById('add-form');
    form.innerHTML = '';
    slices[activeSlice].forEach(f => {
        form.innerHTML += `
            <div class="input-group">
                <label>${f.name} (${f.type})</label>
                <input type="${f.type === 'int' || f.type === 'float' ? 'number' : 'text'}" name="${f.name}" required>
            </div>
        `;
    });
    document.getElementById('add-modal').style.display = 'flex';
}

function closeModal() {
    document.getElementById('add-modal').style.display = 'none';
}

async function submitRecord() {
    const form = document.getElementById('add-form');
    const data = {};
    slices[activeSlice].forEach(f => {
        const val = form.elements[f.name].value;
        data[f.name] = f.type === 'int' ? parseInt(val) : f.type === 'float' ? parseFloat(val) : val;
    });
    await fetch('/api/admin/' + activeSlice, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    });
    closeModal();
    loadTableData();
}

async function deleteRecord(id) {
    if(!confirm('Are you sure you want to delete this record?')) return;
    const fields = slices[activeSlice];
    const payload = {};
    payload[fields[0].name] = id;
    await fetch('/api/admin/' + activeSlice, {
        method: 'DELETE',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });
    loadTableData();
}

renderSidebar();
</script>
</body>
</html>
)HTML";

    return ss.str();
}

