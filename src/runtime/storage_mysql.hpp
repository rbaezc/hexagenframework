// MySQL/MariaDB implementation of the Storage strategy. Emitted for mysql/mariadb builds
// (depends on libmysqlclient + getMySQLConn/releaseMySQLConn from the connection pool).
// Falls back to JSONL when no connection is available.
struct MySQLStorage : Storage {
    JsonlStorage fallback;

    void insert(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values) override {
        MYSQL* conn = getMySQLConn();
        if (!conn) { fallback.insert(table, cols, values); return; }
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (!stmt) { releaseMySQLConn(conn); return; }
        std::string query = "INSERT INTO `" + table + "` (";
        for (size_t i = 0; i < cols.size(); ++i) {
            query += "`" + std::string(cols[i].name) + "`";
            if (i + 1 < cols.size()) query += ", ";
        }
        query += ") VALUES (";
        for (size_t i = 0; i < cols.size(); ++i) {
            query += "?";
            if (i + 1 < cols.size()) query += ", ";
        }
        query += ")";
        if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) == 0) {
            std::vector<MYSQL_BIND> bind(cols.size());
            std::memset(bind.data(), 0, sizeof(MYSQL_BIND) * cols.size());
            for (size_t i = 0; i < cols.size(); ++i) {
                switch (cols[i].type) {
                    case 's': bind[i].buffer_type = MYSQL_TYPE_STRING; bind[i].buffer = (char*)values[i].c_str(); bind[i].buffer_length = values[i].length(); break;
                    case 'f': bind[i].buffer_type = MYSQL_TYPE_DOUBLE; bind[i].buffer = (void*)values[i].c_str(); break;
                    case 'b': bind[i].buffer_type = MYSQL_TYPE_TINY;   bind[i].buffer = (void*)values[i].c_str(); break;
                    default:  bind[i].buffer_type = MYSQL_TYPE_LONG;   bind[i].buffer = (void*)values[i].c_str(); break;
                }
            }
            mysql_stmt_bind_param(stmt, bind.data());
            if (mysql_stmt_execute(stmt) != 0)
                std::cerr << "[MySQL] Insert failed: " << mysql_stmt_error(stmt) << std::endl;
        }
        mysql_stmt_close(stmt);
        releaseMySQLConn(conn);
    }

    std::string selectAllJson(const std::string& table, const std::vector<ColumnSpec>& cols, const std::string& req) override {
        MYSQL* conn = getMySQLConn();
        if (!conn) return fallback.selectAllJson(table, cols, req);
        std::string query = "SELECT * FROM `" + table + "`";
        std::vector<std::pair<std::string, std::string>> filters;
        for (const auto& c : cols) {
            std::string val = getQueryParam(req, c.name);
            if (!val.empty()) filters.push_back({c.name, val});
        }
        if (!filters.empty()) {
            query += " WHERE ";
            for (size_t i = 0; i < filters.size(); ++i) {
                char escaped[512]; mysql_real_escape_string(conn, escaped, filters[i].second.c_str(), filters[i].second.length());
                query += "`" + filters[i].first + "` = '" + escaped + "'";
                if (i + 1 < filters.size()) query += " AND ";
            }
        }
        std::string limitStr = getQueryParam(req, "_limit");
        std::string offsetStr = getQueryParam(req, "_offset");
        if (!limitStr.empty()) query += " LIMIT " + limitStr;
        if (!offsetStr.empty()) query += " OFFSET " + offsetStr;
        std::stringstream ss;
        ss << "[";
        if (mysql_query(conn, query.c_str()) == 0) {
            MYSQL_RES* result = mysql_store_result(conn);
            if (result) {
                int num_fields = mysql_num_fields(result);
                MYSQL_FIELD* fields = mysql_fetch_fields(result);
                MYSQL_ROW row;
                bool first = true;
                while ((row = mysql_fetch_row(result))) {
                    if (!first) ss << ",";
                    ss << "{";
                    for (int c = 0; c < num_fields; ++c) {
                        std::string fname = fields[c].name;
                        const ColumnSpec* spec = nullptr;
                        for (const auto& cs : cols) { if (cs.name == fname) { spec = &cs; break; } }
                        if (!spec) continue;
                        if (c > 0) ss << ",";
                        ss << "\"" << fname << "\":";
                        if (spec->type == 's') ss << "\"" << (row[c] ? row[c] : "") << "\"";
                        else if (spec->type == 'b') ss << (row[c] && (row[c][0] == '1') ? "true" : "false");
                        else ss << (row[c] ? row[c] : "0");
                    }
                    ss << "}";
                    first = false;
                }
                mysql_free_result(result);
            }
        } else {
            std::cerr << "[MySQL] Select failed: " << mysql_error(conn) << std::endl;
        }
        ss << "]";
        releaseMySQLConn(conn);
        return ss.str();
    }

    void deleteWhere(const std::string& table, const std::string& key, const std::string& value) override {
        MYSQL* conn = getMySQLConn();
        if (!conn) { fallback.deleteWhere(table, key, value); return; }
        char escaped[512]; mysql_real_escape_string(conn, escaped, value.c_str(), value.length());
        std::string query = "DELETE FROM `" + table + "` WHERE `" + key + "` = '" + escaped + "';";
        if (mysql_query(conn, query.c_str()) != 0)
            std::cerr << "[MySQL] Delete failed: " << mysql_error(conn) << std::endl;
        releaseMySQLConn(conn);
    }

    void updateWhere(const std::string& table, const std::vector<ColumnSpec>& cols, const std::vector<std::string>& values, const std::string& key, const std::string& keyValue) override {
        MYSQL* conn = getMySQLConn();
        if (!conn) { fallback.updateWhere(table, cols, values, key, keyValue); return; }
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (!stmt) { releaseMySQLConn(conn); return; }
        std::string query = "UPDATE `" + table + "` SET ";
        for (size_t i = 0; i < cols.size(); ++i) {
            query += "`" + std::string(cols[i].name) + "` = ?";
            if (i + 1 < cols.size()) query += ", ";
        }
        query += " WHERE `" + key + "` = ?";
        if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) == 0) {
            std::vector<MYSQL_BIND> bind(cols.size() + 1);
            std::memset(bind.data(), 0, sizeof(MYSQL_BIND) * (cols.size() + 1));
            for (size_t i = 0; i < cols.size(); ++i) {
                switch (cols[i].type) {
                    case 's': bind[i].buffer_type = MYSQL_TYPE_STRING; bind[i].buffer = (char*)values[i].c_str(); bind[i].buffer_length = values[i].length(); break;
                    case 'f': bind[i].buffer_type = MYSQL_TYPE_DOUBLE; bind[i].buffer = (void*)values[i].c_str(); break;
                    case 'b': bind[i].buffer_type = MYSQL_TYPE_TINY;   bind[i].buffer = (void*)values[i].c_str(); break;
                    default:  bind[i].buffer_type = MYSQL_TYPE_LONG;   bind[i].buffer = (void*)values[i].c_str(); break;
                }
            }
            bind[cols.size()].buffer_type = MYSQL_TYPE_STRING;
            bind[cols.size()].buffer = (char*)keyValue.c_str();
            bind[cols.size()].buffer_length = keyValue.length();
            mysql_stmt_bind_param(stmt, bind.data());
            if (mysql_stmt_execute(stmt) != 0)
                std::cerr << "[MySQL] Update failed: " << mysql_stmt_error(stmt) << std::endl;
        }
        mysql_stmt_close(stmt);
        releaseMySQLConn(conn);
    }
};

MySQLStorage& mysqlStorage() { static MySQLStorage s; return s; }
struct MySQLStorageRegistrar { MySQLStorageRegistrar() { activeStorage() = &mysqlStorage(); } };
static MySQLStorageRegistrar _mysqlStorageRegistrar;
