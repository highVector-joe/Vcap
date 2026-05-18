#include "ScreenCapture.h"
#include "Logger.h"
#include "Utils.h"

#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>

#ifdef VCAP_HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#endif

#ifdef VCAP_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#endif

namespace vcap {

// ── pImpl ─────────────────────────────────────────────────────────────────────

struct ScreenCapture::Impl {
    CaptureRegion   region;
    int             fps    = 30;
    FrameCallback   cb;
    std::thread     captureThread;
    std::atomic<bool> running{false};

    // X11 state
#ifdef VCAP_HAVE_X11
    Display*       dpy   = nullptr;
    XShmSegmentInfo shmInfo{};
    XImage*        ximg  = nullptr;
    bool           shmOk = false;
#endif

    void runX11();
    void runPipeWire();
    void runGeneric(); // fallback: ffmpeg subprocess
};

// ── ScreenCapture public ──────────────────────────────────────────────────────

ScreenCapture::ScreenCapture() : impl_(std::make_unique<Impl>()) {}
ScreenCapture::~ScreenCapture() { stop(); }

void ScreenCapture::setRegion(const CaptureRegion& r) { impl_->region = r; }
void ScreenCapture::setFps   (int fps)                { impl_->fps    = fps; }
void ScreenCapture::onFrame  (FrameCallback cb)        { impl_->cb     = std::move(cb); }
bool ScreenCapture::isRunning() const                  { return impl_->running; }

bool ScreenCapture::start() {
    if (impl_->running) return true;
    impl_->running = true;

#ifdef VCAP_HAVE_X11
    LOG_INFO("ScreenCapture: using X11/XShm backend");
    impl_->captureThread = std::thread(&Impl::runX11, impl_.get());
    return true;
#elif defined(VCAP_HAVE_PIPEWIRE)
    LOG_INFO("ScreenCapture: using PipeWire backend");
    impl_->captureThread = std::thread(&Impl::runPipeWire, impl_.get());
    return true;
#else
    LOG_WARN("ScreenCapture: no native backend — using FFmpeg subprocess fallback");
    impl_->captureThread = std::thread(&Impl::runGeneric, impl_.get());
    return true;
#endif
}

void ScreenCapture::stop() {
    impl_->running = false;
    if (impl_->captureThread.joinable())
        impl_->captureThread.join();

#ifdef VCAP_HAVE_X11
    if (impl_->ximg) {
        XShmDetach(impl_->dpy, &impl_->shmInfo);
        XDestroyImage(impl_->ximg);
        shmdt(impl_->shmInfo.shmaddr);
        impl_->ximg = nullptr;
    }
    if (impl_->dpy) {
        XCloseDisplay(impl_->dpy);
        impl_->dpy = nullptr;
    }
#endif
}

// ── X11 backend ───────────────────────────────────────────────────────────────

#ifdef VCAP_HAVE_X11

void ScreenCapture::Impl::runX11() {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        LOG_ERROR("X11: cannot open display");
        running = false;
        return;
    }

    Window root = DefaultRootWindow(dpy);
    Screen* screen = DefaultScreenOfDisplay(dpy);

    // Determine capture area
    int capX = region.x;
    int capY = region.y;
    int capW = region.width  > 0 ? region.width  : screen->width;
    int capH = region.height > 0 ? region.height : screen->height;

    LOG_INFO("X11 capture: " + std::to_string(capW) + "x" +
             std::to_string(capH) + " at (" +
             std::to_string(capX) + "," + std::to_string(capY) + ")");

    // Try XShm
    bool useShm = XShmQueryExtension(dpy);
    if (useShm) {
        ximg = XShmCreateImage(dpy,
                               DefaultVisual(dpy, DefaultScreen(dpy)),
                               DefaultDepth(dpy, DefaultScreen(dpy)),
                               ZPixmap, nullptr, &shmInfo,
                               capW, capH);
        if (ximg) {
            shmInfo.shmid = shmget(IPC_PRIVATE,
                                   ximg->bytes_per_line * ximg->height,
                                   IPC_CREAT | 0777);
            shmInfo.shmaddr = ximg->data =
                static_cast<char*>(shmat(shmInfo.shmid, nullptr, 0));
            shmInfo.readOnly = False;
            if (!XShmAttach(dpy, &shmInfo)) {
                XDestroyImage(ximg);
                shmdt(shmInfo.shmaddr);
                ximg   = nullptr;
                useShm = false;
            }
        } else {
            useShm = false;
        }
    }

    if (!useShm)
        LOG_WARN("XShm not available — falling back to XGetImage (slower)");

    using clock = std::chrono::steady_clock;
    auto frameDuration = std::chrono::microseconds(1'000'000 / fps);
    auto nextFrame     = clock::now();
    int64_t ptsUs      = 0;

    while (running) {
        auto now = clock::now();
        if (now < nextFrame) {
            std::this_thread::sleep_until(nextFrame);
            now = clock::now();
        }
        nextFrame += frameDuration;

        RawFrame frame;
        frame.width  = capW;
        frame.height = capH;
        frame.ptsUs  = ptsUs;
        ptsUs += 1'000'000 / fps;

        if (useShm) {
            XShmGetImage(dpy, root, ximg, capX, capY, AllPlanes);
            size_t bytes = ximg->bytes_per_line * capH;
            frame.data.assign(
                reinterpret_cast<uint8_t*>(ximg->data),
                reinterpret_cast<uint8_t*>(ximg->data) + bytes);
        } else {
            XImage* img = XGetImage(dpy, root,
                                    capX, capY, capW, capH,
                                    AllPlanes, ZPixmap);
            if (!img) continue;
            size_t bytes = img->bytes_per_line * capH;
            frame.data.assign(
                reinterpret_cast<uint8_t*>(img->data),
                reinterpret_cast<uint8_t*>(img->data) + bytes);
            XDestroyImage(img);
        }

        if (cb) cb(frame);
    }
}

// Helper — shm attach wrapper for POSIX
static void* shmat(int shmid, const void* addr, int flag) {
    return shmat(shmid, addr, flag);
}

#else // No X11

void ScreenCapture::Impl::runX11() {
    LOG_ERROR("X11 backend not compiled in");
    running = false;
}

#endif // VCAP_HAVE_X11

// ── PipeWire backend ──────────────────────────────────────────────────────────

#ifdef VCAP_HAVE_PIPEWIRE

void ScreenCapture::Impl::runPipeWire() {
    // Full PipeWire screen capture requires xdg-desktop-portal interaction
    // and is event-loop driven. This skeleton connects to PipeWire and
    // demonstrates the setup — a production implementation would integrate
    // with the portal to get a stream node ID.

    pw_init(nullptr, nullptr);

    struct pw_main_loop* loop = pw_main_loop_new(nullptr);
    if (!loop) {
        LOG_ERROR("PipeWire: pw_main_loop_new failed");
        running = false;
        return;
    }

    LOG_INFO("PipeWire: main loop created — awaiting portal stream node");

    // NOTE: In a complete implementation, before reaching here you would:
    // 1. Use libportal (xdp_portal_capture_screen) to open a screen cast session
    // 2. Get the PipeWire node ID from the portal response
    // 3. Create a pw_stream connecting to that node ID
    // 4. In the process callback, copy frame data into RawFrame and call cb()
    //
    // This is left as an integration point because it requires D-Bus + libportal
    // which have complex async setup beyond this scope.

    LOG_WARN("PipeWire backend is a stub. Use X11 or compile with libportal for full support.");

    pw_main_loop_destroy(loop);
    pw_deinit();
    running = false;
}

#else

void ScreenCapture::Impl::runPipeWire() {
    LOG_ERROR("PipeWire backend not compiled in");
    running = false;
}

#endif // VCAP_HAVE_PIPEWIRE

// ── Generic FFmpeg subprocess fallback ───────────────────────────────────────

void ScreenCapture::Impl::runGeneric() {
    // Use ffmpeg as a subprocess to perform capture, output raw frames to pipe
    // This works even without X11/PipeWire headers

    std::string display = ":0.0";
    const char* dp = getenv("DISPLAY");
    if (dp) display = dp;

    int capW = region.width  > 0 ? region.width  : 1920;
    int capH = region.height > 0 ? region.height : 1080;
    int capX = region.x;
    int capY = region.y;

    std::string offset = std::to_string(capX) + "," + std::to_string(capY);

    // Build ffmpeg command: capture x11 → output raw bgr frames to stdout
    std::string cmd =
        "ffmpeg -f x11grab"
        " -video_size " + std::to_string(capW) + "x" + std::to_string(capH) +
        " -framerate " + std::to_string(fps) +
        " -i " + display + "+" + offset +
        " -f rawvideo -pix_fmt bgr24 pipe:1 2>/dev/null";

    LOG_INFO("Generic capture via: " + cmd);

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOG_ERROR("Cannot launch ffmpeg for screen capture");
        running = false;
        return;
    }

    size_t frameBytes = static_cast<size_t>(capW * capH * 3); // BGR24
    std::vector<uint8_t> frameBuf(frameBytes);
    int64_t ptsUs = 0;

    while (running) {
        size_t got = fread(frameBuf.data(), 1, frameBytes, pipe);
        if (got < frameBytes) break; // EOF or error

        RawFrame frame;
        frame.data   = frameBuf;
        frame.width  = capW;
        frame.height = capH;
        frame.ptsUs  = ptsUs;
        ptsUs += 1'000'000 / fps;

        if (cb) cb(frame);
    }

    pclose(pipe);
    running = false;
}

// ── Auto-detect video region ──────────────────────────────────────────────────

CaptureRegion ScreenCapture::detectVideoRegion(const std::string& windowNameHint) {
    CaptureRegion r;

#ifdef VCAP_HAVE_X11
    if (!utils::commandExists("xdotool")) {
        LOG_WARN("xdotool not found — cannot auto-detect video region");
        return r;
    }

    std::string hint = windowNameHint.empty() ? "firefox\\|chrome\\|chromium" : windowNameHint;
    std::string out, err;
    // Get window geometry
    std::string cmd = "xdotool search --name '" + hint + "' getwindowgeometry --shell 2>/dev/null | head -1";
    utils::runCommand(cmd, out, err);

    // Parse: X=100 Y=50 WIDTH=1280 HEIGHT=720
    int x = 0, y = 0, w = 0, h = 0;
    sscanf(out.c_str(), "X=%d\nY=%d\nWIDTH=%d\nHEIGHT=%d", &x, &y, &w, &h);
    if (w > 0 && h > 0) {
        r.x = x; r.y = y; r.width = w; r.height = h;
        LOG_INFO("Auto-detected capture region: " +
                 std::to_string(w) + "x" + std::to_string(h) +
                 " at (" + std::to_string(x) + "," + std::to_string(y) + ")");
    }
#endif

    return r;
}

} // namespace vcap
