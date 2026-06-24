std::vector<std::string> splitPathSegments(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '/') { i++; continue; }
        size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool matchDynamicRoute(const std::string& req, const std::string& method,
                       const std::string& pattern,
                       std::map<std::string, std::string>& params) {
    params.clear();
    size_t firstLineEnd = req.find('\n');
    std::string reqLine = (firstLineEnd == std::string::npos) ? req : req.substr(0, firstLineEnd);
    size_t sp1 = reqLine.find(' ');
    if (sp1 == std::string::npos) return false;
    if (reqLine.substr(0, sp1) != method) return false;
    size_t sp2 = reqLine.find(' ', sp1 + 1);
    std::string target = (sp2 == std::string::npos) ? reqLine.substr(sp1 + 1) : reqLine.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t qPos = target.find('?');
    if (qPos != std::string::npos) target = target.substr(0, qPos);
    std::vector<std::string> pSeg = splitPathSegments(pattern);
    std::vector<std::string> tSeg = splitPathSegments(target);
    if (pSeg.size() != tSeg.size()) return false;
    for (size_t i = 0; i < pSeg.size(); ++i) {
        if (!pSeg[i].empty() && pSeg[i][0] == ':') {
            params[pSeg[i].substr(1)] = tSeg[i];
        } else if (pSeg[i] != tSeg[i]) {
            return false;
        }
    }
    return true;
}
