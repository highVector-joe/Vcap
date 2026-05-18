#include "ManifestParser.h"
#include "Logger.h"
#include "Utils.h"
#include <regex>
#include <sstream>

namespace vcap {

using namespace utils;

// ── Type detection ────────────────────────────────────────────────────────────

ManifestType ManifestParser::detect(const std::string& url,
                                     const std::string& content) {
    auto u = toLower(url);
    if (contains(u, ".m3u8") || contains(u, "m3u8"))
        return ManifestType::HLS;
    if (contains(u, ".mpd") || contains(u, "manifest.mpd"))
        return ManifestType::DASH;

    // Inspect content
    if (contains(content, "#EXTM3U"))
        return ManifestType::HLS;
    if (contains(content, "<MPD") || contains(content, "xmlns=\"urn:mpeg:dash"))
        return ManifestType::DASH;

    auto ext = u.substr(u.rfind('.') + 1);
    if (ext == "mp4" || ext == "webm" || ext == "ts" || ext == "mkv" || ext == "mov")
        return ManifestType::DIRECT;

    return ManifestType::UNKNOWN;
}

// ── URL resolution ────────────────────────────────────────────────────────────

std::string ManifestParser::resolveUrl(const std::string& base,
                                        const std::string& rel) {
    if (rel.empty()) return base;
    if (isAbsoluteUrl(rel)) return rel;

    // Protocol-relative
    if (startsWith(rel, "//")) {
        return urlScheme(base) + ":" + rel;
    }

    // Root-relative
    if (rel[0] == '/') {
        auto scheme = urlScheme(base);
        auto host   = urlHost(base);
        return scheme + "://" + host + rel;
    }

    // Relative
    return urlDir(base) + rel;
}

// ── HLS Parser ────────────────────────────────────────────────────────────────

ParsedManifest ManifestParser::parseHLS(const std::string& baseUrl,
                                         const std::string& content) {
    ParsedManifest result;
    result.type = ManifestType::HLS;

    auto lines = splitLines(content);
    bool isMaster = false;

    // First pass: detect master playlist
    for (auto& l : lines) {
        if (startsWith(l, "#EXT-X-STREAM-INF")) {
            isMaster = true;
            break;
        }
    }

    if (isMaster) {
        // Master playlist — extract variants
        for (size_t i = 0; i < lines.size(); ++i) {
            if (startsWith(lines[i], "#EXT-X-STREAM-INF")) {
                StreamVariant v;
                // Parse BANDWIDTH
                std::regex bwRe("BANDWIDTH=(\\d+)");
                std::smatch m;
                if (std::regex_search(lines[i], m, bwRe))
                    v.bandwidth = std::stoi(m[1]);

                // Parse RESOLUTION
                std::regex resRe("RESOLUTION=(\\d+)x(\\d+)");
                if (std::regex_search(lines[i], m, resRe)) {
                    v.width  = std::stoi(m[1]);
                    v.height = std::stoi(m[2]);
                }

                // Next non-comment line is the URL
                if (i + 1 < lines.size() && !startsWith(lines[i+1], "#")) {
                    v.url = resolveUrl(baseUrl, trim(lines[i+1]));
                    result.variants.push_back(std::move(v));
                    ++i;
                }
            }
        }
    } else {
        // Media playlist — extract segments
        double segDuration = 0.0;
        bool encryptionWarned = false;

        for (size_t i = 0; i < lines.size(); ++i) {
            auto& l = lines[i];

            if (l == "#EXT-X-ENDLIST") break;

            if (startsWith(l, "#EXTINF:")) {
                // Duration
                try {
                    segDuration = std::stod(l.substr(8, l.find(',') - 8));
                } catch (...) { segDuration = 0.0; }
                continue;
            }

            if (startsWith(l, "#EXT-X-KEY")) {
                // Encryption detected — warn but continue (segments still downloadable)
                if (!encryptionWarned) {
                    LOG_WARN("HLS stream is encrypted (#EXT-X-KEY). "
                             "Segments may not be directly playable without key.");
                    encryptionWarned = true;
                }
                continue;
            }

            if (startsWith(l, "#EXT-X-MAP")) {
                // Init segment
                std::regex uriRe("URI=\"([^\"]+)\"");
                std::smatch m;
                if (std::regex_search(l, m, uriRe)) {
                    MediaSegment seg;
                    seg.url    = resolveUrl(baseUrl, m[1]);
                    seg.isInit = true;
                    result.segments.push_back(std::move(seg));
                }
                continue;
            }

            if (!l.empty() && l[0] != '#') {
                MediaSegment seg;
                seg.url         = resolveUrl(baseUrl, trim(l));
                seg.durationSec = segDuration;
                result.segments.push_back(std::move(seg));
                segDuration = 0.0;
            }
        }

        LOG_INFO("HLS: parsed " + std::to_string(result.segments.size()) + " segments");
    }

    return result;
}

// ── DASH Parser ───────────────────────────────────────────────────────────────

ParsedManifest ManifestParser::parseDASH(const std::string& baseUrl,
                                          const std::string& content) {
    ParsedManifest result;
    result.type = ManifestType::DASH;

    // Lightweight regex-based DASH parser
    // For production use, a proper XML parser (pugixml / tinyxml2) is recommended.
    // This covers the vast majority of DASH manifests in the wild.

    // Extract BaseURL
    std::string manifestBase = baseUrl;
    std::regex baseUrlRe("<BaseURL>([^<]+)</BaseURL>");
    std::smatch bm;
    if (std::regex_search(content, bm, baseUrlRe)) {
        std::string bu = trim(bm[1]);
        if (isAbsoluteUrl(bu)) manifestBase = bu;
        else manifestBase = resolveUrl(baseUrl, bu);
    }

    // Find all Representation elements with highest bandwidth video
    std::regex repRe(
        "<Representation[^>]+id=\"([^\"]+)\"[^>]*bandwidth=\"(\\d+)\"[^>]*/?>",
        std::regex::icase);

    // Find SegmentTemplate or SegmentList
    std::regex tmplRe("initialization=\"([^\"]+)\"");
    std::regex mediaRe("media=\"([^\"]+)\"");
    std::regex startRe("startNumber=\"(\\d+)\"");
    std::regex timescaleRe("timescale=\"(\\d+)\"");
    std::regex durationRe("<S[^>]+d=\"(\\d+)\"");

    // Extract SegmentTemplate info
    std::smatch sm;
    std::string initTmpl, mediaTmpl;
    int startNumber = 1;
    int timescale   = 1;

    if (std::regex_search(content, sm, tmplRe))   initTmpl    = sm[1];
    if (std::regex_search(content, sm, mediaRe))   mediaTmpl   = sm[1];
    if (std::regex_search(content, sm, startRe))   startNumber = std::stoi(sm[1]);
    if (std::regex_search(content, sm, timescaleRe)) timescale = std::stoi(sm[1]);

    // Count segment durations to know how many segments there are
    std::vector<int64_t> durations;
    auto dIt = std::sregex_iterator(content.begin(), content.end(), durationRe);
    for (; dIt != std::sregex_iterator(); ++dIt)
        durations.push_back(std::stoll((*dIt)[1]));

    if (!mediaTmpl.empty() && !durations.empty()) {
        // Find best video representation ID
        std::string bestRepId;
        int bestBw = 0;

        auto rBegin = std::sregex_iterator(content.begin(), content.end(), repRe);
        for (auto it = rBegin; it != std::sregex_iterator(); ++it) {
            int bw = std::stoi((*it)[2]);
            if (bw > bestBw) {
                bestBw    = bw;
                bestRepId = (*it)[1];
            }
        }

        if (bestRepId.empty()) bestRepId = "video";

        // Generate init segment
        if (!initTmpl.empty()) {
            std::string initUrl = initTmpl;
            // Replace $RepresentationID$
            auto pos = initUrl.find("$RepresentationID$");
            if (pos != std::string::npos)
                initUrl.replace(pos, 18, bestRepId);
            MediaSegment seg;
            seg.url    = resolveUrl(manifestBase, initUrl);
            seg.isInit = true;
            result.segments.push_back(std::move(seg));
        }

        // Generate media segments
        int idx = startNumber;
        for (auto d : durations) {
            std::string segUrl = mediaTmpl;

            auto repPos = segUrl.find("$RepresentationID$");
            if (repPos != std::string::npos)
                segUrl.replace(repPos, 18, bestRepId);

            auto numPos = segUrl.find("$Number$");
            if (numPos != std::string::npos)
                segUrl.replace(numPos, 8, std::to_string(idx));

            auto numPPos = segUrl.find("$Number%");
            if (numPPos != std::string::npos) {
                // Handle padded $Number%05d$ etc — simplify
                auto end = segUrl.find('$', numPPos + 1);
                if (end != std::string::npos) {
                    std::string fmt = segUrl.substr(numPPos + 1, end - numPPos - 1);
                    char buf[64];
                    snprintf(buf, sizeof(buf), ("%" + fmt.substr(7)).c_str(), idx);
                    segUrl.replace(numPPos, end - numPPos + 1, buf);
                }
            }

            MediaSegment seg;
            seg.url         = resolveUrl(manifestBase, segUrl);
            seg.durationSec = static_cast<double>(d) / timescale;
            result.segments.push_back(std::move(seg));
            ++idx;
        }

        LOG_INFO("DASH: generated " + std::to_string(result.segments.size()) +
                 " segments (best bandwidth: " + std::to_string(bestBw) + " bps)");
    } else {
        LOG_WARN("DASH: could not extract segment template — will try direct URL");
    }

    return result;
}

// ── Variant selection ─────────────────────────────────────────────────────────

const StreamVariant* ManifestParser::bestVariant(const ParsedManifest& m) {
    if (m.variants.empty()) return nullptr;
    const StreamVariant* best = &m.variants[0];
    for (auto& v : m.variants)
        if (v.bandwidth > best->bandwidth) best = &v;
    return best;
}

} // namespace vcap
