#include "lexer.hpp"
#include "parser.hpp"
#include "codegen.hpp"
#include "security_analyzer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>
#include <map>
#include <regex>

namespace fs = std::filesystem;

void printUsage() {
    std::cout << "Hexagen Framework Compiler (hf)\n";
    std::cout << "Usage:\n";
    std::cout << "  hf new <project_name>               - Scaffold a new project workspace\n";
    std::cout << "  hf dev <file.hx>                    - Start live development dev server (Hot Reload)\n";
    std::cout << "  hf transpile <file.hx>              - Transpile to C++ source code\n";
    std::cout << "  hf compile <file.hx> -o <output>    - Transpile and compile to executable\n";
    std::cout << "  hf run <file.hx>                    - Transpile, compile, and run immediately\n";
    std::cout << "  hf ast <file.hx>                    - Print the AST of the input file\n";
    std::cout << "  hf help                             - Show this help message\n";
}

std::string readFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

struct ComponentData {
    std::string name;
    std::vector<std::string> props;
    std::string htmlContent;
};

std::string resolveConditions(std::string html, const std::map<std::string, std::string>& attrValues) {
    std::regex condRegex(R"(\{\{\s*if\s+(\w+)\s*(==|!=)\s*['\"]([^'\"]*)['\"]\s*\}\}([\s\S]*?)\{\{\s*endif\s*\}\})");
    std::smatch match;
    std::string result = html;
    
    while (std::regex_search(result, match, condRegex)) {
        std::string propName = match[1].str();
        std::string op = match[2].str();
        std::string targetVal = match[3].str();
        std::string innerHtml = match[4].str();
        
        std::string actualVal = "";
        if (attrValues.find(propName) != attrValues.end()) {
            actualVal = attrValues.at(propName);
        }
        
        bool conditionMet = false;
        if (op == "==") {
            conditionMet = (actualVal == targetVal);
        } else {
            conditionMet = (actualVal != targetVal);
        }
        
        std::string replacement = conditionMet ? innerHtml : "";
        result.replace(match.position(), match.length(), replacement);
    }
    return result;
}

void extractComponents(std::string& source, std::map<std::string, ComponentData>& componentsMap) {
    size_t pos = 0;
    while ((pos = source.find("component ", pos)) != std::string::npos) {
        if (pos > 0 && (std::isalnum(source[pos - 1]) || source[pos - 1] == '_')) {
            pos += 10;
            continue;
        }

        size_t nameStart = pos + 10;
        while (nameStart < source.length() && std::isspace(source[nameStart])) {
            nameStart++;
        }
        if (nameStart >= source.length()) break;

        size_t nameEnd = source.find_first_of(" \t\r\n{", nameStart);
        if (nameEnd == std::string::npos) {
            pos += 10;
            continue;
        }

        std::string compName = source.substr(nameStart, nameEnd - nameStart);

        size_t bracePos = source.find('{', nameEnd);
        if (bracePos == std::string::npos) {
            pos += 10;
            continue;
        }

        int braceCount = 1;
        size_t blockEnd = std::string::npos;
        bool inQuotes = false;
        char quoteChar = 0;

        for (size_t i = bracePos + 1; i < source.length(); ++i) {
            char c = source[i];
            if (inQuotes) {
                if (c == quoteChar && source[i-1] != '\\') {
                    inQuotes = false;
                }
            } else {
                if (c == '"' || c == '\'') {
                    inQuotes = true;
                    quoteChar = c;
                } else if (c == '{') {
                    braceCount++;
                } else if (c == '}') {
                    braceCount--;
                    if (braceCount == 0) {
                        blockEnd = i;
                        break;
                    }
                }
            }
        }

        if (blockEnd == std::string::npos) {
            throw std::runtime_error("Error: Unclosed component block for '" + compName + "'");
        }

        std::string compBody = source.substr(bracePos + 1, blockEnd - bracePos - 1);

        std::vector<std::string> props;
        std::string htmlContent = "";

        size_t bodyPos = 0;
        while (bodyPos < compBody.length()) {
            while (bodyPos < compBody.length() && std::isspace(compBody[bodyPos])) {
                bodyPos++;
            }
            if (bodyPos >= compBody.length()) break;

            if (compBody.compare(bodyPos, 5, "input") == 0 && std::isspace(compBody[bodyPos + 5])) {
                bodyPos += 6;
                while (bodyPos < compBody.length() && std::isspace(compBody[bodyPos])) bodyPos++;
                size_t idStart = bodyPos;
                while (bodyPos < compBody.length() && (std::isalnum(compBody[bodyPos]) || compBody[bodyPos] == '_')) {
                    bodyPos++;
                }
                std::string propName = compBody.substr(idStart, bodyPos - idStart);
                if (!propName.empty()) {
                    props.push_back(propName);
                }
                while (bodyPos < compBody.length() && compBody[bodyPos] != '\n' && compBody[bodyPos] != ';') {
                    bodyPos++;
                }
            } else if (compBody.compare(bodyPos, 4, "html") == 0 && (std::isspace(compBody[bodyPos + 4]) || compBody[bodyPos + 4] == '"')) {
                bodyPos += 4;
                while (bodyPos < compBody.length() && std::isspace(compBody[bodyPos])) bodyPos++;
                if (bodyPos < compBody.length() && compBody[bodyPos] == '"') {
                    size_t htmlStart = bodyPos + 1;
                    size_t htmlEnd = std::string::npos;
                    bool escaped = false;
                    for (size_t i = htmlStart; i < compBody.length(); ++i) {
                        if (compBody[i] == '\\' && !escaped) {
                            escaped = true;
                        } else {
                            if (compBody[i] == '"' && !escaped) {
                                htmlEnd = i;
                                break;
                            }
                            escaped = false;
                        }
                    }
                    if (htmlEnd != std::string::npos) {
                        htmlContent = compBody.substr(htmlStart, htmlEnd - htmlStart);
                        bodyPos = htmlEnd + 1;
                    } else {
                        bodyPos = htmlStart;
                    }
                } else {
                    bodyPos++;
                }
            } else {
                bodyPos++;
            }
        }

        ComponentData data = {compName, props, htmlContent};
        componentsMap[compName] = data;

        source.erase(pos, blockEnd - pos + 1);
    }

    // Recursively resolve components inside other components (composition)
    bool nestedChanged = true;
    int iterations = 0;
    while (nestedChanged && iterations < 10) {
        nestedChanged = false;
        iterations++;
        for (auto& [compName, compData] : componentsMap) {
            for (const auto& [otherCompName, otherCompData] : componentsMap) {
                if (compName == otherCompName) continue;
                std::string searchTag = "<" + otherCompName;
                size_t tagPos = 0;
                while ((tagPos = compData.htmlContent.find(searchTag, tagPos)) != std::string::npos) {
                    char nextChar = compData.htmlContent[tagPos + searchTag.length()];
                    if (!std::isspace(nextChar) && nextChar != '/' && nextChar != '>') {
                        tagPos++;
                        continue;
                    }

                    size_t endTagPos = std::string::npos;
                    bool inDoubleQuotes = false;
                    bool inSingleQuotes = false;
                    for (size_t i = tagPos + searchTag.length(); i < compData.htmlContent.length(); ++i) {
                        char c = compData.htmlContent[i];
                        if (c == '"' && compData.htmlContent[i-1] != '\\') inDoubleQuotes = !inDoubleQuotes;
                        else if (c == '\'' && compData.htmlContent[i-1] != '\\') inSingleQuotes = !inSingleQuotes;
                        else if (!inDoubleQuotes && !inSingleQuotes) {
                            if (c == '>') {
                                endTagPos = i;
                                break;
                            }
                        }
                    }
                    if (endTagPos == std::string::npos) {
                        tagPos++;
                        continue;
                    }

                    std::string fullTag = compData.htmlContent.substr(tagPos, endTagPos - tagPos + 1);
                    bool isSelfClosing = (fullTag.length() >= 2 && fullTag[fullTag.length() - 2] == '/');

                    std::map<std::string, std::string> attrValues;
                    std::string attrsStr = fullTag.substr(searchTag.length(), endTagPos - tagPos - searchTag.length() - (isSelfClosing ? 1 : 0));

                    size_t attrPos = 0;
                    while (attrPos < attrsStr.length()) {
                        while (attrPos < attrsStr.length() && std::isspace(attrsStr[attrPos])) attrPos++;
                        if (attrPos >= attrsStr.length()) break;
                        size_t keyStart = attrPos;
                        while (attrPos < attrsStr.length() && (std::isalnum(attrsStr[attrPos]) || attrsStr[attrPos] == '_' || attrsStr[attrPos] == '-')) attrPos++;
                        std::string key = attrsStr.substr(keyStart, attrPos - keyStart);
                        while (attrPos < attrsStr.length() && std::isspace(attrsStr[attrPos])) attrPos++;
                        if (attrPos < attrsStr.length() && attrsStr[attrPos] == '=') {
                            attrPos++;
                            while (attrPos < attrsStr.length() && std::isspace(attrsStr[attrPos])) attrPos++;
                            if (attrPos < attrsStr.length() && (attrsStr[attrPos] == '"' || attrsStr[attrPos] == '\'')) {
                                char quote = attrsStr[attrPos];
                                attrPos++;
                                size_t valStart = attrPos;
                                while (attrPos < attrsStr.length() && attrsStr[attrPos] != quote) attrPos++;
                                std::string val = attrsStr.substr(valStart, attrPos - valStart);
                                if (attrPos < attrsStr.length()) attrPos++;
                                attrValues[key] = val;
                            } else {
                                size_t valStart = attrPos;
                                while (attrPos < attrsStr.length() && !std::isspace(attrsStr[attrPos])) attrPos++;
                                attrValues[key] = attrsStr.substr(valStart, attrPos - valStart);
                            }
                        }
                    }

                    std::string replacement = otherCompData.htmlContent;
                    for (const auto& prop : otherCompData.props) {
                        std::string val = "";
                        if (attrValues.find(prop) != attrValues.end()) val = attrValues[prop];
                        std::string placeholder = "{{" + prop + "}}";
                        size_t subPos = 0;
                        while ((subPos = replacement.find(placeholder, subPos)) != std::string::npos) {
                            replacement.replace(subPos, placeholder.length(), val);
                            subPos += val.length();
                        }
                    }

                    replacement = resolveConditions(replacement, attrValues);

                    size_t totalReplaceLen = fullTag.length();
                    if (!isSelfClosing) {
                        std::string closingTag = "</" + otherCompName + ">";
                        size_t closeTagPos = compData.htmlContent.find(closingTag, endTagPos + 1);
                        if (closeTagPos != std::string::npos) {
                            totalReplaceLen = (closeTagPos + closingTag.length()) - tagPos;
                        }
                    }

                    compData.htmlContent.replace(tagPos, totalReplaceLen, replacement);
                    nestedChanged = true;
                }
            }
        }
    }
}

std::string resolveComponents(const std::string& source, const std::map<std::string, ComponentData>& componentsMap) {
    std::string result = source;
    size_t vPos = 0;
    while ((vPos = result.find("view ", vPos)) != std::string::npos) {
        if (vPos > 0 && (std::isalnum(result[vPos - 1]) || result[vPos - 1] == '_')) {
            vPos += 5;
            continue;
        }

        size_t nameStart = vPos + 5;
        while (nameStart < result.length() && std::isspace(result[nameStart])) nameStart++;
        if (nameStart >= result.length()) break;

        size_t nameEnd = result.find_first_of(" \t\r\n{", nameStart);
        if (nameEnd == std::string::npos) {
            vPos += 5;
            continue;
        }

        std::string viewName = result.substr(nameStart, nameEnd - nameStart);

        size_t bracePos = result.find('{', nameEnd);
        if (bracePos == std::string::npos) {
            vPos += 5;
            continue;
        }

        int braceCount = 1;
        size_t blockEnd = std::string::npos;
        bool inQuotes = false;
        char quoteChar = 0;

        for (size_t i = bracePos + 1; i < result.length(); ++i) {
            char c = result[i];
            if (inQuotes) {
                if (c == quoteChar && result[i-1] != '\\') inQuotes = false;
            } else {
                if (c == '"' || c == '\'') {
                    inQuotes = true;
                    quoteChar = c;
                } else if (c == '{') braceCount++;
                else if (c == '}') {
                    braceCount--;
                    if (braceCount == 0) {
                        blockEnd = i;
                        break;
                    }
                }
            }
        }

        if (blockEnd == std::string::npos) {
            vPos++;
            continue;
        }

        std::string viewBody = result.substr(bracePos + 1, blockEnd - bracePos - 1);

        std::vector<std::string> declaredComponents;
        size_t compDeclPos = std::string::npos;
        size_t compDeclLength = 0;

        std::vector<std::string> keywords = {"components", "standalone"};
        for (const auto& kw : keywords) {
            size_t kwPos = viewBody.find(kw);
            if (kwPos != std::string::npos) {
                size_t openBracket = viewBody.find('[', kwPos);
                size_t closeBracket = viewBody.find(']', kwPos);
                if (openBracket != std::string::npos && closeBracket != std::string::npos && openBracket < closeBracket) {
                    std::string listStr = viewBody.substr(openBracket + 1, closeBracket - openBracket - 1);
                    std::stringstream ss(listStr);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        item = trim(item);
                        if (!item.empty()) {
                            declaredComponents.push_back(item);
                        }
                    }
                    compDeclPos = kwPos;
                    compDeclLength = (closeBracket + 1) - kwPos;
                    break;
                }
            }
        }

        if (compDeclPos != std::string::npos) {
            viewBody.erase(compDeclPos, compDeclLength);
        }

        size_t htmlPos = 0;
        while ((htmlPos = viewBody.find("html ", htmlPos)) != std::string::npos) {
            htmlPos += 4;
            while (htmlPos < viewBody.length() && std::isspace(viewBody[htmlPos])) htmlPos++;
            if (htmlPos < viewBody.length() && viewBody[htmlPos] == '"') {
                size_t htmlStart = htmlPos + 1;
                size_t htmlEnd = std::string::npos;
                bool escaped = false;
                for (size_t i = htmlStart; i < viewBody.length(); ++i) {
                    if (viewBody[i] == '\\' && !escaped) {
                        escaped = true;
                    } else {
                        if (viewBody[i] == '"' && !escaped) {
                            htmlEnd = i;
                            break;
                        }
                        escaped = false;
                    }
                }

                if (htmlEnd != std::string::npos) {
                    std::string htmlContent = viewBody.substr(htmlStart, htmlEnd - htmlStart);
                    bool changed = false;

                    for (const auto& [compName, compData] : componentsMap) {
                        std::string searchTag = "<" + compName;
                        size_t tagPos = 0;
                        while ((tagPos = htmlContent.find(searchTag, tagPos)) != std::string::npos) {
                            char nextChar = htmlContent[tagPos + searchTag.length()];
                            if (!std::isspace(nextChar) && nextChar != '/' && nextChar != '>') {
                                tagPos++;
                                continue;
                            }

                            bool isDeclared = false;
                            for (const auto& dc : declaredComponents) {
                                if (dc == compName) {
                                    isDeclared = true;
                                    break;
                                }
                            }
                            if (!isDeclared) {
                                std::cerr << "Warning: Component '" << compName << "' used in view '" << viewName 
                                          << "' but not declared in components/standalone list!" << std::endl;
                            }

                            size_t endTagPos = std::string::npos;
                            bool inDoubleQuotes = false;
                            bool inSingleQuotes = false;
                            for (size_t i = tagPos + searchTag.length(); i < htmlContent.length(); ++i) {
                                char c = htmlContent[i];
                                if (c == '"' && htmlContent[i-1] != '\\') inDoubleQuotes = !inDoubleQuotes;
                                else if (c == '\'' && htmlContent[i-1] != '\\') inSingleQuotes = !inSingleQuotes;
                                else if (!inDoubleQuotes && !inSingleQuotes) {
                                    if (c == '>') {
                                        endTagPos = i;
                                        break;
                                    }
                                }
                            }

                            if (endTagPos == std::string::npos) {
                                tagPos++;
                                continue;
                            }

                            std::string fullTag = htmlContent.substr(tagPos, endTagPos - tagPos + 1);
                            bool isSelfClosing = (fullTag.length() >= 2 && fullTag[fullTag.length() - 2] == '/');

                            std::map<std::string, std::string> attrValues;
                            std::string attrsStr = fullTag.substr(searchTag.length(), endTagPos - tagPos - searchTag.length() - (isSelfClosing ? 1 : 0));

                            size_t attrPos = 0;
                            while (attrPos < attrsStr.length()) {
                                while (attrPos < attrsStr.length() && std::isspace(attrsStr[attrPos])) attrPos++;
                                if (attrPos >= attrsStr.length()) break;
                                size_t keyStart = attrPos;
                                while (attrPos < attrsStr.length() && (std::isalnum(attrsStr[attrPos]) || attrsStr[attrPos] == '_' || attrsStr[attrPos] == '-')) attrPos++;
                                std::string key = attrsStr.substr(keyStart, attrPos - keyStart);
                                while (attrPos < attrsStr.length() && std::isspace(attrsStr[attrPos])) attrPos++;
                                if (attrPos < attrsStr.length() && attrsStr[attrPos] == '=') {
                                    attrPos++;
                                    while (attrPos < attrsStr.length() && std::isspace(attrsStr[attrPos])) attrPos++;
                                    if (attrPos < attrsStr.length() && (attrsStr[attrPos] == '"' || attrsStr[attrPos] == '\'')) {
                                        char quote = attrsStr[attrPos];
                                        attrPos++;
                                        size_t valStart = attrPos;
                                        while (attrPos < attrsStr.length() && attrsStr[attrPos] != quote) attrPos++;
                                        std::string val = attrsStr.substr(valStart, attrPos - valStart);
                                        if (attrPos < attrsStr.length()) attrPos++;
                                        attrValues[key] = val;
                                    } else {
                                        size_t valStart = attrPos;
                                        while (attrPos < attrsStr.length() && !std::isspace(attrPos)) attrPos++;
                                        attrValues[key] = attrsStr.substr(valStart, attrPos - valStart);
                                    }
                                }
                            }

                            std::string replacement = compData.htmlContent;
                            for (const auto& prop : compData.props) {
                                std::string val = "";
                                if (attrValues.find(prop) != attrValues.end()) val = attrValues[prop];
                                std::string placeholder = "{{" + prop + "}}";
                                size_t subPos = 0;
                                while ((subPos = replacement.find(placeholder, subPos)) != std::string::npos) {
                                    replacement.replace(subPos, placeholder.length(), val);
                                    subPos += val.length();
                                }
                            }

                            replacement = resolveConditions(replacement, attrValues);

                            size_t totalReplaceLen = fullTag.length();
                            if (!isSelfClosing) {
                                std::string closingTag = "</" + compName + ">";
                                size_t closeTagPos = htmlContent.find(closingTag, endTagPos + 1);
                                if (closeTagPos != std::string::npos) {
                                    totalReplaceLen = (closeTagPos + closingTag.length()) - tagPos;
                                }
                            }

                            htmlContent.replace(tagPos, totalReplaceLen, replacement);
                            changed = true;
                            tagPos += replacement.length();
                        }
                    }

                    if (changed) {
                        viewBody.replace(htmlStart, htmlEnd - htmlStart, htmlContent);
                    }
                    htmlPos = htmlEnd + 1;
                } else {
                    htmlPos = htmlStart;
                }
            } else {
                htmlPos++;
            }
        }

        result.replace(bracePos + 1, blockEnd - bracePos - 1, viewBody);
        vPos = bracePos + 1 + viewBody.length() + 1;
    }

    return result;
}

std::string preprocessIncludes(const std::string& source, const fs::path& baseDir, int depth = 0) {
    if (depth > 10) {
        throw std::runtime_error("Exceeded maximum include depth (10). Check for circular dependencies.");
    }
    
    std::string result = source;
    size_t pos = 0;
    while ((pos = result.find("include(", pos)) != std::string::npos) {
        size_t startPos = pos;
        pos += 8; // skip "include("
        
        int parenCount = 1;
        size_t endPos = std::string::npos;
        bool inDoubleQuotes = false;
        bool inSingleQuotes = false;
        
        for (size_t i = pos; i < result.length(); ++i) {
            char c = result[i];
            if (c == '"' && (i == 0 || result[i-1] != '\\')) {
                inDoubleQuotes = !inDoubleQuotes;
            } else if (c == '\'' && (i == 0 || result[i-1] != '\\')) {
                inSingleQuotes = !inSingleQuotes;
            } else if (!inDoubleQuotes && !inSingleQuotes) {
                if (c == '(') parenCount++;
                else if (c == ')') {
                    parenCount--;
                    if (parenCount == 0) {
                        endPos = i;
                        break;
                    }
                }
            }
        }
        
        if (endPos == std::string::npos) {
            pos++;
            continue;
        }
        
        std::string argsStr = result.substr(pos, endPos - pos);
        
        std::vector<std::string> args;
        std::string currentArg = "";
        inDoubleQuotes = false;
        inSingleQuotes = false;
        
        for (size_t i = 0; i < argsStr.length(); ++i) {
            char c = argsStr[i];
            if (c == '"' && (i == 0 || argsStr[i-1] != '\\')) {
                inDoubleQuotes = !inDoubleQuotes;
            } else if (c == '\'' && (i == 0 || argsStr[i-1] != '\\')) {
                inSingleQuotes = !inSingleQuotes;
            }
            
            if (c == ',' && !inDoubleQuotes && !inSingleQuotes) {
                args.push_back(trim(currentArg));
                currentArg = "";
            } else {
                currentArg += c;
            }
        }
        args.push_back(trim(currentArg));
        
        if (args.empty() || args[0].empty()) {
            pos = endPos + 1;
            continue;
        }
        
        std::string filename = args[0];
        if (filename.front() == '"' && filename.back() == '"') {
            filename = filename.substr(1, filename.length() - 2);
        } else if (filename.front() == '\'' && filename.back() == '\'') {
            filename = filename.substr(1, filename.length() - 2);
        }
        
        std::map<std::string, std::string> params;
        for (size_t i = 1; i < args.size(); ++i) {
            std::string param = args[i];
            size_t eqPos = param.find('=');
            if (eqPos != std::string::npos) {
                std::string key = trim(param.substr(0, eqPos));
                std::string val = trim(param.substr(eqPos + 1));
                if (val.front() == '"' && val.back() == '"') {
                    val = val.substr(1, val.length() - 2);
                } else if (val.front() == '\'' && val.back() == '\'') {
                    val = val.substr(1, val.length() - 2);
                }
                params[key] = val;
            }
        }
        
        fs::path includePath = baseDir / filename;
        if (!fs::exists(includePath)) {
            includePath = fs::path(filename);
        }
        
        std::string replacement = "";
        if (fs::exists(includePath)) {
            std::ifstream file(includePath);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                replacement = buffer.str();
            } else {
                std::cerr << "Warning: Could not open include file: " << includePath << std::endl;
            }
        } else {
            std::cerr << "Warning: Include file not found: " << filename << " (resolved: " << includePath << ")" << std::endl;
        }
        
        for (const auto& [key, val] : params) {
            std::string placeholder = "{{" + key + "}}";
            size_t subPos = 0;
            while ((subPos = replacement.find(placeholder, subPos)) != std::string::npos) {
                replacement.replace(subPos, placeholder.length(), val);
                subPos += val.length();
            }
        }
        
        replacement = preprocessIncludes(replacement, includePath.parent_path(), depth + 1);
        
        size_t includeLength = (endPos + 1) - startPos;
        result.replace(startPos, includeLength, replacement);
        
        pos = startPos + replacement.length();
    }
    
    return result;
}

std::string preprocessLayouts(const std::string& source, const fs::path& baseDir) {
    std::string result = source;
    static const std::regex layoutRegex(R"(layout\(\s*\\?[\"\']([^\'\"]+)\\?[\"\']\s*\))");
    std::smatch match;
    
    while (std::regex_search(result, match, layoutRegex)) {
        size_t startPos = match.position();
        size_t contentStartPos = startPos + match.length();
        std::string layoutFile = match[1].str();
        
        size_t closingQuotePos = std::string::npos;
        bool escaped = false;
        for (size_t i = contentStartPos; i < result.length(); ++i) {
            if (result[i] == '\\' && !escaped) {
                escaped = true;
            } else {
                if (result[i] == '"' && !escaped) {
                    closingQuotePos = i;
                    break;
                }
                escaped = false;
            }
        }
        
        if (closingQuotePos == std::string::npos) {
            break;
        }
        
        std::string pageContent = result.substr(contentStartPos, closingQuotePos - contentStartPos);
        
        fs::path layoutPath = baseDir / layoutFile;
        if (!fs::exists(layoutPath)) {
            layoutPath = fs::path(layoutFile);
        }
        
        std::string layoutContent = "";
        if (fs::exists(layoutPath)) {
            std::ifstream file(layoutPath);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                layoutContent = buffer.str();
            } else {
                std::cerr << "Warning: Could not open layout file: " << layoutPath << std::endl;
            }
        } else {
            std::cerr << "Warning: Layout file not found: " << layoutFile << " (resolved: " << layoutPath << ")" << std::endl;
        }
        
        std::string placeholder = "{{CONTENT}}";
        size_t subPos = 0;
        while ((subPos = layoutContent.find(placeholder, subPos)) != std::string::npos) {
            layoutContent.replace(subPos, placeholder.length(), pageContent);
            subPos += pageContent.length();
        }
        
        size_t layoutAndContentLength = closingQuotePos - startPos;
        result.replace(startPos, layoutAndContentLength, layoutContent);
    }
    
    return result;
}

std::string readInputSource(const std::string& filepath) {
    std::string rawSource = "";
    fs::path baseDir;
    
    if (fs::is_directory(filepath)) {
        baseDir = fs::path(filepath);
        std::stringstream ss;
        std::vector<fs::path> paths;
        for (const auto& entry : fs::recursive_directory_iterator(filepath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".hx") {
                paths.push_back(entry.path());
            }
        }
        std::sort(paths.begin(), paths.end());
        for (const auto& p : paths) {
            ss << "\n// File: " << p.filename().string() << "\n";
            std::ifstream file(p);
            if (file.is_open()) {
                ss << file.rdbuf() << "\n";
            }
        }
        rawSource = ss.str();
    } else {
        baseDir = fs::path(filepath).parent_path();
        rawSource = readFile(filepath);
    }
    
    std::map<std::string, ComponentData> componentsMap;
    extractComponents(rawSource, componentsMap);
    std::string withLayouts = preprocessLayouts(rawSource, baseDir);
    std::string withIncludes = preprocessIncludes(withLayouts, baseDir);
    return resolveComponents(withIncludes, componentsMap);
}

uint64_t getInputState(const std::string& filepath) {
    if (!fs::exists(filepath)) return 0;
    if (fs::is_directory(filepath)) {
        uint64_t total = 0;
        size_t count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(filepath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".hx") {
                auto mt = fs::last_write_time(entry.path());
                total += std::chrono::duration_cast<std::chrono::milliseconds>(mt.time_since_epoch()).count();
                count++;
            }
        }
        return total + count * 1000000ULL;
    } else {
        auto mt = fs::last_write_time(filepath);
        return std::chrono::duration_cast<std::chrono::milliseconds>(mt.time_since_epoch()).count();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "help" || command == "--help" || command == "-h") {
        printUsage();
        return 0;
    }

    if (command == "new") {
        if (argc < 3) {
            std::cerr << "Error: Missing project name.\n";
            std::cout << "Usage: hf new <project_name>\n";
            return 1;
        }
        std::string projectName = argv[2];
        try {
            if (fs::exists(projectName)) {
                std::cerr << "Error: Directory '" << projectName << "' already exists.\n";
                return 1;
            }
            fs::create_directory(projectName);
            
            // Write app.hx
            std::string appHxPath = projectName + "/app.hx";
            std::ofstream appHxFile(appHxPath);
            if (appHxFile.is_open()) {
                appHxFile << "// Archivo principal de Hexagen Framework\n\n"
                          << "slice Tareas {\n"
                          << "    field desc: string\n"
                          << "    field completada: bool\n"
                          << "    \n"
                          << "    action Crear() {\n"
                          << "        print(\"Tarea creada: \" + desc)\n"
                          << "    }\n"
                          << "}\n\n"
                          << "view Home {\n"
                          << "    title \"Gestor de Tareas Vortex\"\n"
                          << "    input desc: string\n"
                          << "    button \"Agregar Tarea\" -> Tareas.Crear\n"
                          << "    table Tareas -> desc, completada\n"
                          << "}\n\n"
                          << "api Router {\n"
                          << "    route \"/tareas\" POST -> Tareas.Crear\n"
                          << "}\n";
                appHxFile.close();
            }
            
            // Write Makefile
            std::string makefilePath = projectName + "/Makefile";
            std::ofstream makefile(makefilePath);
            if (makefile.is_open()) {
                makefile << ".PHONY: build run dev\n\n"
                         << "build:\n"
                         << "\thf compile app.hx -o " << projectName << "_server\n\n"
                         << "run:\n"
                         << "\thf run app.hx\n\n"
                         << "dev:\n"
                         << "\thf dev app.hx\n";
                makefile.close();
            }
            
            std::cout << "✨ Project '" << projectName << "' initialized successfully!\n";
            std::cout << "📁 Directory: " << projectName << "\n";
            std::cout << "👉 Run 'cd " << projectName << "' and 'hf dev app.hx' to start developing!\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Error creating project: " << e.what() << "\n";
            return 1;
        }
    }

    if (argc < 3) {
        std::cerr << "Error: Missing input file.\n";
        printUsage();
        return 1;
    }

    std::string inputFile = argv[2];

    try {
        if (command == "dev") {
            std::cout << "⚡ [Hexagen Dev] Starting live development server on: " << inputFile << "\n";
            
            std::string outputExe = "./temp_dev_server";
            std::string tempCppFile = "temp_codegen.cpp";
            int serverPid = 0;
            std::string currentDbType = "jsonl";
            std::string currentTarget = "web";
            bool currentUseHttp = false;

            auto startServer = [&]() {
                std::string dbFlags = "";
                if (currentDbType == "sqlite") dbFlags = " -lsqlite3";
                else if (currentDbType == "postgres" || currentDbType == "postgresql") dbFlags = " -lpq";
                else if (currentDbType == "mysql") dbFlags = " -lmysqlclient";

                std::string desktopFlags = "";
                if (currentTarget == "desktop") {
                    desktopFlags = " -DWEBVIEW_GTK `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0 2>/dev/null || pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1`";
                }

                std::string moduleFlags = "";
                if (fs::exists(".hexagen_modules")) {
                    for (const auto& entry : fs::recursive_directory_iterator(".hexagen_modules")) {
                        if (entry.is_directory()) {
                            moduleFlags += " -I" + entry.path().string();
                        } else if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
                            moduleFlags += " " + entry.path().string();
                        }
                    }
                }

                std::string httpFlags = currentUseHttp ? " -lssl -lcrypto" : "";

                std::string compileCmd = "g++ -std=c++20 " + tempCppFile + " -o " + outputExe + " -pthread" + dbFlags + desktopFlags + moduleFlags + httpFlags;
                std::cout << "[Hexagen Dev] Compiling..." << std::endl;
                int res = std::system(compileCmd.c_str());
                if (res != 0) {
                    std::cerr << "❌ Compile failed. Fix errors in '" << inputFile << "' or missing dependencies to trigger reload." << std::endl;
                    if (currentDbType == "postgres" || currentDbType == "postgresql") {
                        std::cerr << "\n💡 Tip: It looks like you are using PostgreSQL. Please ensure PostgreSQL development headers are installed:\n"
                                  << "   - Ubuntu/Debian: sudo apt-get install libpq-dev\n"
                                  << "   - Fedora/RHEL:   sudo dnf install postgresql-devel\n"
                                  << "   - macOS:         brew install postgresql\n" << std::endl;
                    } else if (currentDbType == "mysql") {
                        std::cerr << "\n💡 Tip: It looks like you are using MySQL. Please ensure MySQL development headers are installed:\n"
                                  << "   - Ubuntu/Debian: sudo apt-get install default-libmysqlclient-dev\n"
                                  << "   - Fedora/RHEL:   sudo dnf install mysql-devel\n"
                                  << "   - macOS:         brew install mysql-client\n" << std::endl;
                    } else if (currentDbType == "sqlite") {
                        std::cerr << "\n💡 Tip: It looks like you are using SQLite. Please ensure SQLite3 development headers are installed:\n"
                                  << "   - Ubuntu/Debian: sudo apt-get install libsqlite3-dev\n"
                                  << "   - Fedora/RHEL:   sudo dnf install sqlite-devel\n"
                                  << "   - macOS:         brew install sqlite\n" << std::endl;
                    }
                    return false;
                }
                
                std::cout << "[Hexagen Dev] Spawning server in background..." << std::endl;
                std::string runCmd = outputExe + " & echo $! > temp_pid.txt";
                std::system(runCmd.c_str());
                
                std::ifstream pidFile("temp_pid.txt");
                if (pidFile.is_open()) {
                    pidFile >> serverPid;
                    pidFile.close();
                }
                fs::remove("temp_pid.txt");
                
                std::cout << "🚀 [Hexagen Dev] Server running at http://localhost:8080 (PID: " << serverPid << ")\n" << std::endl;
                return true;
            };
            
            auto stopServer = [&]() {
                if (serverPid > 0) {
                    std::cout << "[Hexagen Dev] Stopping running server (PID: " << serverPid << ")..." << std::endl;
                    std::string killCmd = "kill " + std::to_string(serverPid) + " 2>/dev/null";
                    std::system(killCmd.c_str());
                    serverPid = 0;
                }
            };

            auto compileAndReload = [&]() {
                try {
                    std::string src = readInputSource(inputFile);
                    Lexer lexer(src);
                    auto tokens = lexer.tokenize();

                    // Run token-based security checks before parsing
                    SecurityAnalyzer::checkObfuscation(tokens);
                    SecurityAnalyzer::checkSandboxing(tokens);

                    Parser parser(tokens);
                    auto program = parser.parse();

                    // Run AST-based security checks
                    SecurityAnalyzer analyzer(program, tokens);
                    analyzer.analyzeAST();

                    CodeGenerator codegen(program);
                    std::string cppCode = codegen.generateSourceCode(true);
                    currentDbType = program->dbType;
                    currentTarget = program->target;
                    currentUseHttp = program->useHttp;


                    std::ofstream tempFile(tempCppFile);
                    if (!tempFile.is_open()) {
                        std::cerr << "Error: Could not write temporary code file." << std::endl;
                        return;
                    }
                    tempFile << cppCode;
                    tempFile.close();

                    stopServer();
                    startServer();
                    fs::remove(tempCppFile);
                } catch (const std::exception& e) {
                    std::cerr << "❌ Parser Error: " << e.what() << std::endl;
                }
            };

            // Run first iteration
            compileAndReload();

            // Monitor changes
            auto lastState = getInputState(inputFile);
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                try {
                    if (fs::exists(inputFile)) {
                        auto currentState = getInputState(inputFile);
                        if (currentState != lastState) {
                            lastState = currentState;
                            std::cout << "⚡ [Hexagen Dev] Change detected in " << inputFile << "! Reloading..." << std::endl;
                            compileAndReload();
                        }
                    }
                } catch (...) {}
            }
            return 0;
        }

        // Standard commands
        std::string source = readInputSource(inputFile);
        
        Lexer lexer(source);
        auto tokens = lexer.tokenize();

        // Run token-based security checks before parsing
        SecurityAnalyzer::checkObfuscation(tokens);
        SecurityAnalyzer::checkSandboxing(tokens);

        Parser parser(tokens);
        auto program = parser.parse();

        if (command == "validate") {
            SecurityAnalyzer analyzer(program, tokens);
            analyzer.analyzeAST();
            return 0;
        }

        if (command == "ast") {
            program->print();
            return 0;
        }

        // Run AST-based security checks
        SecurityAnalyzer analyzer(program, tokens);
        analyzer.analyzeAST();

        if (command == "schema") {
            std::cout << "{\n";
            for (size_t i = 0; i < program->slices.size(); ++i) {
                const auto& slice = program->slices[i];
                std::cout << "  \"" << slice->name << "\": {\n";
                for (size_t j = 0; j < slice->fields.size(); ++j) {
                    const auto& field = slice->fields[j];
                    std::string typeStr;
                    if (field->type == DataType::INT) typeStr = "int";
                    else if (field->type == DataType::FLOAT) typeStr = "float";
                    else if (field->type == DataType::STRING) typeStr = "string";
                    else if (field->type == DataType::BOOL) typeStr = "bool";
                    else if (field->type == DataType::RELATION) typeStr = "relation(" + field->relatedSlice + ")";
                    else typeStr = "unknown";
                    
                    std::cout << "    \"" << field->name << "\": \"" << typeStr << "\"";
                    if (j + 1 < slice->fields.size()) std::cout << ",";
                    std::cout << "\n";
                }
                std::cout << "  }";
                if (i + 1 < program->slices.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "}\n";
            return 0;
        }

        CodeGenerator codegen(program);
        std::string cppCode = codegen.generateSourceCode(true);

        if (command == "transpile") {
            std::cout << cppCode;
            return 0;
        }

        if (command == "compile" || command == "run") {
            std::string outputExe = "a.out";
            if (command == "compile") {
                for (int i = 3; i < argc; ++i) {
                    if (std::string(argv[i]) == "-o" && i + 1 < argc) {
                        outputExe = argv[i + 1];
                        break;
                    }
                }
            } else {
                outputExe = "./temp_hexagen_run";
            }

            std::string tempCppFile = "temp_codegen.cpp";
            std::ofstream tempFile(tempCppFile);
            if (!tempFile.is_open()) {
                std::cerr << "Error: Could not write temporary code file.\n";
                return 1;
            }
            tempFile << cppCode;
            tempFile.close();

            std::string dbFlags = "";
            if (program->dbType == "sqlite") dbFlags = " -lsqlite3";
            else if (program->dbType == "postgres" || program->dbType == "postgresql") dbFlags = " -lpq";
            else if (program->dbType == "mysql") dbFlags = " -lmysqlclient";

            std::string desktopFlags = "";
            if (program->target == "desktop") {
                desktopFlags = " -DWEBVIEW_GTK `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0 2>/dev/null || pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1`";
            }

            std::string moduleFlags = "";
            if (fs::exists(".hexagen_modules")) {
                for (const auto& entry : fs::recursive_directory_iterator(".hexagen_modules")) {
                    if (entry.is_directory()) {
                        moduleFlags += " -I" + entry.path().string();
                    } else if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
                        moduleFlags += " " + entry.path().string();
                    }
                }
            }

            std::string httpFlags = program->useHttp ? " -lssl -lcrypto" : "";

            std::string compileCmd = "g++ -std=c++20 " + tempCppFile + " -o " + outputExe + " -pthread" + dbFlags + desktopFlags + moduleFlags + httpFlags;
            std::cout << "[Hexagen] Compiling generated C++ code: " << compileCmd << "\n";
            int compileResult = std::system(compileCmd.c_str());

            fs::remove(tempCppFile);

            if (compileResult != 0) {
                std::cerr << "Error: C++ compilation failed.\n";
                if (program->dbType == "postgres" || program->dbType == "postgresql") {
                    std::cerr << "\n💡 Tip: It looks like you are using PostgreSQL. Please ensure PostgreSQL development headers are installed:\n"
                              << "   - Ubuntu/Debian: sudo apt-get install libpq-dev\n"
                              << "   - Fedora/RHEL:   sudo dnf install postgresql-devel\n"
                              << "   - macOS:         brew install postgresql\n" << std::endl;
                } else if (program->dbType == "mysql") {
                    std::cerr << "\n💡 Tip: It looks like you are using MySQL. Please ensure MySQL development headers are installed:\n"
                              << "   - Ubuntu/Debian: sudo apt-get install default-libmysqlclient-dev\n"
                              << "   - Fedora/RHEL:   sudo dnf install mysql-devel\n"
                              << "   - macOS:         brew install mysql-client\n" << std::endl;
                } else if (program->dbType == "sqlite") {
                    std::cerr << "\n💡 Tip: It looks like you are using SQLite. Please ensure SQLite3 development headers are installed:\n"
                              << "   - Ubuntu/Debian: sudo apt-get install libsqlite3-dev\n"
                              << "   - Fedora/RHEL:   sudo dnf install sqlite-devel\n"
                              << "   - macOS:         brew install sqlite\n" << std::endl;
                }
                return compileResult;
            }

            std::cout << "[Hexagen] Compilation successful! Output binary: " << outputExe << "\n";

            if (command == "run") {
                std::cout << "[Hexagen] Running executable...\n\n";
                int runResult = std::system(outputExe.c_str());
                fs::remove(outputExe);
                return runResult;
            }

            return 0;
        }

        std::cerr << "Unknown command: " << command << "\n";
        printUsage();
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Compilation Error: " << e.what() << "\n";
        return 1;
    }
}
