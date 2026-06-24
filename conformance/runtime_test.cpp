// Unit tests for the extracted runtime fragments. These functions used to live as
// string literals inside the compiler and were only "tested" by compiling a whole
// generated app; now they are real C++ and can be tested directly.
#include <string>
#include <vector>
#include <map>
#include <iostream>

#include "../src/runtime/router.hpp"
#include "../src/code_writer.hpp"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++failures; } } while (0)

int main() {
    // splitPathSegments
    CHECK(splitPathSegments("/api/leads/42").size() == 3);
    CHECK(splitPathSegments("/").empty());

    std::map<std::string, std::string> p;

    // exact static match
    CHECK(matchDynamicRoute("GET /api/leads HTTP/1.1\r\n", "GET", "/api/leads", p));

    // dynamic param capture
    CHECK(matchDynamicRoute("GET /api/leads/42 HTTP/1.1\r\n", "GET", "/api/leads/:id", p));
    CHECK(p["id"] == "42");

    // query string ignored
    CHECK(matchDynamicRoute("GET /api/leads/7?x=1 HTTP/1.1\r\n", "GET", "/api/leads/:id", p));
    CHECK(p["id"] == "7");

    // method mismatch
    CHECK(!matchDynamicRoute("POST /api/leads/42 HTTP/1.1\r\n", "GET", "/api/leads/:id", p));

    // segment-count mismatch (no greedy substring shadowing)
    CHECK(!matchDynamicRoute("GET /api/leads/42 HTTP/1.1\r\n", "GET", "/", p));

    // CodeWriter: quoting escapes (no hand-written \" or \\n)
    CHECK(CodeWriter::quote("hi") == "\"hi\"");
    CHECK(CodeWriter::quote("a\"b") == "\"a\\\"b\"");
    CHECK(CodeWriter::quote("x\ny") == "\"x\\ny\"");
    CHECK(CodeWriter::quote("c:\\path") == "\"c:\\\\path\"");

    // CodeWriter: indentation
    {
        CodeWriter cw;
        cw.line("a").push().line("b").pop().line("c");
        CHECK(cw.str() == "a\n    b\nc\n");
    }

    if (failures == 0) {
        std::cout << "runtime_test: all checks passed\n";
        return 0;
    }
    std::cerr << "runtime_test: " << failures << " failure(s)\n";
    return 1;
}
