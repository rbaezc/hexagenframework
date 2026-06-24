// SQLite implementation of the Storage strategy. Emitted only for sqlite builds
// (depends on sqlite3 + getSQLiteConn/releaseSQLiteConn from the connection pool).
// Falls back to JSONL when no connection is available, matching prior behavior.
struct SqliteStorage : Storage {
    JsonlStorage fallback;

    void insert(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values) override {
        sqlite3* db = getSQLiteConn();
        if (!db) { fallback.insert(table, cols, values); return; }
        std::string query = "INSERT INTO \"" + table + "\" (";
        for (size_t i = 0; i < cols.size(); ++i) { query += std::string("\"") + cols[i].name + "\""; if (i + 1 < cols.size()) query += ", "; }
        query += ") VALUES (";
        for (size_t i = 0; i < cols.size(); ++i) { query += "?"; if (i + 1 < cols.size()) query += ", "; }
        query += ");";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            for (size_t i = 0; i < cols.size(); ++i) {
                int idx = (int)i + 1;
                switch (cols[i].type) {
                    case 's': sqlite3_bind_text(stmt, idx, values[i].c_str(), -1, SQLITE_TRANSIENT); break;
                    case 'b': sqlite3_bind_int(stmt, idx, (values[i] == "true" || values[i] == "1") ? 1 : 0); break;
                    case 'f': sqlite3_bind_double(stmt, idx, atof(values[i].c_str())); break;
                    default:  sqlite3_bind_int(stmt, idx, atoi(values[i].c_str())); break;
                }
            }
            if (sqlite3_step(stmt) != SQLITE_DONE) std::cerr << "[SQLite] Insert failed" << std::endl;
            sqlite3_finalize(stmt);
        }
        releaseSQLiteConn(db);
    }

    std::string selectAllJson(const std::string& table, const std::vector<ColumnSpec>& cols, const std::string& req) override {
        sqlite3* db = getSQLiteConn();
        if (!db) return fallback.selectAllJson(table, cols, req);
        std::vector<std::pair<std::string, std::string>> filters;
        for (const auto& c : cols) {
            std::string val = getQueryParam(req, c.name);
            if (!val.empty()) {
                if (c.type == 'b') { if (val == "true") val = "1"; else if (val == "false") val = "0"; }
                filters.push_back({c.name, val});
            }
        }
        std::string query = "SELECT * FROM \"" + table + "\"";
        if (!filters.empty()) {
            query += " WHERE ";
            for (size_t i = 0; i < filters.size(); ++i) { query += "\"" + filters[i].first + "\" = ?"; if (i + 1 < filters.size()) query += " AND "; }
        }
        std::string limitStr = getQueryParam(req, "_limit");
        std::string offsetStr = getQueryParam(req, "_offset");
        if (!limitStr.empty()) query += " LIMIT ?";
        if (!offsetStr.empty()) query += " OFFSET ?";
        sqlite3_stmt* stmt;
        std::stringstream ss;
        ss << "[";
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            int bindIdx = 1;
            for (const auto& f : filters) sqlite3_bind_text(stmt, bindIdx++, f.second.c_str(), -1, SQLITE_TRANSIENT);
            if (!limitStr.empty()) sqlite3_bind_int(stmt, bindIdx++, safeStoi(limitStr));
            if (!offsetStr.empty()) sqlite3_bind_int(stmt, bindIdx++, safeStoi(offsetStr));
            bool first = true;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!first) ss << ",";
                ss << "{";
                for (size_t i = 0; i < cols.size(); ++i) {
                    int colIdx = (int)i + 1; // column 0 is the auto id
                    ss << "\"" << cols[i].name << "\":";
                    switch (cols[i].type) {
                        case 's': {
                            const unsigned char* t = sqlite3_column_text(stmt, colIdx);
                            ss << "\"" << (t ? reinterpret_cast<const char*>(t) : "") << "\"";
                            break;
                        }
                        case 'b': ss << (sqlite3_column_int(stmt, colIdx) ? "true" : "false"); break;
                        case 'f': ss << sqlite3_column_double(stmt, colIdx); break;
                        default:  ss << sqlite3_column_int(stmt, colIdx); break;
                    }
                    if (i + 1 < cols.size()) ss << ",";
                }
                ss << "}";
                first = false;
            }
            sqlite3_finalize(stmt);
        }
        ss << "]";
        releaseSQLiteConn(db);
        return ss.str();
    }

    void deleteWhere(const std::string& table, const std::string& key, const std::string& value) override {
        sqlite3* db = getSQLiteConn();
        if (!db) { fallback.deleteWhere(table, key, value); return; }
        std::string query = "DELETE FROM \"" + table + "\" WHERE \"" + key + "\" = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        releaseSQLiteConn(db);
    }

    void updateWhere(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values, const std::string& key, const std::string& keyValue) override {
        sqlite3* db = getSQLiteConn();
        if (!db) { fallback.updateWhere(table, cols, values, key, keyValue); return; }
        std::string query = "UPDATE \"" + table + "\" SET ";
        for (size_t i = 0; i < cols.size(); ++i) {
            query += "\"" + std::string(cols[i].name) + "\" = ?";
            if (i + 1 < cols.size()) query += ", ";
        }
        query += " WHERE \"" + key + "\" = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            for (size_t i = 0; i < cols.size(); ++i) {
                int idx = (int)i + 1;
                switch (cols[i].type) {
                    case 's': sqlite3_bind_text(stmt, idx, values[i].c_str(), -1, SQLITE_TRANSIENT); break;
                    case 'b': sqlite3_bind_int(stmt, idx, (values[i] == "true" || values[i] == "1") ? 1 : 0); break;
                    case 'f': sqlite3_bind_double(stmt, idx, atof(values[i].c_str())); break;
                    default:  sqlite3_bind_int(stmt, idx, atoi(values[i].c_str())); break;
                }
            }
            sqlite3_bind_text(stmt, (int)cols.size() + 1, keyValue.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE)
                std::cerr << "[SQLite] Update failed" << std::endl;
            sqlite3_finalize(stmt);
        }
        releaseSQLiteConn(db);
    }
};

SqliteStorage& sqliteStorage() { static SqliteStorage s; return s; }
struct SqliteStorageRegistrar { SqliteStorageRegistrar() { activeStorage() = &sqliteStorage(); } };
static SqliteStorageRegistrar _sqliteStorageRegistrar;
