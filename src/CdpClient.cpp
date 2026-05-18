#include "CdpClient.h"
#include "Logger.h"
#include "Utils.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <sys/types.h>
#include <unistd.h>

namespace beast    = boost::beast;
namespace http     = beast::http;
namespace ws       = beast::websocket;
namespace net      = boost::asio;
using tcp          = net::ip::tcp;
using json         = nlohmann::json;

namespace vcap {

// ── CDP pImpl ─────────────────────────────────────────────────────────────────

struct CdpClient::Impl {
    std::string host;
    uint16_t    port;

    net::io_context          ioc;
    ws::stream<tcp::socket>* wsStream = nullptr;
    beast::flat_buffer       buf;

    std::atomic<bool> connected{false};
    std::atomic<bool> done{false};

    int  cmdId = 1;
    CdpEventCb eventCb;

    Impl(const std::string& h, uint16_t p) : host(h), port(p) {}

    ~Impl() {
        if (wsStream) {
            try { wsStream->close(ws::close_code::normal); } catch (...) {}
            delete wsStream;
        }
    }

    // Get the first available debug target's webSocketDebuggerUrl via HTTP
    std::string getDebuggerUrl() {
        try {
            net::io_context ioc2;
            tcp::resolver   resolver(ioc2);
            beast::tcp_stream stream(ioc2);

            auto results = resolver.resolve(host, std::to_string(port));
            stream.connect(results);

            http::request<http::string_body> req{http::verb::get, "/json", 11};
            req.set(http::field::host, host + ":" + std::to_string(port));
            req.set(http::field::user_agent, "vcap/1.0");
            http::write(stream, req);

            beast::flat_buffer b;
            http::response<http::string_body> res;
            http::read(stream, b, res);

            auto jArr = json::parse(res.body());
            if (jArr.is_array() && !jArr.empty()) {
                auto& target = jArr[0];
                if (target.contains("webSocketDebuggerUrl"))
                    return target["webSocketDebuggerUrl"].get<std::string>();
            }
        } catch (const std::exception& e) {
            LOG_ERROR("CDP HTTP probe failed: " + std::string(e.what()));
        }
        return {};
    }

    bool connectWs(const std::string& debuggerUrl) {
        try {
            // Parse host/path out of the debuggerUrl
            // e.g. ws://127.0.0.1:9222/devtools/page/xxxxx
            std::string wsHost = utils::urlHost(debuggerUrl);
            std::string wsPath = utils::urlPath(debuggerUrl);

            tcp::resolver resolver(ioc);
            wsStream = new ws::stream<tcp::socket>(ioc);

            auto results = resolver.resolve(host, std::to_string(port));
            net::connect(wsStream->next_layer(), results);

            wsStream->handshake(wsHost, wsPath);
            connected = true;
            LOG_INFO("CDP WebSocket connected to " + debuggerUrl);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("CDP WebSocket connect failed: " + std::string(e.what()));
            return false;
        }
    }

    void send(const json& cmd) {
        std::string s = cmd.dump();
        wsStream->write(net::buffer(s));
    }

    bool readMessage(json& out, int timeoutMs = 2000) {
        try {
            // Non-blocking read with timeout emulation via io_context dispatch
            buf.clear();
            wsStream->read(buf);
            out = json::parse(beast::buffers_to_string(buf.data()));
            return true;
        } catch (const beast::system_error& e) {
            if (e.code() == beast::websocket::error::closed) {
                connected = false;
                return false;
            }
            LOG_DEBUG("CDP read error: " + std::string(e.what()));
            return false;
        } catch (const std::exception& e) {
            LOG_DEBUG("CDP parse error: " + std::string(e.what()));
            return false;
        }
    }
};

// ── CdpClient public API ─────────────────────────────────────────────────────

CdpClient::CdpClient(const std::string& host, uint16_t port)
    : impl_(std::make_unique<Impl>(host, port)) {}

CdpClient::~CdpClient() = default;

bool CdpClient::connect() {
    std::string debugUrl = impl_->getDebuggerUrl();
    if (debugUrl.empty()) {
        LOG_ERROR("No CDP debugger URL found — is Chrome running with --remote-debugging-port?");
        return false;
    }
    return impl_->connectWs(debugUrl);
}

void CdpClient::disconnect() {
    impl_->done = true;
}

bool CdpClient::isConnected() const { return impl_->connected; }

bool CdpClient::waitForVideoRequest(CdpVideoResult& result, int timeoutSec) {
    if (!impl_->connected) return false;

    // Enable Network domain
    json enableCmd = {
        {"id", impl_->cmdId++},
        {"method", "Network.enable"},
        {"params", {{"maxTotalBufferSize", 10485760}, {"maxResourceBufferSize", 5242880}}}
    };
    impl_->send(enableCmd);

    int64_t deadline = utils::nowMs() + timeoutSec * 1000LL;

    auto isVideoRequest = [](const json& params) -> bool {
        if (!params.contains("type")) return false;
        std::string type = params["type"];
        if (type == "Media" || type == "XHR" || type == "Fetch") {
            if (params.contains("request")) {
                std::string url = params["request"]["url"].get<std::string>();
                auto u = utils::toLower(url);
                if (utils::contains(u, ".m3u8") ||
                    utils::contains(u, ".mpd")  ||
                    utils::contains(u, ".mp4")  ||
                    utils::contains(u, ".webm") ||
                    utils::contains(u, ".ts")   ||
                    utils::contains(u, "segment") ||
                    utils::contains(u, "manifest")) {
                    return true;
                }
            }
        }
        return false;
    };

    while (utils::nowMs() < deadline) {
        json msg;
        if (!impl_->readMessage(msg)) break;

        if (!msg.contains("method")) continue;
        std::string method = msg["method"];

        if (method == "Network.requestWillBeSent") {
            auto& params = msg["params"];
            if (isVideoRequest(params)) {
                std::string url = params["request"]["url"];
                LOG_INFO("CDP captured video URL: " + url);

                result.found = true;

                // Categorise
                if (utils::isManifestUrl(url)) {
                    result.manifestUrl = url;
                } else {
                    result.directUrl = url;
                }

                // Clone request headers for spoofing
                if (params["request"].contains("headers")) {
                    auto& hdrs = params["request"]["headers"];
                    if (hdrs.contains("User-Agent"))
                        result.headers.userAgent = hdrs["User-Agent"];
                    if (hdrs.contains("Cookie"))
                        result.headers.cookie    = hdrs["Cookie"];
                    if (hdrs.contains("Referer"))
                        result.headers.referer   = hdrs["Referer"];
                    if (hdrs.contains("Authorization"))
                        result.headers.authorization = hdrs["Authorization"];
                    // Copy remaining headers
                    for (auto& [k, v] : hdrs.items()) {
                        std::string kk = k;
                        if (kk != "User-Agent" && kk != "Cookie" &&
                            kk != "Referer"    && kk != "Authorization") {
                            result.headers.extra[kk] = v;
                        }
                    }
                }

                return true;
            }
        }
    }

    LOG_WARN("CDP timeout: no video URL detected within " +
             std::to_string(timeoutSec) + "s");
    return false;
}

void CdpClient::onVideoRequest(CdpEventCb cb) {
    impl_->eventCb = std::move(cb);
}

bool CdpClient::poll(int /*timeoutMs*/) {
    if (!impl_->connected) return false;
    json msg;
    return impl_->readMessage(msg);
}

// ── Browser launcher ─────────────────────────────────────────────────────────

int launchBrowser(const std::string& bin, uint16_t debugPort,
                  const std::string& url) {
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork() failed");
        return -1;
    }
    if (pid == 0) {
        // Child — exec browser
        std::string portArg  = "--remote-debugging-port=" + std::to_string(debugPort);
        std::string profileArg = "--user-data-dir=/tmp/vcap_chrome_profile";
        // Detach from terminal
        setsid();
        execlp(bin.c_str(), bin.c_str(),
               portArg.c_str(),
               profileArg.c_str(),
               "--disable-notifications",
               "--disable-popup-blocking",
               "--no-first-run",
               url.c_str(),
               nullptr);
        _exit(1);
    }
    LOG_INFO("Browser launched (pid=" + std::to_string(pid) +
             ") on debug port " + std::to_string(debugPort));
    return static_cast<int>(pid);
}

} // namespace vcap
