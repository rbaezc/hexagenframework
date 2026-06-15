#pragma once
#include <string>
#include <vector>
#include <memory>
#include <iostream>

enum class DataType {
    INT,
    FLOAT,
    STRING,
    BOOL,
    RELATION,
    UNKNOWN
};

inline std::string dataTypeToString(DataType type) {
    switch (type) {
        case DataType::INT: return "int";
        case DataType::FLOAT: return "float";
        case DataType::STRING: return "std::string";
        case DataType::BOOL: return "bool";
        case DataType::RELATION: return "int";
        default: return "void";
    }
}

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void print(int indent = 0) const = 0;
};

class ASTExpression : public ASTNode {
public:
    virtual ~ASTExpression() = default;
};

class ASTLiteral : public ASTExpression {
public:
    DataType type;
    std::string value;

    ASTLiteral(DataType type, std::string value) : type(type), value(value) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Literal (" << dataTypeToString(type) << "): " << value << "\n";
    }
};

class ASTIdentifier : public ASTExpression {
public:
    std::string name;

    ASTIdentifier(std::string name) : name(name) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Identifier: " << name << "\n";
    }
};

class ASTBinaryExpression : public ASTExpression {
public:
    std::string op;
    std::shared_ptr<ASTExpression> left;
    std::shared_ptr<ASTExpression> right;

    ASTBinaryExpression(std::string op, std::shared_ptr<ASTExpression> left, std::shared_ptr<ASTExpression> right)
        : op(op), left(left), right(right) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "BinaryExpression (" << op << "):\n";
        if (left) left->print(indent + 2);
        if (right) right->print(indent + 2);
    }
};

class ASTStatement : public ASTNode {
public:
    virtual ~ASTStatement() = default;
};

class ASTPrintStatement : public ASTStatement {
public:
    std::shared_ptr<ASTExpression> expression;

    ASTPrintStatement(std::shared_ptr<ASTExpression> expr) : expression(expr) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "PrintStatement\n";
        if (expression) expression->print(indent + 2);
    }
};

class ASTCppStatement : public ASTStatement {
public:
    std::string code;

    ASTCppStatement(std::string code) : code(code) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "CppStatement:\n" << std::string(indent + 2, ' ') << code << "\n";
    }
};

class ASTAssignmentStatement : public ASTStatement {
public:
    std::string variableName;
    std::shared_ptr<ASTExpression> expression;

    ASTAssignmentStatement(std::string varName, std::shared_ptr<ASTExpression> expr)
        : variableName(varName), expression(expr) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "AssignmentStatement: " << variableName << " =\n";
        if (expression) expression->print(indent + 2);
    }
};

class ASTIfStatement : public ASTStatement {
public:
    std::shared_ptr<ASTExpression> condition;
    std::vector<std::shared_ptr<ASTStatement>> thenBranch;
    std::vector<std::shared_ptr<ASTStatement>> elseBranch;

    ASTIfStatement(std::shared_ptr<ASTExpression> cond) : condition(cond) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "IfStatement\n";
        std::cout << std::string(indent + 2, ' ') << "Condition:\n";
        if (condition) condition->print(indent + 4);
        std::cout << std::string(indent + 2, ' ') << "Then:\n";
        for (const auto& stmt : thenBranch) {
            stmt->print(indent + 4);
        }
        if (!elseBranch.empty()) {
            std::cout << std::string(indent + 2, ' ') << "Else:\n";
            for (const auto& stmt : elseBranch) {
                stmt->print(indent + 4);
            }
        }
    }
};

class ASTWhileStatement : public ASTStatement {
public:
    std::shared_ptr<ASTExpression> condition;
    std::vector<std::shared_ptr<ASTStatement>> body;

    ASTWhileStatement(std::shared_ptr<ASTExpression> cond) : condition(cond) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "WhileStatement\n";
        std::cout << std::string(indent + 2, ' ') << "Condition:\n";
        if (condition) condition->print(indent + 4);
        std::cout << std::string(indent + 2, ' ') << "Body:\n";
        for (const auto& stmt : body) {
            stmt->print(indent + 4);
        }
    }
};

class ASTCallStatement : public ASTStatement {
public:
    std::string actionName;
    std::vector<std::shared_ptr<ASTExpression>> arguments;

    ASTCallStatement(std::string name, std::vector<std::shared_ptr<ASTExpression>> args = {})
        : actionName(name), arguments(args) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "CallStatement: " << actionName << "(\n";
        for (const auto& arg : arguments) {
            arg->print(indent + 2);
        }
        std::cout << std::string(indent, ' ') << ")\n";
    }
};

class ASTEnqueueStatement : public ASTStatement {
public:
    std::string jobName;
    std::vector<std::pair<std::string, std::shared_ptr<ASTExpression>>> arguments;

    ASTEnqueueStatement(std::string name) : jobName(name) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "EnqueueStatement: " << jobName << "(\n";
        for (const auto& arg : arguments) {
            std::cout << std::string(indent + 2, ' ') << arg.first << ":\n";
            arg.second->print(indent + 4);
        }
        std::cout << std::string(indent, ' ') << ")\n";
    }
};

class ASTField : public ASTNode {
public:
    std::string name;
    DataType type;
    std::string relatedSlice;

    ASTField(std::string name, DataType type, std::string relatedSlice = "")
        : name(name), type(type), relatedSlice(relatedSlice) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Field: " << name << " (" << dataTypeToString(type)
                  << (type == DataType::RELATION ? ", related to " + relatedSlice : "") << ")\n";
    }
};

class ASTAction : public ASTNode {
public:
    std::string name;
    std::vector<std::shared_ptr<ASTStatement>> statements;

    ASTAction(std::string name) : name(name) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Action: " << name << "\n";
        for (const auto& stmt : statements) {
            stmt->print(indent + 2);
        }
    }
};

class ASTSlice : public ASTNode {
public:
    std::string name;
    std::vector<std::shared_ptr<ASTField>> fields;
    std::vector<std::shared_ptr<ASTAction>> actions;

    ASTSlice(std::string name) : name(name) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Slice: " << name << "\n";
        std::cout << std::string(indent + 2, ' ') << "Fields:\n";
        for (const auto& field : fields) {
            field->print(indent + 4);
        }
        std::cout << std::string(indent + 2, ' ') << "Actions:\n";
        for (const auto& action : actions) {
            action->print(indent + 4);
        }
    }
};

// UI View AST Classes
class ASTViewField : public ASTNode {
public:
    std::string type;         // "title", "input", "button", "table"
    std::string label;        // Text of title/button label, or datasource for table
    std::string name;         // Input variable name
    std::string targetAction; // Action callback
    std::vector<std::string> columns; // Table columns if type is "table"
    std::string className = "";       // Custom CSS class names

    ASTViewField(std::string type, std::string label, std::string name, std::string target)
        : type(type), label(label), name(name), targetAction(target), className("") {}

    ASTViewField(std::string type, std::string label, std::vector<std::string> cols)
        : type(type), label(label), name(""), targetAction(""), columns(cols), className("") {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "ViewField (" << type << "): \"" << label << "\"";
        if (!name.empty()) std::cout << " name=" << name;
        if (!targetAction.empty()) std::cout << " -> " << targetAction;
        if (!columns.empty()) {
            std::cout << " cols=[";
            for (size_t i = 0; i < columns.size(); ++i) {
                std::cout << columns[i] << (i + 1 < columns.size() ? "," : "");
            }
            std::cout << "]";
        }
        std::cout << "\n";
    }
};

class ASTView : public ASTNode {
public:
    std::string name;
    std::vector<std::shared_ptr<ASTViewField>> elements;

    ASTView(std::string name) : name(name) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "View: " << name << "\n";
        for (const auto& elem : elements) {
            elem->print(indent + 2);
        }
    }
};

// API Routing AST Classes
class ASTRoute : public ASTNode {
public:
    std::string path;
    std::string method;        // "GET", "POST", etc.
    std::string targetAction;  // e.g., "Inventario.Agregar"
    bool isSecure;

    ASTRoute(std::string path, std::string method, std::string target, bool isSecure = false)
        : path(path), method(method), targetAction(target), isSecure(isSecure) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Route: " << method << " " << path << " -> " << targetAction;
        if (isSecure) std::cout << " [SECURE]";
        std::cout << "\n";
    }
};

class ASTMiddleware : public ASTNode {
public:
    std::string name;
    std::vector<std::string> arguments;

    ASTMiddleware(std::string name, std::vector<std::string> args = {})
        : name(name), arguments(args) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Middleware: " << name;
        if (!arguments.empty()) {
            std::cout << "(";
            for (size_t i = 0; i < arguments.size(); ++i) {
                std::cout << arguments[i];
                if (i + 1 < arguments.size()) std::cout << ", ";
            }
            std::cout << ")";
        }
        std::cout << "\n";
    }
};

class ASTApi : public ASTNode {
public:
    std::string name;
    std::vector<std::shared_ptr<ASTRoute>> routes;
    std::vector<std::shared_ptr<ASTMiddleware>> middlewares;

    ASTApi(std::string name) : name(name) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "API: " << name << "\n";
        for (const auto& mw : middlewares) {
            mw->print(indent + 2);
        }
        for (const auto& route : routes) {
            route->print(indent + 2);
        }
    }
};

class ASTJob : public ASTNode {
public:
    std::string name;
    std::vector<std::shared_ptr<ASTField>> fields;
    std::vector<std::shared_ptr<ASTAction>> actions;

    ASTJob(std::string name) : name(name) {}

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Job: " << name << "\n";
        std::cout << std::string(indent + 2, ' ') << "Fields:\n";
        for (const auto& field : fields) {
            field->print(indent + 4);
        }
        std::cout << std::string(indent + 2, ' ') << "Actions:\n";
        for (const auto& action : actions) {
            action->print(indent + 4);
        }
    }
};

// Main Unified Program AST
class ASTProgram : public ASTNode {
public:
    std::string dbType = "jsonl"; // Default database type
    std::string frontend = "vanilla"; // Default frontend type
    std::string css = "vanilla";       // Default CSS type
    std::string target = "web";        // Default target (web or desktop)
    std::vector<std::shared_ptr<ASTSlice>> slices;
    std::vector<std::shared_ptr<ASTView>> views;
    std::vector<std::shared_ptr<ASTApi>> apis;
    std::vector<std::shared_ptr<ASTJob>> jobs;

    void print(int indent = 0) const override {
        std::cout << "Program AST (DB Engine: " << dbType << "):\n";
        for (const auto& slice : slices) slice->print(indent + 2);
        for (const auto& job : jobs) job->print(indent + 2);
        for (const auto& view : views) view->print(indent + 2);
        for (const auto& api : apis) api->print(indent + 2);
    }
};
