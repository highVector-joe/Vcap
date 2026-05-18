#include "Pipeline.h"
#include "CdpClient.h"
#include "YtDlpProbe.h"
#include "ScreenCapture.h"
#include "Downloader.h"
#include "ManifestParser.h"
#include "Muxer.h"
#include "Logger.h"
#include "Utils.h"

#include <csignal>
#include <atomic>
#include <sys/types.h>
#include <sys/wait.h>

namespace vcap {

namespace {
std::atomic<bool> g_interrupted{false};

void sigHandler(int) {
    g_interrupted = true;
}
} // anon

Pipeline::Pipeline(const Config& cfg) : cfg_(cfg) {}
Pipeline::~Pipeline() = default;

void Pipeline::onStatus(StatusCb cb) { statusCb_ = std::move(cb); }

void Pipeline::reportStatus(PipelineStage stage, int pct, const std::string& msg) {
    if (statusCb_) {
        PipelineStatus s;
        s.stage    = stage;
        s.progress = pct;
        s.message  = msg;
        statusCb_(s);
    }
    utils::printProgress(msg, pct);
    LOG_INFO("[" + std::to_string(pct) + "%] " + msg);
}

bool Pipeline::run() {
    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    utils::mkdirs(utils::tempDir());

    if (cfg_.enableCdp) {
        reportStatus(PipelineStage::CDP, 0, "CDP: launching browser");
        if (runCdpLayer()) {
            reportStatus(PipelineStage::DONE, 100, "Done");
            return true;
        }
        if (g_interrupted) return false;
        LOG_WARN("CDP layer failed — trying yt-dlp");
    }

    if (cfg_.enableYtDlp) {
        reportStatus(PipelineStage::YTDLP, 0, "yt-dlp: probing URL");
        if (runYtDlpLayer()) {
            reportStatus(PipelineStage::DONE, 100, "Done");
            return true;
        }
        if (g_interrupted) return false;
        LOG_WARN("yt-dlp layer failed — trying screen capture");
    }

    if (cfg_.enableScreen) {
        reportStatus(PipelineStage::SCREEN, 0, "Screen capture: starting");
        if (runScreenLayer()) {
            reportStatus(PipelineStage::DONE, 100, "Done");
            return true;
        }
    }

    LOG_ERROR("All capture layers failed.");
    return false;
}

// ── Layer 1: CDP ──────────────────────────────────────────────────────────────

bool Pipeline::runCdpLayer() {
    int browserPid = -1;

    // Check if Chrome is already running on the debug port
    CdpClient cdp("127.0.0.1", cfg_.cdpPort);
    bool alreadyRunning = cdp.connect();

    if (!alreadyRunning) {
        LOG_INFO("Launching browser: " + cfg_.browserBin);
        browserPid = launchBrowser(cfg_.browserBin, cfg_.cdpPort, cfg_.url);
        if (browserPid < 0) {
            LOG_ERROR("Failed to launch browser");
            return false;
        }
        // Give it time to start
        utils::sleepMs(3000);

        // Try connecting again
        if (!cdp.connect()) {
            LOG_ERROR("CDP: could not connect after browser launch");
            if (browserPid > 0) kill(browserPid, SIGTERM);
            return false;
        }
    } else {
        LOG_INFO("CDP: connected to existing browser instance");
    }

    CdpVideoResult videoResult;
    bool found = cdp.waitForVideoRequest(videoResult, cfg_.cdpTimeoutS);

    if (!found) {
        if (browserPid > 0) kill(browserPid, SIGTERM);
        return false;
    }

    bool ok = false;

    if (!videoResult.manifestUrl.empty()) {
        LOG_INFO("CDP found manifest URL: " + videoResult.manifestUrl);
        ok = downloadManifest(videoResult.manifestUrl, videoResult.headers);
    } else if (!videoResult.directUrl.empty()) {
        LOG_INFO("CDP found direct URL: " + videoResult.directUrl);
        ok = downloadDirect(videoResult.directUrl, videoResult.headers);
    }

    if (browserPid > 0) {
        kill(browserPid, SIGTERM);
        waitpid(browserPid, nullptr, 0);
    }

    return ok;
}

// ── Layer 2: yt-dlp ───────────────────────────────────────────────────────────

bool Pipeline::runYtDlpLayer() {
    YtDlpProbe probe(cfg_.ytDlpBin, cfg_.cookiesBrowser);

    if (!probe.isAvailable()) {
        LOG_WARN("yt-dlp not found: " + cfg_.ytDlpBin);
        return false;
    }

    LOG_INFO("yt-dlp version: " + probe.version());
    auto result = probe.probe(cfg_.url);

    if (!result.ok || result.urls.empty()) {
        LOG_WARN("yt-dlp probe failed: " + result.error);
        return false;
    }

    // If multiple URLs (video + audio), we take the first (best quality) for now
    const std::string& streamUrl = result.urls[0];
    LOG_INFO("yt-dlp stream URL: " + streamUrl);

    if (utils::isManifestUrl(streamUrl)) {
        return downloadManifest(streamUrl, result.headers);
    } else {
        return downloadDirect(streamUrl, result.headers);
    }
}

// ── Layer 3: Screen capture ───────────────────────────────────────────────────

bool Pipeline::runScreenLayer() {
    ScreenCapture cap;

    // Auto-detect or use configured region
    CaptureRegion region;
    if (cfg_.captureW > 0) {
        region.x = cfg_.captureX; region.y = cfg_.captureY;
        region.width = cfg_.captureW; region.height = cfg_.captureH;
    } else {
        region = ScreenCapture::detectVideoRegion();
    }

    cap.setRegion(region);
    cap.setFps(cfg_.captureFps);

    Muxer muxer;
    int w = region.width  > 0 ? region.width  : 1920;
    int h = region.height > 0 ? region.height : 1080;

    if (!muxer.openForEncoding(cfg_.outputPath, w, h, cfg_.captureFps)) {
        LOG_ERROR("Cannot open muxer for screen capture");
        return false;
    }

    int64_t startMs = utils::nowMs();

    cap.onFrame([&](const RawFrame& frame) {
        if (g_interrupted) {
            cap.stop();
            return;
        }
        muxer.writeRawFrame(frame.data.data(),
                            frame.width, frame.height,
                            frame.ptsUs);

        // Duration limit
        if (cfg_.captureDurationS > 0) {
            int elapsed = static_cast<int>((utils::nowMs() - startMs) / 1000);
            int pct = std::min(99, elapsed * 100 / cfg_.captureDurationS);
            if (elapsed % 5 == 0)
                reportStatus(PipelineStage::SCREEN, pct,
                             "Screen capture: " + std::to_string(elapsed) + "s");
            if (elapsed >= cfg_.captureDurationS)
                cap.stop();
        }
    });

    if (!cap.start()) {
        LOG_ERROR("Screen capture failed to start");
        return false;
    }

    LOG_INFO("Screen capture running — press Ctrl-C to stop");

    // Wait until done
    while (cap.isRunning() && !g_interrupted)
        utils::sleepMs(500);

    cap.stop();
    muxer.closeEncoding();

    return utils::fileExists(cfg_.outputPath);
}

// ── Shared download helpers ───────────────────────────────────────────────────

bool Pipeline::downloadManifest(const std::string& manifestUrl,
                                 const HttpHeaders& hdrs) {
    Downloader dl(cfg_.parallelSegments, cfg_.retryCount, cfg_.retryDelayMs);
    if (cfg_.spoofHeaders) dl.setHeaders(hdrs);

    // Fetch manifest content
    auto manifestRes = dl.fetch(manifestUrl);
    if (!manifestRes.ok) {
        LOG_ERROR("Cannot fetch manifest: " + manifestRes.error);
        return false;
    }

    std::string content(manifestRes.data.begin(), manifestRes.data.end());
    auto type = ManifestParser::detect(manifestUrl, content);

    ParsedManifest manifest;
    if (type == ManifestType::HLS) {
        manifest = ManifestParser::parseHLS(manifestUrl, content);

        // Master playlist → pick best variant and re-fetch media playlist
        if (!manifest.variants.empty()) {
            auto* best = ManifestParser::bestVariant(manifest);
            if (!best) { LOG_ERROR("No valid HLS variant found"); return false; }
            LOG_INFO("Best HLS variant: " + std::to_string(best->bandwidth) + " bps");

            auto mediaRes = dl.fetch(best->url);
            if (!mediaRes.ok) {
                LOG_ERROR("Cannot fetch HLS media playlist: " + mediaRes.error);
                return false;
            }
            std::string mediaContent(mediaRes.data.begin(), mediaRes.data.end());
            manifest = ManifestParser::parseHLS(best->url, mediaContent);
        }
    } else if (type == ManifestType::DASH) {
        manifest = ManifestParser::parseDASH(manifestUrl, content);
    } else {
        // Unknown manifest — treat as direct
        return downloadDirect(manifestUrl, hdrs);
    }

    if (manifest.segments.empty()) {
        LOG_ERROR("Manifest parsed but no segments found");
        return false;
    }

    LOG_INFO("Downloading " + std::to_string(manifest.segments.size()) + " segments...");

    // Collect URLs
    std::vector<std::string> urls;
    for (auto& seg : manifest.segments)
        urls.push_back(seg.url);

    // Track progress
    int total = static_cast<int>(urls.size());
    dl.setProgressCallback([&](int64_t done, int64_t /*tot*/, int /*idx*/) {
        int pct = static_cast<int>(done * 90 / total);
        reportStatus(PipelineStage::CDP, pct,
                     "Downloading " + std::to_string(done) + "/" +
                     std::to_string(total) + " segments");
    });

    // Download all segments to temp dir
    std::vector<DownloadResult> results;
    bool allOk = dl.fetchAll(urls, results);
    if (!allOk) {
        LOG_WARN("Some segments failed — attempting to mux what was retrieved");
    }

    // Write segments to disk
    std::string segDir = utils::tempDir();
    utils::mkdirs(segDir);
    std::vector<std::string> segPaths;

    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i].ok) continue;
        std::string path = utils::joinPath(segDir,
            "seg_" + std::to_string(i) + ".ts");
        FILE* f = fopen(path.c_str(), "wb");
        if (f) {
            fwrite(results[i].data.data(), 1, results[i].data.size(), f);
            fclose(f);
            segPaths.push_back(path);
        }
    }

    reportStatus(PipelineStage::CDP, 92, "Muxing segments...");

    Muxer muxer;
    bool ok = muxer.concat(segPaths, cfg_.outputPath);

    // Cleanup temp segments
    for (auto& p : segPaths) utils::removeFile(p);

    return ok;
}

bool Pipeline::downloadDirect(const std::string& directUrl,
                               const HttpHeaders& hdrs) {
    Downloader dl(1, cfg_.retryCount, cfg_.retryDelayMs);
    if (cfg_.spoofHeaders) dl.setHeaders(hdrs);

    dl.setProgressCallback([&](int64_t done, int64_t total, int) {
        int pct = total > 0 ? static_cast<int>(done * 100 / total) : 0;
        reportStatus(PipelineStage::YTDLP, pct,
                     "Downloading: " + std::to_string(done / 1024 / 1024) + " MB");
    });

    LOG_INFO("Direct download → " + cfg_.outputPath);
    return dl.fetchToFile(directUrl, cfg_.outputPath);
}

} // namespace vcap
