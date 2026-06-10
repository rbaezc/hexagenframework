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

    // Dynamic Database Save Method (Pillar 1)
    ss << "    void save() {\n";
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

    // Dynamic Database Get All Method (Pillar 1)
    ss << "    static std::string getAllAsJSON() {\n";
    ss << "        std::ifstream infile(\"db_" << slice->name << ".jsonl\");\n";
    ss << "        std::stringstream ss;\n";
    ss << "        ss << \"[\";\n";
    ss << "        if (infile.is_open()) {\n";
    ss << "            std::string line;\n";
    ss << "            bool first = true;\n";
    ss << "            while (std::getline(infile, line)) {\n";
    ss << "                if (line.empty()) continue;\n";
    ss << "                if (!first) ss << \",\";\n";
    ss << "                ss << line;\n";
    ss << "                first = false;\n";
    ss << "            }\n";
    ss << "            infile.close();\n";
    ss << "        }\n";
    ss << "        ss << \"]\";\n";
    ss << "        return ss.str();\n";
    ss << "    }\n\n";

    // Dynamic Database Delete Method (Pillar 1)
    ss << "    static void deleteRecord(const std::string& key, const std::string& value) {\n";
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
                ss << "                        rowHtml += `<td>\\${row." << col << " || ''}</td>`;\n";
            }
            if (hasDeleteRoute) {
                std::string keyCol = elem->columns.empty() ? "" : elem->columns[0];
                ss << "                        rowHtml += `<td><button class=\"btn\" style=\"padding:0.4rem 0.8rem; font-size:0.8rem; margin:0; width:auto; background:linear-gradient(135deg, #f43f5e 0%, #e11d48 100%); color:white;\" onclick=\"deleteRow('\\${row." << keyCol << "}', '" << deleteEndpoint << "')\">Eliminar</button></td>`;\n";
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
        ss << "        char buffer[8192] = {0};\n";
        ss << "        int valread = read(client_fd, buffer, 8192);\n";
        ss << "        if (valread > 0) {\n";
        ss << "            std::string req(buffer, valread);\n";
        
        // Dynamic multi-view routing
        bool isFirstView = true;
        for (const auto& view : program->views) {
            std::string viewNameLower = view->name;
            std::transform(viewNameLower.begin(), viewNameLower.end(), viewNameLower.begin(), ::tolower);
            
            if (isFirstView) {
                ss << "            if (req.rfind(\"GET /" << viewNameLower << " \", 0) == 0 || req.rfind(\"GET / \", 0) == 0 || req.rfind(\"GET /index.html\", 0) == 0) {\n";
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

        // Route serving database tables queries
        for (const auto& slice : program->slices) {
            ss << "            else if (req.find(\"GET /api/" << slice->name << "\") != std::string::npos) {\n";
            ss << "                std::string json = " << slice->name << "::getAllAsJSON();\n";
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
                ss << "            else if (req.find(\"" << r->method << " " << r->path << "\") != std::string::npos) {\n";
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
                                    if (field->type == DataType::INT) {
                                        ss << "std::stoi(getJSONVal(body, \"" << field->name << "\"));\n";
                                    } else if (field->type == DataType::STRING) {
                                        ss << "getJSONVal(body, \"" << field->name << "\");\n";
                                    } else if (field->type == DataType::FLOAT) {
                                        ss << "std::stof(getJSONVal(body, \"" << field->name << "\"));\n";
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
