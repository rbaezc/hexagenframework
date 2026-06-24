// Persistence behind a Storage strategy. Generated slice code delegates here
// instead of inlining a backend-specific body per method. JSONL is implemented;
// SQL backends are migrated in follow-ups. Depends on getQueryParam/getJSONVal/
// safeStoi (defined earlier in the generated translation unit).
struct ColumnSpec {
    const char* name;
    char type; // 's' = string (JSON-quoted); anything else written raw (int/float/bool)
};

struct Storage {
    virtual ~Storage() {}
    virtual void insert(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values) = 0;
    virtual std::string selectAllJson(const std::string& table, const std::vector<ColumnSpec>& cols, const std::string& req) = 0;
    virtual void deleteWhere(const std::string& table, const std::string& key, const std::string& value) = 0;
    virtual void updateWhere(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values, const std::string& key, const std::string& keyValue) = 0;
};

struct JsonlStorage : Storage {
    static std::string filePath(const std::string& table) { return "db_" + table + ".jsonl"; }

    void insert(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values) override {
        std::ofstream outfile(filePath(table), std::ios::app);
        if (!outfile.is_open()) return;
        outfile << "{";
        for (size_t i = 0; i < cols.size(); ++i) {
            outfile << "\"" << cols[i].name << "\":";
            if (cols[i].type == 's') outfile << "\"" << values[i] << "\"";
            else outfile << values[i];
            if (i + 1 < cols.size()) outfile << ",";
        }
        outfile << "}\n";
    }

    std::string selectAllJson(const std::string& table, const std::vector<ColumnSpec>& cols, const std::string& req) override {
        std::ifstream infile(filePath(table));
        std::stringstream ss;
        ss << "[";
        int limitVal = -1, offsetVal = 0;
        std::string limitStr = getQueryParam(req, "_limit");
        if (!limitStr.empty()) limitVal = safeStoi(limitStr, -1);
        std::string offsetStr = getQueryParam(req, "_offset");
        if (!offsetStr.empty()) offsetVal = safeStoi(offsetStr, 0);
        int matchedCount = 0, skipped = 0;
        if (infile.is_open()) {
            std::string line;
            bool first = true;
            while (std::getline(infile, line)) {
                if (line.empty()) continue;
                bool matches = true;
                for (const auto& c : cols) {
                    std::string f = getQueryParam(req, c.name);
                    if (!f.empty() && getJSONVal(line, c.name) != f) { matches = false; break; }
                }
                if (!matches) continue;
                if (skipped < offsetVal) { skipped++; continue; }
                if (limitVal >= 0 && matchedCount >= limitVal) break;
                if (!first) ss << ",";
                ss << line;
                first = false;
                matchedCount++;
            }
            infile.close();
        }
        ss << "]";
        return ss.str();
    }

    void deleteWhere(const std::string& table, const std::string& key, const std::string& value) override {
        std::ifstream infile(filePath(table));
        std::vector<std::string> lines;
        if (infile.is_open()) {
            std::string line;
            while (std::getline(infile, line)) {
                if (line.empty()) continue;
                if (getJSONVal(line, key) != value) lines.push_back(line);
            }
            infile.close();
        }
        std::ofstream outfile(filePath(table), std::ios::trunc);
        if (outfile.is_open()) {
            for (const auto& l : lines) outfile << l << "\n";
        }
    }

    void updateWhere(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values, const std::string& key, const std::string& keyValue) override {
        std::ifstream infile(filePath(table));
        std::vector<std::string> lines;
        if (infile.is_open()) {
            std::string line;
            while (std::getline(infile, line)) {
                if (line.empty()) continue;
                if (getJSONVal(line, key) == keyValue) {
                    std::string updated = "{";
                    for (size_t i = 0; i < cols.size(); ++i) {
                        updated += "\"" + std::string(cols[i].name) + "\":";
                        if (cols[i].type == 's') updated += "\"" + values[i] + "\"";
                        else updated += values[i];
                        if (i + 1 < cols.size()) updated += ",";
                    }
                    updated += "}";
                    lines.push_back(updated);
                } else {
                    lines.push_back(line);
                }
            }
            infile.close();
        }
        std::ofstream outfile(filePath(table), std::ios::trunc);
        if (outfile.is_open()) {
            for (const auto& l : lines) outfile << l << "\n";
        }
    }
};

// Storage selection. Defaults to JSONL; a SQL backend fragment may swap the
// active pointer at startup (see storage_sqlite.hpp) — strategy registration.
JsonlStorage& jsonlStorage() { static JsonlStorage s; return s; }
Storage*& activeStorage() { static Storage* p = &jsonlStorage(); return p; }
Storage* getStorage() { return activeStorage(); }
