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

std::string readInputSource(const std::string& filepath) {
    if (fs::is_directory(filepath)) {
        std::stringstream ss;
        std::vector<fs::path> paths;
        for (const auto& entry : fs::directory_iterator(filepath)) {
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
        return ss.str();
    }
    return readFile(filepath);
}

uint64_t getInputState(const std::string& filepath) {
    if (!fs::exists(filepath)) return 0;
    if (fs::is_directory(filepath)) {
        uint64_t total = 0;
        size_t count = 0;
        for (const auto& entry : fs::directory_iterator(filepath)) {
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

            auto startServer = [&]() {
                std::string dbFlags = "";
                if (currentDbType == "sqlite") dbFlags = " -lsqlite3";
                else if (currentDbType == "postgres" || currentDbType == "postgresql") dbFlags = " -lpq";
                else if (currentDbType == "mysql") dbFlags = " -lmysqlclient";

                std::string compileCmd = "g++ -std=c++17 " + tempCppFile + " -o " + outputExe + " -pthread" + dbFlags;
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

            std::string compileCmd = "g++ -std=c++17 " + tempCppFile + " -o " + outputExe + " -pthread" + dbFlags;
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
