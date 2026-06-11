cmake_minimum_required(VERSION 3.10)
project(hexagen_framework CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hf
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/codegen.cpp
    src/security_analyzer.cpp
)
