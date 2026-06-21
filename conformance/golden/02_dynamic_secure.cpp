// Generated automatically by Hexagen Framework
// Database Engine: jsonl
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <set>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>
#include <functional>
#include <queue>
#include <unordered_map>
#include <map>
#include <random>
#include <cstdlib>
#include <ctime>
#include <atomic>
#include <arpa/inet.h>
#include <condition_variable>
#include <coroutine>

// C++20 Coroutine Async server components
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

struct AsyncRead {
    int fd;
    std::string& out_req;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        std::thread([this, h]() {
            char buffer[4096];
            std::string req;
            while (true) {
                int valread = recv(fd, buffer, sizeof(buffer) - 1, 0);
                if (valread <= 0) break;
                buffer[valread] = '\0';
                req += buffer;
                if (req.find("\r\n\r\n") != std::string::npos) break;
                if (req.find("Content-Length:") != std::string::npos) {
                    size_t pos = req.find("Content-Length:");
                    size_t end = req.find("\r\n", pos);
                    if (end != std::string::npos) {
                        std::string lenStr = req.substr(pos + 15, end - (pos + 15));
                        size_t len = std::stoul(lenStr);
                        size_t bodyPos = req.find("\r\n\r\n");
                        if (bodyPos != std::string::npos && req.length() >= bodyPos + 4 + len) {
                            break;
                        }
                    }
                }
            }
            out_req = req;
            h.resume();
        }).detach();
    }
    void await_resume() noexcept {}
};


// Asynchronous Job Queue + durability + retry + supervision (OTP/Oban-style)
std::string base64_encode(const std::string& in);
std::string base64_decode(const std::string& in);
std::string getJSONVal(const std::string& json, const std::string& field);
void dispatch_job(const std::string& name, const std::string& argsJson);

struct PendingJob {
    std::string id;
    std::string name;
    std::string args;
    int attempts = 0;
    std::function<void()> run;
};
std::queue<PendingJob> global_job_queue;
std::mutex global_job_queue_mutex;
std::condition_variable global_job_queue_cv;
std::mutex jobs_file_mutex;
static const char* JOBS_FILE = "jobs_pending.jsonl";

std::string makeJobId() {
    static long counter = 0;
    std::lock_guard<std::mutex> lk(jobs_file_mutex);
    long n = ++counter;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(now) + "-" + std::to_string(n);
}

void persistJob(const std::string& id, const std::string& name, const std::string& args, int attempts) {
    std::lock_guard<std::mutex> lk(jobs_file_mutex);
    std::ofstream f(JOBS_FILE, std::ios::app);
    if (f.is_open()) f << "{\"id\":\"" << id << "\",\"name\":\"" << name
                        << "\",\"args\":\"" << base64_encode(args) << "\",\"attempts\":" << attempts << "}\n";
}

void removeJob(const std::string& id) {
    std::lock_guard<std::mutex> lk(jobs_file_mutex);
    std::ifstream in(JOBS_FILE);
    std::vector<std::string> keep; std::string line;
    while (std::getline(in, line)) { if (line.empty()) continue; if (getJSONVal(line, "id") != id) keep.push_back(line); }
    in.close();
    std::ofstream out(JOBS_FILE, std::ios::trunc);
    for (auto& l : keep) out << l << "\n";
}

void enqueue_persisted_job(const std::string& name, const std::string& args, std::function<void()> run) {
    std::string id = makeJobId();
    persistJob(id, name, args, 0);
    { std::lock_guard<std::mutex> lock(global_job_queue_mutex); global_job_queue.push({id, name, args, 0, run}); }
    global_job_queue_cv.notify_one();
}

void enqueue_background_task(std::function<void()> task) {
    { std::lock_guard<std::mutex> lock(global_job_queue_mutex); global_job_queue.push({"", "", "", 0, task}); }
    global_job_queue_cv.notify_one();
}

void run_job_worker() {
    const int maxAttempts = 3;
    while (true) {
        PendingJob job;
        {
            std::unique_lock<std::mutex> lock(global_job_queue_mutex);
            global_job_queue_cv.wait(lock, [] { return !global_job_queue.empty(); });
            job = global_job_queue.front();
            global_job_queue.pop();
        }
        if (!job.run) continue;
        bool ok = false;
        for (int attempt = job.attempts + 1; attempt <= maxAttempts && !ok; ++attempt) {
            try { job.run(); ok = true; }
            catch (const std::exception& e) {
                std::cerr << "[Job] '" << job.name << "' attempt " << attempt << "/" << maxAttempts << " failed: " << e.what() << std::endl;
                if (attempt < maxAttempts) std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));
            }
            catch (...) {
                std::cerr << "[Job] '" << job.name << "' attempt " << attempt << " failed (unknown)" << std::endl;
                if (attempt < maxAttempts) std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));
            }
        }
        if (!ok) std::cerr << "[Job] '" << job.name << "' permanently failed after " << maxAttempts << " attempts" << std::endl;
        if (!job.id.empty()) removeJob(job.id);
    }
}

// Supervisor: keep N workers alive; restart any that exit unexpectedly.
void start_job_supervisor(int n) {
    for (int i = 0; i < n; ++i) {
        std::thread([i]() {
            while (true) {
                std::thread w(run_job_worker);
                w.join();
                std::cerr << "[Supervisor] job worker " << i << " exited; restarting" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }).detach();
    }
}

// Crash recovery: replay jobs persisted by a previous run.
void recover_persisted_jobs() {
    std::vector<PendingJob> recovered;
    {
        std::lock_guard<std::mutex> lk(jobs_file_mutex);
        std::ifstream in(JOBS_FILE);
        if (!in.is_open()) return;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            PendingJob j;
            j.id = getJSONVal(line, "id");
            j.name = getJSONVal(line, "name");
            j.args = base64_decode(getJSONVal(line, "args"));
            j.attempts = std::atoi(getJSONVal(line, "attempts").c_str());
            recovered.push_back(j);
        }
    }
    for (auto& j : recovered) {
        std::string nm = j.name, ar = j.args;
        j.run = [nm, ar]() { dispatch_job(nm, ar); };
        { std::lock_guard<std::mutex> lock(global_job_queue_mutex); global_job_queue.push(j); }
        global_job_queue_cv.notify_one();
    }
    if (!recovered.empty()) std::cerr << "[Supervisor] recovered " << recovered.size() << " persisted job(s)" << std::endl;
}

template <typename State>
class GenServer {
public:
    using Handler = std::function<void(State&, const std::string&)>;
    GenServer(State initial, Handler handler)
        : state_(initial), handler_(handler), running_(true) {
        worker_ = std::thread([this]() { loop(); });
    }
    ~GenServer() { stop(); }
    void cast(const std::string& msg) {
        { std::lock_guard<std::mutex> lk(mtx_); mailbox_.push(msg); }
        cv_.notify_one();
    }
    State get() { std::lock_guard<std::mutex> lk(stateMtx_); return state_; }
    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
private:
    void loop() {
        while (running_) {
            std::string msg;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this]() { return !mailbox_.empty() || !running_; });
                if (!running_ && mailbox_.empty()) break;
                msg = mailbox_.front(); mailbox_.pop();
            }
            try {
                std::lock_guard<std::mutex> lk(stateMtx_);
                handler_(state_, msg);
            } catch (const std::exception& e) {
                std::cerr << "[GenServer] handler error: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[GenServer] handler error (unknown)" << std::endl;
            }
        }
    }
    State state_;
    Handler handler_;
    std::queue<std::string> mailbox_;
    std::mutex mtx_, stateMtx_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
    std::thread worker_;
};

// IP-based Rate Limiting Middleware State & Checker
struct RateLimitEntry {
    int request_count;
    std::chrono::steady_clock::time_point window_start;
};

std::unordered_map<std::string, RateLimitEntry> rate_limit_map;
std::mutex rate_limit_mutex;

bool check_rate_limit(const std::string& ip, int limit, int window_seconds) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex);
    auto now = std::chrono::steady_clock::now();
    auto it = rate_limit_map.find(ip);
    if (it == rate_limit_map.end()) {
        rate_limit_map[ip] = {1, now};
        return true;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.window_start).count();
    if (elapsed >= window_seconds) {
        it->second.request_count = 1;
        it->second.window_start = now;
        return true;
    }
    if (it->second.request_count >= limit) {
        return false;
    }
    it->second.request_count++;
    return true;
}

// Simple JSON parser helpers
std::string getJSONVal(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";
    size_t colon = json.find(":", pos);
    if (colon == std::string::npos) return "";
    size_t valStart = colon + 1;
    while (valStart < json.length() && (json[valStart] == ' ' || json[valStart] == '"')) valStart++;
    size_t valEnd = valStart;
    while (valEnd < json.length() && json[valEnd] != ',' && json[valEnd] != '}' && json[valEnd] != '"' && json[valEnd] != '\n') valEnd++;
    if (valStart >= valEnd) return "";
    return json.substr(valStart, valEnd - valStart);
}

// Simple query parameter extraction helper
std::string getQueryParam(const std::string& req, const std::string& key) {
    size_t firstLineEnd = req.find("\n");
    if (firstLineEnd == std::string::npos) return "";
    std::string reqLine = req.substr(0, firstLineEnd);
    size_t qPos = reqLine.find('?');
    if (qPos == std::string::npos) return "";
    size_t spacePos = reqLine.find(' ', qPos);
    if (spacePos == std::string::npos) spacePos = reqLine.length();
    std::string queryString = reqLine.substr(qPos + 1, spacePos - qPos - 1);
    std::string target = key + "=";
    size_t start = 0;
    while (true) {
        size_t p = queryString.find(target, start);
        if (p == std::string::npos) return "";
        if (p == 0 || queryString[p - 1] == '&') {
            size_t valStart = p + target.length();
            size_t valEnd = queryString.find('&', valStart);
            if (valEnd == std::string::npos) {
                return queryString.substr(valStart);
            } else {
                return queryString.substr(valStart, valEnd - valStart);
            }
        }
        start = p + 1;
    }
    return "";
}

std::vector<std::string> __splitJsonObjects(const std::string& arr) {
    std::vector<std::string> out;
    int depth = 0; bool inStr = false; bool esc = false; size_t start = std::string::npos;
    for (size_t i = 0; i < arr.size(); ++i) {
        char c = arr[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') { if (depth == 0) start = i; depth++; }
        else if (c == '}') { depth--; if (depth == 0 && start != std::string::npos) { out.push_back(arr.substr(start, i - start + 1)); start = std::string::npos; } }
    }
    return out;
}

bool isValidEmail(const std::string& s) {
    size_t at = s.find('@');
    if (at == std::string::npos || at == 0) return false;
    size_t dot = s.find('.', at);
    if (dot == std::string::npos || dot == at + 1) return false;
    if (dot + 1 >= s.size()) return false;
    if (s.find(' ') != std::string::npos) return false;
    return true;
}

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

// Global environment loader and helpers
const char* getEnvOr(const char* key, const char* defaultVal) {
    const char* val = std::getenv(key);
    return val ? val : defaultVal;
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void loadEnv() {
    std::ifstream envFile(".env");
    if (!envFile.is_open()) return;
    std::string line;
    while (std::getline(envFile, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        std::string key = trim(line.substr(0, eqPos));
        std::string val = trim(line.substr(eqPos + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        } else if (val.size() >= 2 && val.front() == '\'' && val.back() == '\'') {
            val = val.substr(1, val.size() - 2);
        }
        setenv(key.c_str(), val.c_str(), 1);
    }
    envFile.close();
}

int safeStoi(const std::string& val, int defaultVal = 0) {
    if (val.empty()) return defaultVal;
    try {
        return std::stoi(val);
    } catch (...) {
        return defaultVal;
    }
}

double safeStod(const std::string& val, double defaultVal = 0.0) {
    if (val.empty()) return defaultVal;
    try {
        return std::stod(val);
    } catch (...) {
        return defaultVal;
    }
}

std::string sha256_bytes(const std::string& str) {
    unsigned int h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;
    unsigned int h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;
    unsigned int k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    std::vector<unsigned char> buf(str.begin(), str.end());
    uint64_t orig_len_bits = (uint64_t)buf.size() * 8;
    buf.push_back(0x80);
    while ((buf.size() * 8) % 512 != 448) {
        buf.push_back(0x00);
    }
    for (int i = 7; i >= 0; i--) {
        buf.push_back((unsigned char)((orig_len_bits >> (i * 8)) & 0xFF));
    }
    auto rotr = [](unsigned int x, unsigned int n) {
        return (x >> n) | (x << (32 - n));
    };
    for (size_t chunk = 0; chunk < buf.size() / 64; ++chunk) {
        unsigned int w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (buf[chunk * 64 + i * 4] << 24) |
                   (buf[chunk * 64 + i * 4 + 1] << 16) |
                   (buf[chunk * 64 + i * 4 + 2] << 8) |
                   (buf[chunk * 64 + i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            unsigned int s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            unsigned int s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned int a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;
        for (int i = 0; i < 64; ++i) {
            unsigned int S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            unsigned int ch = (e & f) ^ (~e & g);
            unsigned int temp1 = h + S1 + ch + k[i] + w[i];
            unsigned int S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            unsigned int temp2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h0 += a; h1 += b; h2 += c; h3 += d;
        h4 += e; h5 += f; h6 += g; h7 += h;
    }
    std::string out; out.resize(32);
    unsigned int hs[8] = {h0, h1, h2, h3, h4, h5, h6, h7};
    for (int i = 0; i < 8; ++i) {
        out[i*4]   = (char)((hs[i] >> 24) & 0xFF);
        out[i*4+1] = (char)((hs[i] >> 16) & 0xFF);
        out[i*4+2] = (char)((hs[i] >> 8) & 0xFF);
        out[i*4+3] = (char)(hs[i] & 0xFF);
    }
    return out;
}

std::string sha256(const std::string& str) {
    std::string d = sha256_bytes(str);
    std::stringstream ss_hex; ss_hex << std::hex << std::setfill('0');
    for (unsigned char c : d) ss_hex << std::setw(2) << (int)c;
    return ss_hex.str();
}

static const std::string b64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

std::string base64_encode(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string base64url_encode(const std::string& in) {
    std::string out = base64_encode(in);
    for (char &c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') {
        out.pop_back();
    }
    return out;
}

std::string base64_decode(const std::string& in) {
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[b64_chars[i]] = i;
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string base64url_decode(std::string in) {
    for (char &c : in) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (in.size() % 4) {
        in.push_back('=');
    }
    return base64_decode(in);
}

bool constTimeEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

std::string hmac_sha256(const std::string& key, const std::string& msg) {
    std::string k = key;
    if (k.size() > 64) k = sha256_bytes(k);
    if (k.size() < 64) k.resize(64, '\0');
    std::string o_pad(64, 0), i_pad(64, 0);
    for (int i = 0; i < 64; ++i) {
        o_pad[i] = (char)((unsigned char)k[i] ^ 0x5c);
        i_pad[i] = (char)((unsigned char)k[i] ^ 0x36);
    }
    return sha256_bytes(o_pad + sha256_bytes(i_pad + msg));
}

std::string pbkdf2_sha256(const std::string& password, const std::string& salt, int iterations, int dkLen) {
    std::string dk;
    int hLen = 32;
    int blocks = (dkLen + hLen - 1) / hLen;
    for (int b = 1; b <= blocks; ++b) {
        std::string saltBlock = salt;
        saltBlock.push_back((char)((b >> 24) & 0xFF));
        saltBlock.push_back((char)((b >> 16) & 0xFF));
        saltBlock.push_back((char)((b >> 8) & 0xFF));
        saltBlock.push_back((char)(b & 0xFF));
        std::string u = hmac_sha256(password, saltBlock);
        std::string t = u;
        for (int i = 1; i < iterations; ++i) {
            u = hmac_sha256(password, u);
            for (size_t j = 0; j < t.size(); ++j) t[j] = (char)((unsigned char)t[j] ^ (unsigned char)u[j]);
        }
        dk += t;
    }
    return dk.substr(0, dkLen);
}

std::string hashPassword(const std::string& password) {
    int iterations = 100000;
    std::string salt; salt.resize(16);
    std::random_device rd;
    for (int i = 0; i < 16; ++i) salt[i] = (char)(rd() & 0xFF);
    std::string dk = pbkdf2_sha256(password, salt, iterations, 32);
    return "pbkdf2$" + std::to_string(iterations) + "$" + base64_encode(salt) + "$" + base64_encode(dk);
}

bool verifyPassword(const std::string& password, const std::string& stored) {
    if (stored.rfind("pbkdf2$", 0) == 0) {
        size_t p1 = stored.find('$', 7);
        if (p1 == std::string::npos) return false;
        size_t p2 = stored.find('$', p1 + 1);
        if (p2 == std::string::npos) return false;
        int iterations = std::atoi(stored.substr(7, p1 - 7).c_str());
        std::string salt = base64_decode(stored.substr(p1 + 1, p2 - p1 - 1));
        std::string expected = base64_decode(stored.substr(p2 + 1));
        std::string dk = pbkdf2_sha256(password, salt, iterations, (int)expected.size());
        return constTimeEq(dk, expected);
    }
    return constTimeEq(sha256(password), stored);
}

std::string generateSessionToken(const std::string& payload) {
    std::string secret = getEnvOr("JWT_SECRET", "hexagen_secret_key_2026");
    std::string encHeader = base64url_encode("{\"alg\":\"HS256\",\"typ\":\"JWT\"}");
    std::string encPayload = base64url_encode(payload);
    std::string signingInput = encHeader + "." + encPayload;
    std::string signature = base64url_encode(hmac_sha256(secret, signingInput));
    return signingInput + "." + signature;
}

bool verifySessionToken(const std::string& token, std::string& payloadOut) {
    std::string secret = getEnvOr("JWT_SECRET", "hexagen_secret_key_2026");
    size_t dot1 = token.find('.');
    if (dot1 == std::string::npos) return false;
    size_t dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return false;
    std::string signingInput = token.substr(0, dot2);
    std::string signature = token.substr(dot2 + 1);
    std::string expected = base64url_encode(hmac_sha256(secret, signingInput));
    if (!constTimeEq(signature, expected)) return false;
    payloadOut = base64url_decode(token.substr(dot1 + 1, dot2 - dot1 - 1));
    return true;
}

std::string getHeaderValue(const std::string& req, const std::string& headerName) {
    std::string reqLower = req;
    size_t bodyPos = reqLower.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        reqLower = reqLower.substr(0, bodyPos);
    }
    for (char &c : reqLower) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    std::string target = headerName;
    for (char &c : target) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    target += ":";
    size_t pos = reqLower.find(target);
    if (pos == std::string::npos) return "";
    size_t valStart = pos + target.length();
    while (valStart < reqLower.length() && (reqLower[valStart] == ' ' || reqLower[valStart] == '\t')) valStart++;
    size_t origStart = valStart;
    size_t origEnd = req.find("\r\n", origStart);
    if (origEnd == std::string::npos) origEnd = req.length();
    std::string val = req.substr(origStart, origEnd - origStart);
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) {
        val.pop_back();
    }
    return val;
}

struct MultipartPart {
    std::string name;
    std::string filename;
    std::string contentType;
    std::string data;
};

std::vector<MultipartPart> parseMultipart(const std::string& body, const std::string& boundary) {
    std::vector<MultipartPart> parts;
    if (boundary.empty()) return parts;
    size_t pos = 0;
    while (true) {
        size_t partStart = body.find(boundary, pos);
        if (partStart == std::string::npos) break;
        partStart += boundary.length();
        if (partStart + 2 <= body.length() && body.substr(partStart, 2) == "--") {
            break;
        }
        if (partStart + 2 <= body.length() && body.substr(partStart, 2) == "\r\n") {
            partStart += 2;
        } else if (partStart + 1 <= body.length() && body[partStart] == '\n') {
            partStart += 1;
        }
        size_t partEnd = body.find(boundary, partStart);
        if (partEnd == std::string::npos) break;
        size_t contentLen = partEnd - partStart;
        if (contentLen >= 2 && body[partEnd - 2] == '\r' && body[partEnd - 1] == '\n') {
            contentLen -= 2;
        } else if (contentLen >= 1 && body[partEnd - 1] == '\n') {
            contentLen -= 1;
        }
        if (contentLen >= 1 && body[partEnd - contentLen - 1] == '\r') {
            contentLen -= 1;
        }
        std::string partContent = body.substr(partStart, contentLen);
        size_t headerEnd = partContent.find("\r\n\r\n");
        size_t headerEndLen = 4;
        if (headerEnd == std::string::npos) {
            headerEnd = partContent.find("\n\n");
            headerEndLen = 2;
        }
        if (headerEnd != std::string::npos) {
            std::string headers = partContent.substr(0, headerEnd);
            std::string data = partContent.substr(headerEnd + headerEndLen);
            MultipartPart part;
            part.data = data;
            size_t cdPos = headers.find("Content-Disposition:");
            if (cdPos != std::string::npos) {
                size_t cdEnd = headers.find("\n", cdPos);
                std::string cdLine = headers.substr(cdPos, cdEnd == std::string::npos ? std::string::npos : cdEnd - cdPos);
                size_t namePos = cdLine.find("name=\"");
                if (namePos != std::string::npos) {
                    size_t nameEnd = cdLine.find("\"", namePos + 6);
                    if (nameEnd != std::string::npos) {
                        part.name = cdLine.substr(namePos + 6, nameEnd - (namePos + 6));
                    }
                }
                size_t filePos = cdLine.find("filename=\"");
                if (filePos != std::string::npos) {
                    size_t fileEnd = cdLine.find("\"", filePos + 10);
                    if (fileEnd != std::string::npos) {
                        part.filename = cdLine.substr(filePos + 10, fileEnd - (filePos + 10));
                    }
                }
            }
            size_t ctPos = headers.find("Content-Type:");
            if (ctPos != std::string::npos) {
                size_t ctEnd = headers.find("\n", ctPos);
                std::string ctLine = headers.substr(ctPos, ctEnd == std::string::npos ? std::string::npos : ctEnd - ctPos);
                size_t ctValStart = ctLine.find(":") + 1;
                while (ctValStart < ctLine.length() && (ctLine[ctValStart] == ' ' || ctLine[ctValStart] == '\t' || ctLine[ctValStart] == '\r')) ctValStart++;
                part.contentType = ctLine.substr(ctValStart);
                while (!part.contentType.empty() && (part.contentType.back() == '\r' || part.contentType.back() == ' ' || part.contentType.back() == '\t')) {
                    part.contentType.pop_back();
                }
            }
            parts.push_back(part);
        }
        pos = partEnd;
    }
    return parts;
}

std::string getMultipartVal(const std::vector<MultipartPart>& parts, const std::string& fieldName) {
    for (const auto& part : parts) {
        if (part.name == fieldName) {
            if (!part.filename.empty()) {
                mkdir("public", 0777);
                mkdir("public/uploads", 0777);
                std::string safeName = part.filename;
                for (char &c : safeName) {
                    if (c == '/' || c == '\\' || c == '?' || c == '*' || c == ':' || c == '|') c = '_';
                }
                std::string timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                std::string savedName = timestamp + "_" + safeName;
                std::string filePath = "public/uploads/" + savedName;
                std::ofstream outfile(filePath, std::ios::binary);
                if (outfile.is_open()) {
                    outfile.write(part.data.data(), part.data.size());
                    outfile.close();
                    return "/public/uploads/" + savedName;
                }
                return "";
            } else {
                return part.data;
            }
        }
    }
    return "";
}

std::string base64_encode(const std::vector<unsigned char>& data) {
    static const char* s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int i = 0;
    int val = 0;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        i += 8;
        while (i >= 6) {
            out.push_back(s[(val >> (i - 6)) & 0x3F]);
            i -= 6;
        }
    }
    if (i > 0) {
        out.push_back(s[(val << (6 - i)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

std::vector<unsigned char> sha1(const std::string& str) {
    unsigned int h0 = 0x67452301;
    unsigned int h1 = 0xEFCDAB89;
    unsigned int h2 = 0x98BADCFE;
    unsigned int h3 = 0x10325476;
    unsigned int h4 = 0xC3D2E1F0;
    std::vector<unsigned char> buf(str.begin(), str.end());
    uint64_t orig_len_bits = (uint64_t)buf.size() * 8;
    buf.push_back(0x80);
    while ((buf.size() * 8) % 512 != 448) {
        buf.push_back(0x00);
    }
    for (int i = 7; i >= 0; i--) {
        buf.push_back((unsigned char)((orig_len_bits >> (i * 8)) & 0xFF));
    }
    auto leftrotate = [](unsigned int value, unsigned int bits) {
        return (value << bits) | (value >> (32 - bits));
    };
    for (size_t chunk = 0; chunk < buf.size() / 64; ++chunk) {
        unsigned int w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (buf[chunk * 64 + i * 4] << 24) |
                   (buf[chunk * 64 + i * 4 + 1] << 16) |
                   (buf[chunk * 64 + i * 4 + 2] << 8) |
                   (buf[chunk * 64 + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = leftrotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        unsigned int a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            unsigned int f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            unsigned int temp = leftrotate(a, 5) + f + e + k + w[i];
            e = d; d = c; c = leftrotate(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    std::vector<unsigned char> hash(20);
    hash[0] = (h0 >> 24) & 0xFF; hash[1] = (h0 >> 16) & 0xFF; hash[2] = (h0 >> 8) & 0xFF; hash[3] = h0 & 0xFF;
    hash[4] = (h1 >> 24) & 0xFF; hash[5] = (h1 >> 16) & 0xFF; hash[6] = (h1 >> 8) & 0xFF; hash[7] = h1 & 0xFF;
    hash[8] = (h2 >> 24) & 0xFF; hash[9] = (h2 >> 16) & 0xFF; hash[10] = (h2 >> 8) & 0xFF; hash[11] = h2 & 0xFF;
    hash[12] = (h3 >> 24) & 0xFF; hash[13] = (h3 >> 16) & 0xFF; hash[14] = (h3 >> 8) & 0xFF; hash[15] = h3 & 0xFF;
    hash[16] = (h4 >> 24) & 0xFF; hash[17] = (h4 >> 16) & 0xFF; hash[18] = (h4 >> 8) & 0xFF; hash[19] = h4 & 0xFF;
    return hash;
}

std::string getWebSocketAcceptKey(const std::string& key) {
    std::string concat = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    return base64_encode(sha1(concat));
}

std::set<int> active_ws_clients;
std::mutex ws_clients_mutex;

void register_ws_client(int fd) {
    std::lock_guard<std::mutex> lock(ws_clients_mutex);
    active_ws_clients.insert(fd);
}

void unregister_ws_client(int fd) {
    std::lock_guard<std::mutex> lock(ws_clients_mutex);
    active_ws_clients.erase(fd);
    close(fd);
}

void sendWebSocketFrame(int client_fd, const std::string& message) {
    std::vector<unsigned char> frame;
    frame.push_back(0x81);
    size_t len = message.length();
    if (len < 126) {
        frame.push_back((unsigned char)len);
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back((len >> (i * 8)) & 0xFF);
        }
    }
    frame.insert(frame.end(), message.begin(), message.end());
    send(client_fd, frame.data(), frame.size(), 0);
}

void broadcast_ws_message(const std::string& message, int sender_fd = -1) {
    std::lock_guard<std::mutex> lock(ws_clients_mutex);
    for (int fd : active_ws_clients) {
        if (fd != sender_fd) {
            sendWebSocketFrame(fd, message);
        }
    }
}

std::map<std::string, std::set<int>> ws_topics;
std::mutex ws_topics_mutex;

void subscribe_topic(const std::string& topic, int fd) {
    std::lock_guard<std::mutex> lk(ws_topics_mutex);
    ws_topics[topic].insert(fd);
}

void unsubscribe_all_topics(int fd) {
    std::lock_guard<std::mutex> lk(ws_topics_mutex);
    for (auto& kv : ws_topics) kv.second.erase(fd);
}

int publish_topic(const std::string& topic, const std::string& message) {
    std::vector<int> targets;
    { std::lock_guard<std::mutex> lk(ws_topics_mutex);
      auto it = ws_topics.find(topic);
      if (it != ws_topics.end()) targets.assign(it->second.begin(), it->second.end()); }
    int sent = 0;
    for (int fd : targets) { sendWebSocketFrame(fd, message); sent++; }
    return sent;
}

std::map<std::string, std::string> extractLiveRegions(const std::string& html) {
    std::map<std::string, std::string> regions;
    size_t pos = 0;
    const std::string open = "<!--hg:";
    while ((pos = html.find(open, pos)) != std::string::npos) {
        size_t nameStart = pos + open.size();
        size_t nameEnd = html.find("-->", nameStart);
        if (nameEnd == std::string::npos) break;
        std::string name = html.substr(nameStart, nameEnd - nameStart);
        size_t contentStart = nameEnd + 3;
        std::string close = "<!--/hg:" + name + "-->";
        size_t contentEnd = html.find(close, contentStart);
        if (contentEnd == std::string::npos) { pos = contentStart; continue; }
        regions[name] = html.substr(contentStart, contentEnd - contentStart);
        pos = contentEnd + close.size();
    }
    return regions;
}

std::string jsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o;
}

std::string computeDomPatches(const std::string& oldHtml, const std::string& newHtml) {
    auto oldR = extractLiveRegions(oldHtml);
    auto newR = extractLiveRegions(newHtml);
    std::stringstream js;
    js << "{\"type\":\"patch\",\"patches\":[";
    bool first = true;
    for (auto& kv : newR) {
        auto oit = oldR.find(kv.first);
        if (oit == oldR.end() || oit->second != kv.second) {
            if (!first) js << ",";
            first = false;
            js << "{\"id\":\"" << jsonEscape(kv.first) << "\",\"html\":\"" << jsonEscape(kv.second) << "\"}";
        }
    }
    js << "]}";
    return js.str();
}

int publish_dom_patch(const std::string& oldHtml, const std::string& newHtml) {
    std::string patch = computeDomPatches(oldHtml, newHtml);
    broadcast_ws_message(patch);
    return (int)patch.size();
}

bool readWebSocketFrame(int client_fd, std::string& out_message) {
    unsigned char header[2];
    int n = recv(client_fd, header, 2, 0);
    if (n <= 0) return false;
    unsigned char opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;
    if (opcode == 0x8) return false;
    if (payload_len == 126) {
        unsigned char ext_len[2];
        if (recv(client_fd, ext_len, 2, 0) != 2) return false;
        payload_len = (ext_len[0] << 8) | ext_len[1];
    } else if (payload_len == 127) {
        unsigned char ext_len[8];
        if (recv(client_fd, ext_len, 8, 0) != 8) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | ext_len[i];
        }
    }
    unsigned char masking_key[4] = {0};
    if (masked) {
        if (recv(client_fd, masking_key, 4, 0) != 4) return false;
    }
    std::vector<char> payload(payload_len);
    if (payload_len > 0) {
        size_t total_received = 0;
        while (total_received < payload_len) {
            int rec = recv(client_fd, payload.data() + total_received, payload_len - total_received, 0);
            if (rec <= 0) return false;
            total_received += rec;
        }
        if (masked) {
            for (size_t i = 0; i < payload_len; ++i) {
                payload[i] ^= masking_key[i % 4];
            }
        }
    }
    if (opcode == 0x1) {
        out_message = std::string(payload.begin(), payload.end());
    }
    return true;
}

std::string readHttpRequest(int client_fd) {
    std::string req;
    char buffer[4096];
    size_t bodyPos = std::string::npos;
    size_t contentLength = 0;
    while (true) {
        int valread = read(client_fd, buffer, sizeof(buffer));
        if (valread <= 0) break;
        req.append(buffer, valread);
        if (bodyPos == std::string::npos) {
            bodyPos = req.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                std::string reqLower = req.substr(0, bodyPos);
                for (char &c : reqLower) { if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; }
                size_t clPos = reqLower.find("content-length:");
                if (clPos != std::string::npos) {
                    size_t valStart = clPos + 15;
                    while (valStart < reqLower.length() && (reqLower[valStart] == ' ' || reqLower[valStart] == '\t')) valStart++;
                    size_t valEnd = req.find_first_of("\r\n", valStart);
                    if (valEnd != std::string::npos) {
                        try {
                            contentLength = std::stoul(req.substr(valStart, valEnd - valStart));
                        } catch (...) {}
                    }
                }
            }
        }
        if (bodyPos != std::string::npos) {
            size_t readBodyBytes = req.length() - (bodyPos + 4);
            if (readBodyBytes >= contentLength) {
                break;
            }
        }
    }
    return req;
}

void initDatabase() {
    loadEnv();
    // No init needed for JSONL
}

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
};

// Storage selection. JSONL today; SQL backends will register here as they migrate.
Storage* getStorage() {
    static JsonlStorage jsonl;
    return &jsonl;
}

class Lead {
public:
    int id;
    std::string nombre;

    std::map<std::string, std::string> validateChangeset() {
        std::map<std::string, std::string> errors;
        return errors;
    }

    void saveJSONL() {
        static const std::vector<ColumnSpec> _cols = {{"id", 'i'}, {"nombre", 's'}};
        std::vector<std::string> _vals = {std::to_string(id), nombre};
        getStorage()->insert("Lead", _cols, _vals);
    }

    static std::string getAllAsJSON_JSONL(const std::string& req = "") {
        static const std::vector<ColumnSpec> _cols = {{"id", 'i'}, {"nombre", 's'}};
        return getStorage()->selectAllJson("Lead", _cols, req);
    }

    static void deleteRecord_JSONL(const std::string& key, const std::string& value) {
        getStorage()->deleteWhere("Lead", key, value);
    }

    void save() {
        saveJSONL();
    }

    static std::string getAllAsJSON(const std::string& req = "") {
        return getAllAsJSON_JSONL(req);
    }

    static void deleteRecord(const std::string& key, const std::string& value) {
        deleteRecord_JSONL(key, value);
    }

    void Ver();
    void Borrar();
};

// Association preloading helpers
// Action Implementations
void Lead::Ver() {
        std::cout << "ver" << std::endl;
}

void Lead::Borrar() {
        std::cout << "borrar" << std::endl;
}

void dispatch_job(const std::string& name, const std::string& argsJson) {
    (void)argsJson;
}

// Raw UI View HTML Pages
const char* HTML_Home = R"HTML(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <title>Leads</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&family=JetBrains+Mono:wght@400;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0b0f19;
            --card-bg: rgba(20, 30, 55, 0.45);
            --border-color: rgba(255, 255, 255, 0.08);
            --primary-glow: #00f2fe;
            --secondary-glow: #4facfe;
            --text-color: #f3f4f6;
            --text-muted: #9ca3af;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Outfit', sans-serif; background-color: var(--bg-color); color: var(--text-color);
            min-height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; overflow-x: hidden; position: relative;
        }
        body::before {
            content: ''; position: absolute; width: 300px; height: 300px;
            background: radial-gradient(circle, var(--primary-glow) 0%, transparent 70%);
            top: 10%; left: 15%; opacity: 0.15; filter: blur(80px); z-index: 0;
        }
        body::after {
            content: ''; position: absolute; width: 350px; height: 350px;
            background: radial-gradient(circle, var(--secondary-glow) 0%, transparent 70%);
            bottom: 15%; right: 15%; opacity: 0.15; filter: blur(80px); z-index: 0;
        }
        .container { width: 100%; max-width: 550px; padding: 2rem; z-index: 1; }
        .card {
            background: var(--card-bg); backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px);
            border: 1px solid var(--border-color); border-radius: 24px; padding: 2.5rem; box-shadow: 0 20px 50px rgba(0, 0, 0, 0.3);
        }
        .heading-container { margin-bottom: 2rem; text-align: center; }
        .main-heading { font-size: 2rem; font-weight: 800; background: linear-gradient(135deg, #fff 0%, #a5b4fc 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent; margin-bottom: 0.5rem; }
        .sub-heading { font-size: 0.95rem; color: var(--text-muted); }
        .form-group { margin-bottom: 1.5rem; }
        .form-label { display: block; font-size: 0.85rem; font-weight: 600; text-transform: uppercase; color: var(--text-muted); margin-bottom: 0.5rem; }
        .form-input { width: 100%; background: rgba(255, 255, 255, 0.03); border: 1px solid var(--border-color); border-radius: 12px; padding: 0.85rem 1rem; color: white; font-family: inherit; font-size: 1rem; }
        .form-input:focus { outline: none; border-color: var(--primary-glow); background: rgba(255, 255, 255, 0.06); }
        .btn {
            width: 100%; background: linear-gradient(135deg, var(--secondary-glow) 0%, var(--primary-glow) 100%);
            border: none; color: #0b0f19; padding: 1rem; font-size: 1rem; font-weight: 700; border-radius: 12px; cursor: pointer; transition: all 0.3s ease; margin-bottom: 1rem;
        }
        .btn:hover { transform: translateY(-2px); filter: brightness(1.1); }
        .result-panel { margin-top: 2rem; background: rgba(0, 0, 0, 0.25); border-radius: 16px; border: 1px solid rgba(255, 255, 255, 0.05); padding: 1.25rem; display: none; }
        .result-title { font-size: 0.85rem; font-weight: 600; color: var(--primary-glow); margin-bottom: 0.5rem; text-transform: uppercase; }
        .result-code { font-family: 'JetBrains Mono', monospace; font-size: 0.85rem; white-space: pre-wrap; color: #e5e7eb; }
        .table-container { margin-top: 2rem; background: rgba(0, 0, 0, 0.2); border-radius: 12px; overflow: hidden; border: 1px solid var(--border-color); }
        .data-table { width: 100%; text-align: left; border-collapse: collapse; }
        .data-table th, .data-table td { padding: 0.75rem 1rem; border-bottom: 1px solid var(--border-color); }
        .data-table th { background: rgba(255, 255, 255, 0.03); font-size: 0.85rem; text-transform: uppercase; color: var(--text-muted); }
        .data-table td { font-size: 0.95rem; }
    </style>
</head>
<body>
    <main class="container">
        <section class="card">
            <div id="hexa-root">
                <div class="heading-container">
                    <h1 class="main-heading">Home</h1>
                    <p class="sub-heading">Leads</p>
                </div>
            </div>
            <div class="result-panel" id="result-panel">
                <div class="result-title" id="result-title">Respuesta de la API C++</div>
                <pre class="result-code"><code id="result-code"></code></pre>
            </div>
        </section>
    </main>
    <script>
        async function refreshTables() {
        }

        async function deleteRow(idValue, endpoint) {
            if (!confirm('¿Seguro que deseas eliminar este registro?')) return;
            const payload = {};
            try {
                const response = await fetch(endpoint, {
                    method: 'DELETE',
                    headers: {
                        'Content-Type': 'application/json',
                        'Authorization': 'Bearer ' + (localStorage.getItem('hexagen_token') || 'hexagen_token_123')
                    },
                    body: JSON.stringify(payload)
                });
                const data = await response.json();
                document.getElementById('result-code').innerText = JSON.stringify(data, null, 2);
                document.getElementById('result-panel').style.display = 'block';
                refreshTables();
            } catch (err) {
                alert('Error al eliminar el registro');
            }
        }

        async function triggerAction(endpoint) {
            const payload = {};
            document.querySelectorAll('.form-input').forEach(input => {
                payload[input.name] = input.value;
            });
            try {
                const response = await fetch(endpoint, {
                    method: 'POST',
                    headers: { 
                        'Content-Type': 'application/json',
                        'Authorization': 'Bearer ' + (localStorage.getItem('hexagen_token') || 'hexagen_token_123')
                    },
                    body: JSON.stringify(payload)
                });
                const data = await response.json();
                document.getElementById('result-code').innerText = JSON.stringify(data, null, 2);
                document.getElementById('result-panel').style.display = 'block';
                refreshTables();
            } catch (err) {
                document.getElementById('result-code').innerText = 'Error connecting to API server.';
                document.getElementById('result-panel').style.display = 'block';
            }
        }
        // LiveView Client-side Script (Phase 2)
        const liveSocket = new WebSocket('ws://' + window.location.host + '/live');
        liveSocket.onmessage = function(event) {
            try {
                const msg = JSON.parse(event.data);
                if (msg.type === 'patch' && Array.isArray(msg.patches)) {
                    // LiveView DOM patching: apply minimal server-computed diffs
                    msg.patches.forEach(p => {
                        const el = document.querySelector('[hg-id="' + p.id + '"]');
                        if (el && el.innerHTML !== p.html) el.innerHTML = p.html;
                    });
                } else if (msg.event === 'action') {
                    refreshTables();
                } else if (msg.event === 'input_change') {
                    const input = document.getElementById('input-' + msg.field);
                    if (input && input.value !== msg.value) {
                        input.value = msg.value;
                    }
                }
            } catch(e) {}
        };
        function setupLiveEvents() {
            document.querySelectorAll('.form-input').forEach(input => {
                input.addEventListener('input', () => {
                    liveSocket.send(JSON.stringify({
                        event: 'input_change',
                        field: input.name,
                        value: input.value
                    }));
                });
            });
        }
        liveSocket.onopen = setupLiveEvents;

        window.onload = () => { refreshTables(); setupLiveEvents(); };
    </script>
</body>
</html>

)HTML";

// AutoCRUD Admin Portal HTML
const char* HTML_ADMIN = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Hexagen Admin Portal</title>
<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">
<style>
body {
    margin: 0;
    font-family: 'Outfit', sans-serif;
    background: #0b0f19;
    color: #f3f4f6;
    display: flex;
    height: 100vh;
}
sidebar {
    width: 260px;
    background: #111827;
    border-right: 1px solid #1f2937;
    padding: 24px;
    display: flex;
    flex-direction: column;
}
sidebar h2 {
    font-size: 20px;
    margin: 0 0 24px 0;
    background: linear-gradient(135deg, #00f2fe 0%, #4facfe 100%);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    font-weight: 800;
}
.slice-btn {
    padding: 12px 16px;
    border-radius: 12px;
    cursor: pointer;
    margin-bottom: 8px;
    transition: all 0.2s;
    background: transparent;
    border: none;
    color: #9ca3af;
    text-align: left;
    font-size: 16px;
    width: 100%;
}
.slice-btn:hover, .slice-btn.active {
    background: rgba(0, 242, 254, 0.1);
    color: #00f2fe;
}
content {
    flex: 1;
    padding: 40px;
    overflow-y: auto;
}
.card {
    background: rgba(17, 24, 39, 0.7);
    backdrop-filter: blur(10px);
    border: 1px solid rgba(255,255,255,0.05);
    border-radius: 24px;
    padding: 32px;
    box-shadow: 0 20px 40px rgba(0,0,0,0.3);
}
table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 24px;
}
th, td {
    padding: 16px;
    text-align: left;
    border-bottom: 1px solid #1f2937;
}
th {
    color: #9ca3af;
    font-weight: 600;
}
.btn {
    background: linear-gradient(135deg, #00f2fe 0%, #4facfe 100%);
    border: none;
    color: white;
    padding: 10px 20px;
    border-radius: 12px;
    cursor: pointer;
    font-weight: 600;
    transition: transform 0.2s;
}
.btn:hover {
    transform: scale(1.03);
}
.btn-danger {
    background: linear-gradient(135deg, #f87171 0%, #ef4444 100%);
}
.modal {
    display: none;
    position: fixed;
    top: 0; left: 0; width: 100%; height: 100%;
    background: rgba(0,0,0,0.6);
    justify-content: center; align-items: center;
    backdrop-filter: blur(5px);
}
.modal-content {
    background: #111827;
    border: 1px solid #1f2937;
    padding: 32px;
    border-radius: 24px;
    width: 400px;
}
.input-group {
    margin-bottom: 16px;
}
.input-group label {
    display: block; margin-bottom: 8px; color: #9ca3af;
}
.input-group input {
    width: 100%; padding: 10px; border-radius: 8px; border: 1px solid #1f2937; background: #1f2937; color: white; box-sizing: border-box;
}
</style>
</head>
<body>
<sidebar>
    <h2>Hexagen Admin</h2>
    <div id="slice-list"></div>
</sidebar>
<content>
    <div class="card" id="main-card">
        <h1 id="slice-title">Welcome to Admin Portal</h1>
        <p>Select a slice from the sidebar to manage database records.</p>
    </div>
</content>

<div class="modal" id="add-modal">
    <div class="modal-content">
        <h3 style="margin-top:0;">Add New Record</h3>
        <form id="add-form"></form>
        <div style="display:flex; justify-content: flex-end; gap: 12px; margin-top: 24px;">
            <button class="btn btn-danger" onclick="closeModal()">Cancel</button>
            <button class="btn" onclick="submitRecord()">Save</button>
        </div>
    </div>
</div>

<script>
const slices = {
    "Lead": [
        { "name": "id", "type": "int" },
        { "name": "nombre", "type": "std::string" }
    ],
};

let activeSlice = '';

function renderSidebar() {
    const list = document.getElementById('slice-list');
    list.innerHTML = '';
    Object.keys(slices).forEach(s => {
        const btn = document.createElement('button');
        btn.className = 'slice-btn';
        btn.innerText = s;
        btn.onclick = () => selectSlice(s);
        list.appendChild(btn);
    });
}

async function selectSlice(name) {
    activeSlice = name;
    document.querySelectorAll('.slice-btn').forEach(btn => {
        btn.classList.toggle('active', btn.innerText === name);
    });
    
    const fields = slices[name];
    const card = document.getElementById('main-card');
    card.innerHTML = `
        <div style="display:flex; justify-content:space-between; align-items:center;">
            <h1 style="margin:0;">${name}</h1>
            <button class="btn" onclick="openAddModal()">Add Record</button>
        </div>
        <div style="overflow-x:auto;">
            <table>
                <thead>
                    <tr>
                        ${fields.map(f => `<th>${f.name}</th>`).join('')}
                        <th>Actions</th>
                    </tr>
                </thead>
                <tbody id="table-body"></tbody>
            </table>
        </div>
    `;
    loadTableData();
}

async function loadTableData() {
    const res = await fetch('/api/admin/' + activeSlice);
    const data = await res.json();
    const tbody = document.getElementById('table-body');
    tbody.innerHTML = '';
    data.forEach(row => {
        const tr = document.createElement('tr');
        const fields = slices[activeSlice];
        tr.innerHTML = fields.map(f => `<td>${row[f.name]}</td>`).join('') + 
            `<td><button class="btn btn-danger" onclick="deleteRecord('${row[fields[0].name]}')">Delete</button></td>`;
        tbody.appendChild(tr);
    });
}

function openAddModal() {
    const form = document.getElementById('add-form');
    form.innerHTML = '';
    slices[activeSlice].forEach(f => {
        form.innerHTML += `
            <div class="input-group">
                <label>${f.name} (${f.type})</label>
                <input type="${f.type === 'int' || f.type === 'float' ? 'number' : 'text'}" name="${f.name}" required>
            </div>
        `;
    });
    document.getElementById('add-modal').style.display = 'flex';
}

function closeModal() {
    document.getElementById('add-modal').style.display = 'none';
}

async function submitRecord() {
    const form = document.getElementById('add-form');
    const data = {};
    slices[activeSlice].forEach(f => {
        const val = form.elements[f.name].value;
        data[f.name] = f.type === 'int' ? parseInt(val) : f.type === 'float' ? parseFloat(val) : val;
    });
    await fetch('/api/admin/' + activeSlice, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    });
    closeModal();
    loadTableData();
}

async function deleteRecord(id) {
    if(!confirm('Are you sure you want to delete this record?')) return;
    const fields = slices[activeSlice];
    const payload = {};
    payload[fields[0].name] = id;
    await fetch('/api/admin/' + activeSlice, {
        method: 'DELETE',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });
    loadTableData();
}

renderSidebar();
</script>
</body>
</html>

)HTML";

const char* HTML_CONTENT = HTML_Home;

Task handle_client(int client_fd, struct sockaddr_in address, int addrlen) {
    std::string req;
    co_await AsyncRead{client_fd, req};
    if (!req.empty()) {
        std::map<std::string, std::string> pathParams; // captured dynamic route params (:id)
        try {
            bool wsUpgraded = false;
            std::string upgradeHeader = getHeaderValue(req, "Upgrade");
            for (char &c : upgradeHeader) { if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a'; }
            if (upgradeHeader == "websocket") {
                if (req.find("GET /live ") != std::string::npos) {
                    std::string wsKey = getHeaderValue(req, "Sec-WebSocket-Key");
                    if (!wsKey.empty()) {
                        std::string acceptKey = getWebSocketAcceptKey(wsKey);
                        std::stringstream handshake;
                        handshake << "HTTP/1.1 101 Switching Protocols\r\n"
                                  << "Upgrade: websocket\r\n"
                                  << "Connection: Upgrade\r\n"
                                  << "Sec-WebSocket-Accept: " << acceptKey << "\r\n\r\n";
                        send(client_fd, handshake.str().c_str(), handshake.str().length(), 0);
                        wsUpgraded = true;
                        std::thread([client_fd]() {
                            register_ws_client(client_fd);
                            std::string msg;
                            while (readWebSocketFrame(client_fd, msg)) {
                                if (!msg.empty()) {
                                    std::string _act = getJSONVal(msg, "action");
                                    if (_act == "subscribe") {
                                        subscribe_topic(getJSONVal(msg, "topic"), client_fd);
                                        sendWebSocketFrame(client_fd, std::string("{\"type\":\"subscribed\",\"topic\":\"") + getJSONVal(msg, "topic") + "\"}");
                                    } else if (_act == "publish") {
                                        publish_topic(getJSONVal(msg, "topic"), msg);
                                    } else {
                                        std::cout << "[LiveView WS] Received: " << msg << std::endl;
                                        broadcast_ws_message(msg, client_fd);
                                    }
                                }
                            }
                            unsubscribe_all_topics(client_fd);
                            unregister_ws_client(client_fd);
                        }).detach();
                    }
                }
            }
            if (wsUpgraded) co_return;
            if (req.rfind("GET /admin ", 0) == 0 || req.find("GET /admin?") != std::string::npos) {
                std::string html = HTML_ADMIN;
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/html; charset=utf-8\r\n"
                     << "Content-Length: " << html.length() << "\r\n\r\n"
                     << html;
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else if (req.find("GET /api/admin/Lead") != std::string::npos) {
                std::string json = Lead::getAllAsJSON(req);
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << json.length() << "\r\n\r\n"
                     << json;
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else if (req.find("POST /api/admin/Lead") != std::string::npos) {
                size_t bodyPos = req.find("\r\n\r\n");
                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
                Lead instance;
                {
                    std::string val = getJSONVal(body, "id");
                    if (!val.empty()) instance.id = safeStoi(val);
                }
                {
                    std::string val = getJSONVal(body, "nombre");
                    if (!val.empty()) instance.nombre = val;
                }
                instance.save();
                std::string msg = "{\"status\":\"success\"}";
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << msg.length() << "\r\n\r\n"
                     << msg;
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else if (req.find("DELETE /api/admin/Lead") != std::string::npos) {
                size_t bodyPos = req.find("\r\n\r\n");
                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
                std::string valToDelete = getJSONVal(body, "id");
                Lead::deleteRecord("id", valToDelete);
                std::string msg = "{\"status\":\"success\"}";
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << msg.length() << "\r\n\r\n"
                     << msg;
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else if (req.rfind("GET /home ", 0) == 0 || req.rfind("GET / ", 0) == 0 || req.rfind("GET /index.html", 0) == 0) {
                std::string html = HTML_Home;
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/html; charset=utf-8\r\n"
                     << "Content-Length: " << html.length() << "\r\n\r\n"
                     << html;
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else if (req.rfind("GET /public/uploads/", 0) == 0) {
                size_t space = req.find(' ', 4);
                std::string path = (space != std::string::npos) ? req.substr(4, space - 4) : "";
                if (!path.empty() && path[0] == '/') path = path.substr(1);
                std::ifstream file(path, std::ios::binary);
                if (file.is_open()) {
                    file.seekg(0, std::ios::end);
                    size_t size = file.tellg();
                    file.seekg(0, std::ios::beg);
                    std::vector<char> fileBuf(size);
                    file.read(fileBuf.data(), size);
                    file.close();
                    std::string contentType = "application/octet-stream";
                    if (path.find(".png") != std::string::npos) contentType = "image/png";
                    else if (path.find(".jpg") != std::string::npos || path.find(".jpeg") != std::string::npos) contentType = "image/jpeg";
                    else if (path.find(".gif") != std::string::npos) contentType = "image/gif";
                    std::stringstream resp;
                    resp << "HTTP/1.1 200 OK\r\n"
                         << "Content-Type: " << contentType << "\r\n"
                         << "Content-Length: " << size << "\r\n"
                         << "Access-Control-Allow-Origin: *\r\n\r\n";
                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);
                    send(client_fd, fileBuf.data(), size, 0);
                } else {
                    std::string msg = "File Not Found";
                    std::stringstream resp;
                    resp << "HTTP/1.1 404 Not Found\r\n"
                         << "Content-Length: " << msg.length() << "\r\n\r\n"
                         << msg;
                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);
                }
            }
            else if (req.find("GET /api/Lead") != std::string::npos) {
                std::string json = Lead::getAllAsJSON(req);
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << json.length() << "\r\n\r\n"
                     << json;
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else if (matchDynamicRoute(req, "GET", "/", pathParams)) {
                size_t bodyPos = req.find("\r\n\r\n");
                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
                bool isMultipart = false;
                std::vector<MultipartPart> mpParts;
                std::string contentType = getHeaderValue(req, "Content-Type");
                if (contentType.find("multipart/form-data") != std::string::npos) {
                    size_t bPos = contentType.find("boundary=");
                    if (bPos != std::string::npos) {
                        std::string boundary = contentType.substr(bPos + 9);
                        if (!boundary.empty() && boundary.front() == '"') boundary = boundary.substr(1);
                        if (!boundary.empty() && boundary.back() == '"') boundary.pop_back();
                        boundary = "--" + boundary;
                        mpParts = parseMultipart(body, boundary);
                        isMultipart = true;
                    }
                }
                std::cout << "[HTTP Endpoint] Invoked / -> Running Home" << std::endl;
                std::stringstream json;
                json << "{\n"
                     << "  \"status\": \"success\",\n"
                     << "  \"message\": \"Action Home executed successfully!\"\n"
                     << "}";
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << json.str().length() << "\r\n\r\n"
                     << json.str();
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else if (matchDynamicRoute(req, "GET", "/api/leads/:id", pathParams)) {
                std::string param_id = pathParams.count("id") ? pathParams["id"] : "";
                std::string authHeader = getHeaderValue(req, "Authorization");
                bool isAuth = false;
                std::string tokenPayload;
                if (authHeader.rfind("Bearer ", 0) == 0) {
                    std::string token = authHeader.substr(7);
                    isAuth = verifySessionToken(token, tokenPayload);
                }
                if (!isAuth) {
                    std::string msg = "{\"status\":\"error\",\"message\":\"Unauthorized\"}";
                    std::stringstream resp;
                    resp << "HTTP/1.1 401 Unauthorized\r\n"
                         << "Content-Type: application/json\r\n"
                         << "Access-Control-Allow-Origin: *\r\n"
                         << "Content-Length: " << msg.length() << "\r\n\r\n"
                         << msg;
                    send(client_fd, resp.str().c_str(), resp.str().length(), 0);
                    close(client_fd);
                    co_return;
                }
                size_t bodyPos = req.find("\r\n\r\n");
                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
                bool isMultipart = false;
                std::vector<MultipartPart> mpParts;
                std::string contentType = getHeaderValue(req, "Content-Type");
                if (contentType.find("multipart/form-data") != std::string::npos) {
                    size_t bPos = contentType.find("boundary=");
                    if (bPos != std::string::npos) {
                        std::string boundary = contentType.substr(bPos + 9);
                        if (!boundary.empty() && boundary.front() == '"') boundary = boundary.substr(1);
                        if (!boundary.empty() && boundary.back() == '"') boundary.pop_back();
                        boundary = "--" + boundary;
                        mpParts = parseMultipart(body, boundary);
                        isMultipart = true;
                    }
                }
                std::cout << "[HTTP Endpoint] Invoked /api/leads/:id -> Running Lead.Ver" << std::endl;
                Lead instance;
                {
                    std::string val = param_id;
                    instance.id = safeStoi(val);
                }
                {
                    std::string val = isMultipart ? getMultipartVal(mpParts, "nombre") : getJSONVal(body, "nombre");
                    instance.nombre = val;
                }
                {
                    auto _cs = instance.validateChangeset();
                    if (!_cs.empty()) {
                        std::stringstream ej;
                        ej << "{\"status\":\"error\",\"errors\":{";
                        bool _ef = true;
                        for (auto& kv : _cs) { if (!_ef) ej << ","; _ef = false; ej << "\"" << kv.first << "\":\"" << kv.second << "\""; }
                        ej << "}}";
                        std::string msg = ej.str();
                        std::stringstream resp;
                        resp << "HTTP/1.1 422 Unprocessable Entity\r\n" << "Content-Type: application/json\r\n" << "Access-Control-Allow-Origin: *\r\n" << "Content-Length: " << msg.length() << "\r\n\r\n" << msg;
                        send(client_fd, resp.str().c_str(), resp.str().length(), 0);
                        close(client_fd);
                        co_return;
                    }
                }
                instance.save();
                instance.Ver();
                broadcast_ws_message("{\"event\": \"action\", \"target\": \"Lead.Ver\"}");
                std::stringstream json;
                json << "{\n"
                     << "  \"status\": \"success\",\n"
                     << "  \"message\": \"Action Lead.Ver executed successfully!\"\n"
                     << "}";
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << json.str().length() << "\r\n\r\n"
                     << json.str();
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else if (matchDynamicRoute(req, "DELETE", "/api/leads/:id", pathParams)) {
                std::string param_id = pathParams.count("id") ? pathParams["id"] : "";
                size_t bodyPos = req.find("\r\n\r\n");
                std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";
                bool isMultipart = false;
                std::vector<MultipartPart> mpParts;
                std::string contentType = getHeaderValue(req, "Content-Type");
                if (contentType.find("multipart/form-data") != std::string::npos) {
                    size_t bPos = contentType.find("boundary=");
                    if (bPos != std::string::npos) {
                        std::string boundary = contentType.substr(bPos + 9);
                        if (!boundary.empty() && boundary.front() == '"') boundary = boundary.substr(1);
                        if (!boundary.empty() && boundary.back() == '"') boundary.pop_back();
                        boundary = "--" + boundary;
                        mpParts = parseMultipart(body, boundary);
                        isMultipart = true;
                    }
                }
                std::cout << "[HTTP Endpoint] Invoked /api/leads/:id -> Running Lead.Borrar" << std::endl;
                std::string valToDelete = param_id;
                Lead::deleteRecord("id", valToDelete);
                Lead instance;
                instance.Borrar();
                std::stringstream json;
                json << "{\n"
                     << "  \"status\": \"success\",\n"
                     << "  \"message\": \"Action Lead.Borrar executed successfully!\"\n"
                     << "}";
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << json.str().length() << "\r\n\r\n"
                     << json.str();
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
            else {
                std::string msg = "Not Found";
                std::stringstream resp;
                resp << "HTTP/1.1 404 Not Found\r\n"
                     << "Content-Length: " << msg.length() << "\r\n\r\n"
                     << msg;
                send(client_fd, resp.str().c_str(), resp.str().length(), 0);
            }
        } catch (const std::exception& ex) {
            std::cerr << "[Hexagen 500] Unhandled exception: " << ex.what() << std::endl;
            std::string msg = std::string("{\"status\":\"error\",\"message\":\"Internal Server Error\"}");
            std::stringstream resp;
            resp << "HTTP/1.1 500 Internal Server Error\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Content-Length: " << msg.length() << "\r\n\r\n"
                 << msg;
            send(client_fd, resp.str().c_str(), resp.str().length(), 0);
        } catch (...) {
            std::cerr << "[Hexagen 500] Unhandled non-standard exception" << std::endl;
            std::string msg = std::string("{\"status\":\"error\",\"message\":\"Internal Server Error\"}");
            std::stringstream resp;
            resp << "HTTP/1.1 500 Internal Server Error\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Content-Length: " << msg.length() << "\r\n\r\n"
                 << msg;
            send(client_fd, resp.str().c_str(), resp.str().length(), 0);
        }
    }
    close(client_fd);
}

int main() {
    initDatabase();
    // Background jobs: recover persisted jobs, then start a supervised worker pool
    recover_persisted_jobs();
    start_job_supervisor(std::atoi(getEnvOr("JOB_WORKERS", "4")));
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    int port = 8080;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) return 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;
    if (listen(server_fd, 5) < 0) return 1;

    std::cout << "🏎️  [Hexagen Server] App running at http://localhost:" << port << std::endl;

    while (true) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;
        handle_client(client_fd, address, addrlen);
    }
    close(server_fd);
    return 0;
}
