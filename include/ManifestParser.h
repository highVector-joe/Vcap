#pragma once
#include <string>
#include <vector>

namespace vcap {

enum class ManifestType { UNKNOWN, HLS, DASH, DIRECT };

struct MediaSegment {
    std::string url;
    double      durationSec = 0.0;
    bool        isInit      = false; // DASH init segment
};

struct StreamVariant {
    std::string url;
    int         bandwidth  = 0; // bits/s
    int         width      = 0;
    int         height     = 0;
    std::string codecs;
};

struct ParsedManifest {
    ManifestType             type = ManifestType::UNKNOWN;
    std::vector<StreamVariant> variants; // for master playlists
    std::vector<MediaSegment>  segments; // for media playlists / DASH
    bool isLive = false;
};

class ManifestParser {
public:
    // Detect type from URL or content
    static ManifestType detect(const std::string& url,
                                const std::string& content);

    // Parse HLS master or media playlist
    static ParsedManifest parseHLS(const std::string& baseUrl,
                                   const std::string& content);

    // Parse DASH MPD
    static ParsedManifest parseDASH(const std::string& baseUrl,
                                    const std::string& content);

    // Resolve relative URL against base
    static std::string resolveUrl(const std::string& base,
                                  const std::string& rel);

    // Pick the best variant by bandwidth (highest)
    static const StreamVariant* bestVariant(const ParsedManifest& m);
};

} // namespace vcap
