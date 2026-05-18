#pragma once
#include "Config.h"
#include <string>
#include <functional>

namespace vcap {

enum class PipelineStage {
    CDP,        // Layer 1 — Chrome DevTools Protocol
    YTDLP,      // Layer 2 — yt-dlp probe
    SCREEN,     // Layer 3 — screen capture
    DONE,
    FAILED
};

struct PipelineStatus {
    PipelineStage stage      = PipelineStage::CDP;
    std::string   stageLabel;
    int           progress   = 0;   // 0-100
    std::string   message;
    bool          finished   = false;
    bool          success    = false;
};

using StatusCb = std::function<void(const PipelineStatus&)>;

class Pipeline {
public:
    explicit Pipeline(const Config& cfg);
    ~Pipeline();

    // Register progress/status callback (called on pipeline thread)
    void onStatus(StatusCb cb);

    // Run synchronously — blocks until complete or permanently failed.
    // Returns true on success.
    bool run();

private:
    bool runCdpLayer();
    bool runYtDlpLayer();
    bool runScreenLayer();

    bool downloadManifest(const std::string& manifestUrl,
                          const struct HttpHeaders& hdrs);
    bool downloadDirect  (const std::string& directUrl,
                          const struct HttpHeaders& hdrs);

    void reportStatus(PipelineStage stage, int pct, const std::string& msg);

    const Config& cfg_;
    StatusCb      statusCb_;
};

} // namespace vcap
