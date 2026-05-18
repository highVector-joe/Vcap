
# vcap — Layered Video Capture

A C++ tool that captures video playing in a web browser using three progressively more robust methods, falling back automatically when a faster method fails.

```
Layer 1 (fastest):  Chrome DevTools Protocol — intercept the stream URL directly
Layer 2 (broad):    yt-dlp probe — community-maintained site extractors
Layer 3 (fallback): Screen capture — X11 XShm or FFmpeg x11grab
```

---

## Quick start

```bash
# Install build deps (Ubuntu/Debian)
./install_deps.sh

# Build
./build.sh

# Capture a video
./build/vcap https://example.com/video-page

# With output file
./build/vcap -o my_video.mp4 https://example.com/video-page

# Skip CDP, use yt-dlp only
./build/vcap --no-cdp https://youtube.com/watch?v=xxxxx

# Screen capture fallback only (1280×720 at 60fps)
./build/vcap --no-cdp --no-ytdlp --region 1280x720+0+0 --fps 60 --duration 120 https://site.com
```

---

## How it works

### Layer 1 — CDP (Chrome DevTools Protocol)

vcap launches Chrome with `--remote-debugging-port=9222` and connects to it over WebSocket. It subscribes to `Network.requestWillBeSent` events. When the browser plays a video, it captures:
- The `.m3u8` (HLS) or `.mpd` (DASH) manifest URL
- All request headers — including `Cookie`, `Authorization`, and `Referer`

It then downloads all segments in **parallel** (default: 8 concurrent) and muxes them into a single MP4. This is typically **10–50× faster than real-time**.

### Layer 2 — yt-dlp probe

Calls `yt-dlp --get-url --cookies-from-browser chrome <url>`. yt-dlp handles site-specific anti-bot measures, format negotiation, and geo-restrictions. vcap uses yt-dlp purely as a URL resolver — it does the actual downloading with libcurl.

Fallback: retries without `--cookies-from-browser` if the first attempt fails.

### Layer 3 — Screen capture

Uses **X11 XShm** (shared memory) for zero-copy pixel capture from the X server, or falls back to an **FFmpeg x11grab subprocess** if headers are not compiled in. Encodes directly to H.264 via **libavcodec** with libswscale doing BGR→YUV420P conversion.

Auto-detects the browser window bounds via `xdotool` if no `--region` is specified.

---

## Architecture

```
main.cpp
└── Pipeline          — orchestrates layers, handles fallback
    ├── CdpClient     — WebSocket connection to Chrome DevTools
    ├── YtDlpProbe    — subprocess wrapper for yt-dlp
    ├── ScreenCapture — X11/PipeWire/FFmpeg frame grabber
    ├── Downloader    — parallel libcurl with retry
    ├── ManifestParser — HLS + DASH manifest parsers
    ├── Muxer         — libavformat concat + libavcodec encoder
    ├── Config        — CLI argument parser
    └── Logger        — colourised, thread-safe logger
```

---

## CLI reference

```
Usage: vcap [options] <page-url>

Options:
  -o, --output <file>        Output file (default: output.mp4)
  -v, --verbose              Enable debug logging
  --no-cdp                   Disable CDP layer
  --no-ytdlp                 Disable yt-dlp layer
  --no-screen                Disable screen capture fallback
  --browser <bin>            Browser binary (default: google-chrome)
  --cdp-port <port>          CDP debug port (default: 9222)
  --cdp-timeout <sec>        Seconds to wait for video URL (default: 15)
  --parallel <n>             Parallel segment downloads (default: 8)
  --retry <n>                Per-segment retries (default: 5)
  --fps <n>                  Screen capture FPS (default: 30)
  --region <WxH+X+Y>         Screen capture region (e.g. 1280x720+0+0)
  --duration <sec>           Screen capture duration (0 = until Ctrl-C)
  --cookie-browser <name>    Browser for yt-dlp cookies (default: chrome)
  --ytdlp-bin <path>         Path to yt-dlp binary
  -h, --help                 Show this message
```

---

## Dependencies

| Library | Purpose | Required |
|---------|---------|----------|
| libcurl | HTTP downloads | ✅ |
| Boost.Beast + Asio | WebSocket for CDP | ✅ |
| FFmpeg (libav*) | Muxing + encoding | ✅ |
| OpenSSL | TLS | ✅ |
| nlohmann/json | JSON parsing (fetched by CMake) | ✅ auto |
| libX11 + libXext | X11 screen capture | optional |
| libpipewire | Wayland screen capture | optional |
| yt-dlp | Layer 2 probe | optional (runtime) |
| xdotool | Auto-detect video region | optional (runtime) |

Install on Ubuntu/Debian:
```bash
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
    libcurl4-openssl-dev libboost-all-dev libssl-dev libx11-dev libxext-dev xdotool
```

---

## Extending vcap

- **New site extractor**: add a case in `YtDlpProbe::probe()` or extend `CdpClient::waitForVideoRequest()` with site-specific URL patterns
- **Audio capture**: extend `Muxer::openForEncoding()` with an audio stream and `ScreenCapture` to capture PulseAudio/PipeWire audio alongside video
- **PipeWire Wayland**: implement `ScreenCapture::Impl::runPipeWire()` using `libportal` — the hook is already in place
- **GUI**: hook `Pipeline::onStatus()` into a Qt or GTK status bar
=======
# Vcap
 A C++ tool that captures video playing in a web browser using three progressively more robust methods
>>>>>>> 88d411f3c6814fd8373131196965bf43765381dd
