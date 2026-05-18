#pragma once
#include "Downloader.h"
#include <string>
#include <functional>
#include <memory>
#include <cstdint>

namespace vcap {

struct CdpVideoResult {
    bool        found = false;
    std::string manifestUrl;    // .m3u8 / .mpd
    std::string directUrl;      // direct mp4/webm
    std::string mimeType;
    HttpHeaders headers;        // captured request headers for spoofing
};

// Callback type: called each time a promising network request fires
using CdpEventCb = std::function<void(const CdpVideoResult&)>;

class CdpClient {
public:
    CdpClient(const std::string& host, uint16_t port);
    ~CdpClient();

    // Connect to already-running Chrome (--remote-debugging-port=PORT)
    bool connect();
    void disconnect();

    // Enable Network domain and subscribe to request events.
    // Blocks until a video URL is found or timeoutSec elapses.
    // Returns true and fills result on success.
    bool waitForVideoRequest(CdpVideoResult& result, int timeoutSec = 15);

    // Non-blocking: register callback, then call poll() in a loop
    void onVideoRequest(CdpEventCb cb);
    bool poll(int timeoutMs = 100); // returns false when connection closed

    bool isConnected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Helper: launch Chrome in a subprocess with remote debugging enabled.
// Returns the process PID, or -1 on failure.
int launchBrowser(const std::string& bin,
                  uint16_t           debugPort,
                  const std::string& url);

} // namespace vcap
