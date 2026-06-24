#pragma once
#include <string>
#include <sstream>

// Indentation-aware code emitter. Replaces hand-escaped `ss << "...\"...\\n"`
// patterns: callers emit logical lines and let quote() produce correctly-escaped
// C++ string literals, removing the class of escaping bugs at the mechanism level.
//
// Adoption is incremental — new/migrated emission code should use CodeWriter; the
// existing ss<<"..." sites migrate over time (each migration verified byte-identical
// against the conformance golden files).
class CodeWriter {
public:
    explicit CodeWriter(int indentWidth = 4) : indentWidth_(indentWidth) {}

    CodeWriter& push() { ++level_; return *this; }
    CodeWriter& pop() { if (level_ > 0) --level_; return *this; }

    // Emit one indented line (indentation + text + newline).
    CodeWriter& line(const std::string& text = "") {
        if (!text.empty()) out_ << std::string(level_ * indentWidth_, ' ') << text;
        else out_ << "";
        out_ << "\n";
        return *this;
    }

    // Emit verbatim (no indentation, no newline) — e.g. an amalgamated RT_* block.
    CodeWriter& raw(const std::string& text) { out_ << text; return *this; }

    std::string str() const { return out_.str(); }

    // Produce a C++ string literal for `s` with proper escaping, so callers never
    // hand-write \" or \\n again.
    static std::string quote(const std::string& s) {
        std::string o = "\"";
        for (char c : s) {
            switch (c) {
                case '"':  o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                default:   o += c;
            }
        }
        o += "\"";
        return o;
    }

private:
    std::ostringstream out_;
    int level_ = 0;
    int indentWidth_;
};
