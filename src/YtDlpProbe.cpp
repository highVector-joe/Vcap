#include "YtDlpProbe.h"
#include "Logger.h"
#include "Utils.h"
#include <sstream>

namespace vcap {

YtDlpProbe::YtDlpProbe(const std::string& binary,
                        const std::string& cookieBrowser)
    : bin_(binary), cookieBrowser_(cookieBrowser) {}

bool YtDlpProbe::isAvailable() const {
    return utils::commandExists(bin_);
}

std::string YtDlpProbe::version() const {
    std::string out, err;
    utils::runCommand(bin_ + " --version", out, err);
    return utils::trim(out);
}

bool YtDlpProbe::run(const std::vector<std::string>& args,
                      std::string& stdoutBuf,
                      std::string& stderrBuf) const {
    std::ostringstream cmd;
    cmd << bin_;
    for (auto& a : args) {
        // Basic shell-safe quoting
        cmd << " '" << a << "'";
    }
    int rc = utils::runCommand(cmd.str(), stdoutBuf, stderrBuf);
    return rc == 0;
}

YtDlpResult YtDlpProbe::probe(const std::string& pageUrl) const {
    YtDlpResult result;

    if (!isAvailable()) {
        result.error = "yt-dlp binary not found: " + bin_;
        LOG_WARN(result.error);
        return result;
    }

    LOG_INFO("yt-dlp probe: " + pageUrl);

    // Build args:
    // --get-url         → print direct stream URL(s)
    // --no-playlist     → single video only
    // --cookies-from-browser <browser>
    // --format bestvideo+bestaudio/best
    // --no-warnings
    std::vector<std::string> args = {
        "--get-url",
        "--no-playlist",
        "--format", "bestvideo+bestaudio/best",
        "--no-warnings",
        "--cookies-from-browser", cookieBrowser_,
        pageUrl
    };

    std::string out, err;
    bool ok = run(args, out, err);

    if (!ok || utils::trim(out).empty()) {
        // Try without cookies (some sites don't need them)
        LOG_WARN("yt-dlp with browser cookies failed, retrying without cookies");
        args = {
            "--get-url",
            "--no-playlist",
            "--format", "bestvideo+bestaudio/best",
            "--no-warnings",
            pageUrl
        };
        ok = run(args, out, err);
    }

    if (!ok || utils::trim(out).empty()) {
        result.error = "yt-dlp returned no URL. stderr: " + utils::trim(err);
        LOG_WARN(result.error);
        return result;
    }

    // Each line is a URL (yt-dlp may return multiple for video+audio)
    auto lines = utils::splitLines(out);
    for (auto& l : lines) {
        auto t = utils::trim(l);
        if (!t.empty() && utils::isAbsoluteUrl(t))
            result.urls.push_back(t);
    }

    if (result.urls.empty()) {
        result.error = "yt-dlp output had no valid URLs";
        return result;
    }

    // Now get title and extension via JSON dump
    std::vector<std::string> jsonArgs = {
        "--dump-json",
        "--no-playlist",
        "--no-warnings",
        pageUrl
    };
    std::string jout, jerr;
    if (run(jsonArgs, jout, jerr)) {
        try {
            // Minimal JSON extraction without full parse dependency here
            auto titlePos = jout.find("\"title\":");
            if (titlePos != std::string::npos) {
                titlePos += 9; // skip "title":\"
                auto end = jout.find('"', titlePos);
                if (end != std::string::npos)
                    result.title = jout.substr(titlePos, end - titlePos);
            }
            auto extPos = jout.find("\"ext\":");
            if (extPos != std::string::npos) {
                extPos += 7;
                auto end = jout.find('"', extPos);
                if (end != std::string::npos)
                    result.ext = jout.substr(extPos, end - extPos);
            }
        } catch (...) {}
    }

    if (result.ext.empty()) result.ext = "mp4";

    // Set spoofing headers — use a realistic UA
    result.headers.userAgent =
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
    result.headers.referer = pageUrl;

    result.ok = true;
    LOG_INFO("yt-dlp found " + std::to_string(result.urls.size()) +
             " URL(s) for: " + result.title);
    return result;
}

} // namespace vcap
