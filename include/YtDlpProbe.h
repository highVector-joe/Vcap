#pragma once
#include "Downloader.h"
#include <string>
#include <vector>

namespace vcap {

struct YtDlpResult {
    bool        ok = false;
    std::string error;

    // One or more direct stream URLs (best format first)
    std::vector<std::string> urls;
    std::string ext;         // "mp4", "webm", etc.
    std::string title;
    HttpHeaders headers;     // headers/cookies extracted by yt-dlp
};

class YtDlpProbe {
public:
    YtDlpProbe(const std::string& binary       = "yt-dlp",
               const std::string& cookieBrowser = "chrome");

    // Probe URL — returns direct stream URL(s).
    // Uses --cookies-from-browser and --get-url so we never download here.
    YtDlpResult probe(const std::string& pageUrl) const;

    // Check yt-dlp is available and functional
    bool isAvailable() const;
    std::string version() const;

private:
    std::string bin_;
    std::string cookieBrowser_;

    // Run a yt-dlp command and capture stdout + stderr
    bool run(const std::vector<std::string>& args,
             std::string& stdoutBuf,
             std::string& stderrBuf) const;
};

} // namespace vcap
