#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Forward-declare FFmpeg types so callers don't need to include FFmpeg headers
struct AVFormatContext;
struct AVStream;
struct AVCodecContext;

namespace vcap {

// Muxer writes encoded video (and optionally audio) into a container file.
// Two usage modes:
//   1. concat()  — takes a list of downloaded segment file paths and merges them
//   2. writeFrame() — receives raw frames (from screen capture) and encodes them
class Muxer {
public:
    Muxer();
    ~Muxer();

    // ── Mode 1: Segment concatenation ───────────────────────────────────────
    // Merge a list of segment files (already in order) into outputPath.
    // Returns false on error.
    bool concat(const std::vector<std::string>& segmentPaths,
                const std::string&              outputPath);

    // ── Mode 2: Raw frame encoding (screen capture path) ────────────────────
    bool openForEncoding(const std::string& outputPath,
                         int width, int height, int fps,
                         int videoBitrate = 4'000'000);

    // Push a raw BGR frame; pts is in microseconds from first frame
    bool writeRawFrame(const uint8_t* data, int width, int height, int64_t ptsUs);

    void closeEncoding();

    bool isOpen() const { return fmtCtx_ != nullptr; }

private:
    AVFormatContext* fmtCtx_  = nullptr;
    AVStream*        vStream_ = nullptr;
    AVCodecContext*  vCodec_  = nullptr;
    int64_t          frameIdx_ = 0;

    bool flushEncoder();
};

} // namespace vcap
