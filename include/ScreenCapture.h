#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include <memory>
#include <memory>
#include <vector>
#include <memory>

namespace vcap {

struct CaptureRegion {
    int x = 0, y = 0;
    int width = 0, height = 0;

    bool isZero() const { return width == 0 && height == 0; }
};

// Raw frame: one video frame worth of pixel data (BGR0 or BGRA)
struct RawFrame {
    std::vector<uint8_t> data;
    int                  width  = 0;
    int                  height = 0;
    int64_t              ptsUs  = 0; // presentation timestamp microseconds
};

using FrameCallback = std::function<void(const RawFrame&)>;

class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    // Select the capture region.  If zero, captures the full primary screen.
    void setRegion(const CaptureRegion& r);

    // Frame rate
    void setFps(int fps);

    // Register frame callback — called on the capture thread
    void onFrame(FrameCallback cb);

    // Start/stop capture (non-blocking; runs on internal thread)
    bool start();
    void stop();
    bool isRunning() const;

    // Auto-detect browser video element region via xdotool (X11 only)
    static CaptureRegion detectVideoRegion(const std::string& windowNameHint = "");

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vcap
