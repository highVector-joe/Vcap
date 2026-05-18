#include "Downloader.h"
#include "Logger.h"
#include "Utils.h"
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <fstream>
#include <cstring>

namespace vcap {

namespace {

// libcurl write callback — appends to a vector<uint8_t>
size_t writeToVector(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* vec = static_cast<std::vector<uint8_t>*>(userdata);
    size_t total = size * nmemb;
    vec->insert(vec->end(), ptr, ptr + total);
    return total;
}

// libcurl write callback — appends to a file
size_t writeToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    FILE* f = static_cast<FILE*>(userdata);
    return fwrite(ptr, size, nmemb, f);
}

// Common curl setup
void setupCurl(CURL* curl, const HttpHeaders& h) {
    struct curl_slist* hdrs = nullptr;

    if (!h.userAgent.empty())
        curl_easy_setopt(curl, CURLOPT_USERAGENT, h.userAgent.c_str());
    else
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");

    if (!h.referer.empty())
        curl_easy_setopt(curl, CURLOPT_REFERER, h.referer.c_str());

    if (!h.cookie.empty())
        curl_easy_setopt(curl, CURLOPT_COOKIE, h.cookie.c_str());

    if (!h.authorization.empty()) {
        std::string val = "Authorization: " + h.authorization;
        hdrs = curl_slist_append(hdrs, val.c_str());
    }

    for (auto& [k, v] : h.extra) {
        std::string val = k + ": " + v;
        hdrs = curl_slist_append(hdrs, val.c_str());
    }

    if (hdrs)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, br");
}

} // anon

// ── Downloader ───────────────────────────────────────────────────────────────

Downloader::Downloader(int w, int r, int d)
    : workers_(w), retry_(r), retryDelay_(d) {
    curl_global_init(CURL_GLOBAL_ALL);
}

Downloader::~Downloader() {
    curl_global_cleanup();
}

DownloadResult Downloader::fetchOnce(const std::string& url) {
    DownloadResult res;
    CURL* curl = curl_easy_init();
    if (!curl) { res.error = "curl_easy_init failed"; return res; }

    setupCurl(curl, headers_);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToVector);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.data);

    CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    res.httpCode = static_cast<int>(httpCode);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        res.error = curl_easy_strerror(rc);
        return res;
    }
    if (httpCode >= 400) {
        res.error = "HTTP " + std::to_string(httpCode);
        return res;
    }

    res.ok = true;
    return res;
}

DownloadResult Downloader::fetch(const std::string& url) {
    for (int attempt = 0; attempt <= retry_; ++attempt) {
        if (attempt > 0) {
            LOG_WARN("Retry " + std::to_string(attempt) + "/" +
                     std::to_string(retry_) + " for " + url);
            utils::sleepMs(retryDelay_ * attempt);
        }
        auto res = fetchOnce(url);
        if (res.ok) return res;
        LOG_WARN("Fetch failed: " + res.error + " (" + url + ")");
    }
    DownloadResult fail;
    fail.error = "All retries exhausted for " + url;
    return fail;
}

bool Downloader::fetchAll(const std::vector<std::string>& urls,
                          std::vector<DownloadResult>& out) {
    int n = static_cast<int>(urls.size());
    out.resize(n);
    std::atomic<int> head{0};
    std::atomic<int> failed{0};
    std::mutex doneMu;

    auto worker = [&]() {
        while (true) {
            int idx = head.fetch_add(1, std::memory_order_relaxed);
            if (idx >= n) return;
            out[idx] = fetch(urls[idx]);
            if (!out[idx].ok) {
                ++failed;
                LOG_ERROR("Permanent failure on segment " + std::to_string(idx));
            }
            if (progressCb_) {
                int done = idx + 1;
                progressCb_(done, n, idx);
            }
        }
    };

    int nw = std::min(workers_, n);
    std::vector<std::thread> threads;
    threads.reserve(nw);
    for (int i = 0; i < nw; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    return failed.load() == 0;
}

bool Downloader::fetchToFile(const std::string& url, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        LOG_ERROR("Cannot open output file: " + path);
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) { fclose(f); return false; }

    setupCurl(curl, headers_);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);

    CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    fclose(f);

    if (rc != CURLE_OK || httpCode >= 400) {
        LOG_ERROR("fetchToFile failed: " + std::string(curl_easy_strerror(rc)));
        utils::removeFile(path);
        return false;
    }
    return true;
}

void Downloader::applyHeaders(void* /*curlHandle*/) const {
    // Implementation merged into setupCurl above
}

} // namespace vcap
