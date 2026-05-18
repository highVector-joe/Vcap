#include "Utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace vcap::utils {

// ── String helpers ───────────────────────────────────────────────────────────

std::string trim(const std::string& s) {
    auto b = s.begin();
    auto e = s.end();
    while (b != e && std::isspace((unsigned char)*b)) ++b;
    while (e != b && std::isspace((unsigned char)*(e-1))) --e;
    return {b, e};
}

std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return r;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

// ── URL helpers ──────────────────────────────────────────────────────────────

std::string urlScheme(const std::string& url) {
    auto pos = url.find("://");
    if (pos == std::string::npos) return "";
    return url.substr(0, pos);
}

std::string urlHost(const std::string& url) {
    auto pos = url.find("://");
    if (pos == std::string::npos) return "";
    pos += 3;
    auto end = url.find_first_of("/?#", pos);
    return url.substr(pos, end == std::string::npos ? end : end - pos);
}

std::string urlPath(const std::string& url) {
    auto pos = url.find("://");
    if (pos == std::string::npos) return url;
    pos += 3;
    auto start = url.find('/', pos);
    if (start == std::string::npos) return "/";
    auto end = url.find_first_of("?#", start);
    return url.substr(start, end == std::string::npos ? end : end - start);
}

std::string urlDir(const std::string& url) {
    auto q = url.find('?');
    std::string base = (q == std::string::npos) ? url : url.substr(0, q);
    auto slash = base.rfind('/');
    if (slash == std::string::npos) return base;
    return base.substr(0, slash + 1);
}

bool isAbsoluteUrl(const std::string& url) {
    return startsWith(url, "http://") || startsWith(url, "https://");
}

bool isVideoMime(const std::string& mime) {
    auto m = toLower(mime);
    return contains(m, "video/") ||
           contains(m, "application/x-mpegurl") ||
           contains(m, "application/vnd.apple.mpegurl") ||
           contains(m, "application/dash+xml") ||
           contains(m, "audio/x-mpegurl");
}

bool isManifestUrl(const std::string& url) {
    auto u = toLower(url);
    return contains(u, ".m3u8") || contains(u, ".mpd");
}

// ── File helpers ─────────────────────────────────────────────────────────────

std::string tempDir() {
    const char* td = getenv("TMPDIR");
    if (!td) td = "/tmp";
    return std::string(td) + "/vcap_" + std::to_string(getpid());
}

std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/') return dir + file;
    return dir + '/' + file;
}

bool mkdirs(const std::string& path) {
    std::string cmd = "mkdir -p " + path;
    return system(cmd.c_str()) == 0;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

void removeFile(const std::string& path) {
    ::remove(path.c_str());
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), {}};
}

// ── Process helpers ──────────────────────────────────────────────────────────

int runCommand(const std::string& cmd, std::string& out, std::string& err) {
    // Redirect stderr to a temp file so we can capture both streams
    std::string errFile = "/tmp/vcap_stderr_" + std::to_string(getpid()) + ".tmp";
    std::string fullCmd = cmd + " 2>" + errFile;

    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) return -1;

    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
        out += buf;

    int rc = pclose(pipe);

    err = readFile(errFile);
    ::remove(errFile.c_str());

    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool commandExists(const std::string& bin) {
    std::string out, err;
    return runCommand("which " + bin + " >/dev/null 2>&1", out, err) == 0;
}

// ── Time ─────────────────────────────────────────────────────────────────────

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ── Progress bar ─────────────────────────────────────────────────────────────

void printProgress(const std::string& label, int pct, int barWidth) {
    pct = std::max(0, std::min(100, pct));
    int filled = barWidth * pct / 100;
    fprintf(stdout, "\r\033[32m%-20s\033[0m [", label.c_str());
    for (int i = 0; i < barWidth; ++i)
        fputc(i < filled ? '=' : ' ', stdout);
    fprintf(stdout, "] %3d%%", pct);
    if (pct == 100) fputc('\n', stdout);
    fflush(stdout);
}

} // namespace vcap::utils
