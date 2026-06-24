// PostgreSQL implementation of the Storage strategy. Emitted only for postgres builds
// (depends on libpq + getPGConn/releasePGConn from the connection pool).
// Falls back to JSONL when no connection is available.
struct PostgresStorage : Storage {
    JsonlStorage fallback;

    void insert(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values) override {
        PGconn* conn = getPGConn();
        if (!conn) { fallback.insert(table, cols, values); return; }
        std::string query = "INSERT INTO \"" + table + "\" (";
        for (size_t i = 0; i < cols.size(); ++i) {
            query += "\"" + std::string(cols[i].name) + "\"";
            if (i + 1 < cols.size()) query += ", ";
        }
        query += ") VALUES (";
        for (size_t i = 0; i < cols.size(); ++i) {
            query += "$" + std::to_string(i + 1);
            if (i + 1 < cols.size()) query += ", ";
        }
        query += ");";
        std::vector<const char*> paramValues(cols.size());
        for (size_t i = 0; i < cols.size(); ++i) paramValues[i] = values[i].c_str();
        PGresult* res = PQexecParams(conn, query.c_str(), (int)cols.size(), nullptr, paramValues.data(), nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            std::cerr << "[PostgreSQL] Insert failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        releasePGConn(conn);
    }

    std::string selectAllJson(const std::string& table, const std::vector<ColumnSpec>& cols, const std::string& req) override {
        PGconn* conn = getPGConn();
        if (!conn) return fallback.selectAllJson(table, cols, req);
        std::vector<std::pair<std::string, std::string>> filters;
        for (const auto& c : cols) {
            std::string val = getQueryParam(req, c.name);
            if (!val.empty()) filters.push_back({c.name, val});
        }
        std::vector<const char*> paramValues;
        std::string query = "SELECT * FROM \"" + table + "\"";
        if (!filters.empty()) {
            query += " WHERE ";
            for (size_t i = 0; i < filters.size(); ++i) {
                query += "\"" + filters[i].first + "\" = $" + std::to_string(i + 1);
                if (i + 1 < filters.size()) query += " AND ";
                paramValues.push_back(filters[i].second.c_str());
            }
        }
        std::string limitStr = getQueryParam(req, "_limit");
        std::string offsetStr = getQueryParam(req, "_offset");
        if (!limitStr.empty()) { query += " LIMIT $" + std::to_string(paramValues.size() + 1); paramValues.push_back(limitStr.c_str()); }
        if (!offsetStr.empty()) { query += " OFFSET $" + std::to_string(paramValues.size() + 1); paramValues.push_back(offsetStr.c_str()); }
        PGresult* res = PQexecParams(conn, query.c_str(), (int)paramValues.size(), nullptr,
                                     paramValues.empty() ? nullptr : paramValues.data(), nullptr, nullptr, 0);
        std::stringstream ss;
        ss << "[";
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
            int rows = PQntuples(res);
            int ncols = PQnfields(res);
            for (int r = 0; r < rows; ++r) {
                if (r > 0) ss << ",";
                ss << "{";
                // skip column 0 (auto id), match user-defined cols by name
                bool first = true;
                for (int c = 0; c < ncols; ++c) {
                    std::string fname = PQfname(res, c);
                    // find matching ColumnSpec to know the type
                    const ColumnSpec* spec = nullptr;
                    for (const auto& cs : cols) { if (cs.name == fname) { spec = &cs; break; } }
                    if (!spec) continue; // skip internal columns like rowid
                    if (!first) ss << ",";
                    ss << "\"" << fname << "\":";
                    const char* val = PQgetvalue(res, r, c);
                    if (spec->type == 's') ss << "\"" << (val ? val : "") << "\"";
                    else if (spec->type == 'b') ss << (val && (val[0] == 't' || val[0] == '1') ? "true" : "false");
                    else ss << (val ? val : "0");
                    first = false;
                }
                ss << "}";
            }
        } else {
            std::cerr << "[PostgreSQL] Select failed: " << PQerrorMessage(conn) << std::endl;
        }
        ss << "]";
        PQclear(res);
        releasePGConn(conn);
        return ss.str();
    }

    void deleteWhere(const std::string& table, const std::string& key, const std::string& value) override {
        PGconn* conn = getPGConn();
        if (!conn) { fallback.deleteWhere(table, key, value); return; }
        std::string query = "DELETE FROM \"" + table + "\" WHERE \"" + key + "\" = $1;";
        const char* paramValues[1] = { value.c_str() };
        PGresult* res = PQexecParams(conn, query.c_str(), 1, nullptr, paramValues, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            std::cerr << "[PostgreSQL] Delete failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        releasePGConn(conn);
    }

    void updateWhere(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values, const std::string& key, const std::string& keyValue) override {
        PGconn* conn = getPGConn();
        if (!conn) { fallback.updateWhere(table, cols, values, key, keyValue); return; }
        std::string query = "UPDATE \"" + table + "\" SET ";
        for (size_t i = 0; i < cols.size(); ++i) {
            query += "\"" + std::string(cols[i].name) + "\" = $" + std::to_string(i + 1);
            if (i + 1 < cols.size()) query += ", ";
        }
        query += " WHERE \"" + key + "\" = $" + std::to_string(cols.size() + 1) + ";";
        std::vector<const char*> paramValues(cols.size() + 1);
        for (size_t i = 0; i < cols.size(); ++i) paramValues[i] = values[i].c_str();
        paramValues[cols.size()] = keyValue.c_str();
        PGresult* res = PQexecParams(conn, query.c_str(), (int)paramValues.size(), nullptr, paramValues.data(), nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            std::cerr << "[PostgreSQL] Update failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        releasePGConn(conn);
    }
};

PostgresStorage& postgresStorage() { static PostgresStorage s; return s; }
struct PostgresStorageRegistrar { PostgresStorageRegistrar() { activeStorage() = &postgresStorage(); } };
static PostgresStorageRegistrar _postgresStorageRegistrar;
