#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>

namespace vcap {

struct HttpHeaders {
    std::string userAgent;
    std::string referer;
    std::string cookie;
    std::string authorization;
    std::map<std::string,std::string> extra;
};

struct DownloadResult {
    bool        ok = false;
    int         httpCode = 0;
    std::string error;
    std::vector<uint8_t> data;
};

// Progress callback: (bytesDownloaded, totalBytes, segmentIndex)
using ProgressCb = std::function<void(int64_t, int64_t, int)>;

class Downloader {
public:
    explicit Downloader(int parallelWorkers = 8,
                        int retryCount      = 5,
                        int retryDelayMs    = 500);
    ~Downloader();

    void setHeaders(const HttpHeaders& h) { headers_ = h; }
    void setProgressCallback(ProgressCb cb) { progressCb_ = cb; }

    // Single synchronous fetch (blocking)
    DownloadResult fetch(const std::string& url);

    // Download a list of URLs in parallel; results are in order.
    // Returns false if any segment permanently failed after all retries.
    bool fetchAll(const std::vector<std::string>& urls,
                  std::vector<DownloadResult>& out);

    // Stream a URL and write directly to a file (for large direct downloads)
    bool fetchToFile(const std::string& url, const std::string& path);

private:
    int       workers_;
    int       retry_;
    int       retryDelay_;
    HttpHeaders headers_;
    ProgressCb  progressCb_;

    DownloadResult fetchOnce(const std::string& url);
    void           applyHeaders(void* curlHandle) const;
};

} // namespace vcap
