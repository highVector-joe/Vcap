#pragma once
#include <string>
#include <vector>
#include <chrono>

namespace vcap::utils {

// String helpers
std::string trim(const std::string& s);
std::string toLower(const std::string& s);
std::vector<std::string> splitLines(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith  (const std::string& s, const std::string& suffix);
bool contains  (const std::string& s, const std::string& sub);

// URL helpers
std::string urlScheme(const std::string& url);   // "https"
std::string urlHost  (const std::string& url);   // "example.com"
std::string urlPath  (const std::string& url);   // "/path/to/file.m3u8"
std::string urlDir   (const std::string& url);   // base without filename

bool isAbsoluteUrl(const std::string& url);
bool isVideoMime  (const std::string& mime);
bool isManifestUrl(const std::string& url); // .m3u8 / .mpd

// File helpers
std::string tempDir();
std::string joinPath(const std::string& dir, const std::string& file);
bool        mkdirs   (const std::string& path);
bool        fileExists(const std::string& path);
void        removeFile(const std::string& path);
std::string readFile  (const std::string& path);

// Process helpers
int   runCommand(const std::string& cmd, std::string& out, std::string& err);
bool  commandExists(const std::string& bin);

// Time helpers
int64_t nowMs();
void    sleepMs(int ms);

// Progress bar for terminal
void printProgress(const std::string& label, int pct, int barWidth = 40);

} // namespace vcap::utils
