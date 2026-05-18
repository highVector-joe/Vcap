#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace vcap {

struct Config {
    // Target
    std::string url;                    // page URL to capture from
    std::string outputPath = "output.mp4";

    // Strategy control
    bool enableCdp       = true;        // Layer 1: Chrome DevTools Protocol
    bool enableYtDlp     = true;        // Layer 2: yt-dlp probe
    bool enableScreen    = true;        // Layer 3: screen capture fallback

    // CDP / browser settings
    std::string browserBin  = "google-chrome"; // or "chromium-browser"
    uint16_t    cdpPort     = 9222;
    int         cdpTimeoutS = 15;       // seconds to wait for first network video event

    // Download settings
    int  parallelSegments = 8;          // concurrent segment downloads
    int  retryCount       = 5;          // per-segment retry attempts
    int  retryDelayMs     = 500;        // delay between retries
    bool spoofHeaders     = true;       // clone browser UA + cookies

    // Screen capture settings (Layer 3 fallback)
    int  captureFps       = 30;
    int  captureX = 0, captureY = 0;   // 0,0,0,0 → auto-detect from xdotool
    int  captureW = 0, captureH = 0;
    int  captureDurationS = 0;          // 0 = run until Ctrl-C

    // yt-dlp
    std::string ytDlpBin       = "yt-dlp";
    std::string cookiesBrowser = "chrome"; // passed to --cookies-from-browser

    // Logging
    bool verbose = false;

    // Parse from argc/argv; returns false and prints usage on error
    bool parse(int argc, char** argv);
    void printUsage(const char* argv0) const;
};

} // namespace vcap
