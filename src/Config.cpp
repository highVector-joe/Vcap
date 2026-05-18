#include "Config.h"
#include "Logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace vcap {

bool Config::parse(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option %s requires an argument\n", arg.c_str());
                exit(1);
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        } else if (arg == "-o" || arg == "--output") {
            outputPath = next();
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
            Logger::instance().setLevel(LogLevel::DEBUG);
        } else if (arg == "--no-cdp") {
            enableCdp = false;
        } else if (arg == "--no-ytdlp") {
            enableYtDlp = false;
        } else if (arg == "--no-screen") {
            enableScreen = false;
        } else if (arg == "--browser") {
            browserBin = next();
        } else if (arg == "--cdp-port") {
            cdpPort = static_cast<uint16_t>(std::stoi(next()));
        } else if (arg == "--cdp-timeout") {
            cdpTimeoutS = std::stoi(next());
        } else if (arg == "--parallel") {
            parallelSegments = std::stoi(next());
        } else if (arg == "--retry") {
            retryCount = std::stoi(next());
        } else if (arg == "--fps") {
            captureFps = std::stoi(next());
        } else if (arg == "--region") {
            // format: WxH+X+Y  e.g.  1280x720+100+50
            std::string val = next();
            sscanf(val.c_str(), "%dx%d+%d+%d",
                   &captureW, &captureH, &captureX, &captureY);
        } else if (arg == "--duration") {
            captureDurationS = std::stoi(next());
        } else if (arg == "--cookie-browser") {
            cookiesBrowser = next();
        } else if (arg == "--ytdlp-bin") {
            ytDlpBin = next();
        } else if (arg[0] != '-') {
            url = arg;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            printUsage(argv[0]);
            return false;
        }
    }

    if (url.empty()) {
        fprintf(stderr, "Error: no URL specified\n");
        printUsage(argv[0]);
        return false;
    }
    return true;
}

void Config::printUsage(const char* argv0) const {
    fprintf(stderr,
        "vcap — layered video capture tool\n\n"
        "Usage: %s [options] <page-url>\n\n"
        "Options:\n"
        "  -o, --output <file>        Output file (default: output.mp4)\n"
        "  -v, --verbose              Enable debug logging\n"
        "  --no-cdp                   Disable Chrome DevTools Protocol layer\n"
        "  --no-ytdlp                 Disable yt-dlp probe layer\n"
        "  --no-screen                Disable screen capture fallback\n"
        "  --browser <bin>            Browser binary (default: google-chrome)\n"
        "  --cdp-port <port>          CDP debug port (default: 9222)\n"
        "  --cdp-timeout <sec>        Seconds to wait for video URL (default: 15)\n"
        "  --parallel <n>             Parallel segment downloads (default: 8)\n"
        "  --retry <n>                Per-segment retries (default: 5)\n"
        "  --fps <n>                  Screen capture FPS (default: 30)\n"
        "  --region <WxH+X+Y>         Screen capture region (default: auto)\n"
        "  --duration <sec>           Screen capture duration (default: until Ctrl-C)\n"
        "  --cookie-browser <name>    Browser for yt-dlp cookies (default: chrome)\n"
        "  --ytdlp-bin <path>         Path to yt-dlp binary (default: yt-dlp)\n"
        "  -h, --help                 Show this message\n\n"
        "Examples:\n"
        "  %s https://example.com/video-page\n"
        "  %s --no-cdp -o clip.mp4 https://example.com/page\n"
        "  %s --no-cdp --no-ytdlp --fps 60 --region 1280x720+0+0 https://site.com\n",
        argv0, argv0, argv0, argv0);
}

} // namespace vcap
