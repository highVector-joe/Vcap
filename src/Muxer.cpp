#include "Muxer.h"
#include "Logger.h"
#include "Utils.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <cstring>
#include <vector>

namespace vcap {

Muxer::Muxer()  = default;
Muxer::~Muxer() { closeEncoding(); }

// ── Mode 1: Segment concatenation ─────────────────────────────────────────────

bool Muxer::concat(const std::vector<std::string>& segmentPaths,
                   const std::string& outputPath) {
    if (segmentPaths.empty()) {
        LOG_ERROR("Muxer::concat: no segment files provided");
        return false;
    }

    // Write a concat demuxer list file
    std::string listPath = utils::joinPath(utils::tempDir(), "concat_list.txt");
    utils::mkdirs(utils::tempDir());

    FILE* f = fopen(listPath.c_str(), "w");
    if (!f) {
        LOG_ERROR("Cannot write concat list: " + listPath);
        return false;
    }
    for (auto& p : segmentPaths)
        fprintf(f, "file '%s'\n", p.c_str());
    fclose(f);

    // Use avformat concat demuxer
    AVFormatContext* inCtx = nullptr;
    AVDictionary*   opts   = nullptr;
    av_dict_set(&opts, "safe", "0", 0);

    std::string concatUrl = "concat:" + listPath;
    // Use the concat protocol (simpler than the concat demuxer for TS/MP4)
    // For maximum compatibility we invoke ffmpeg as a subprocess here
    std::string cmd =
        "ffmpeg -y -f concat -safe 0 -i '" + listPath + "'"
        " -c copy '" + outputPath + "' 2>&1";

    LOG_INFO("Muxing " + std::to_string(segmentPaths.size()) + " segments → " + outputPath);

    std::string out, err;
    int rc = utils::runCommand(cmd, out, err);
    utils::removeFile(listPath);

    if (rc != 0) {
        LOG_ERROR("ffmpeg concat failed:\n" + out);
        return false;
    }

    LOG_INFO("Mux complete → " + outputPath);
    return true;
}

// ── Mode 2: Raw frame encoding ────────────────────────────────────────────────

bool Muxer::openForEncoding(const std::string& outputPath,
                             int width, int height, int fps,
                             int videoBitrate) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOG_ERROR("H.264 encoder not found");
        return false;
    }

    avformat_alloc_output_context2(&fmtCtx_, nullptr, nullptr,
                                   outputPath.c_str());
    if (!fmtCtx_) {
        LOG_ERROR("Cannot allocate output context for: " + outputPath);
        return false;
    }

    vStream_ = avformat_new_stream(fmtCtx_, nullptr);
    if (!vStream_) { LOG_ERROR("Cannot create video stream"); return false; }

    vCodec_ = avcodec_alloc_context3(codec);
    if (!vCodec_) { LOG_ERROR("Cannot allocate codec context"); return false; }

    vCodec_->codec_id     = AV_CODEC_ID_H264;
    vCodec_->width        = width;
    vCodec_->height       = height;
    vCodec_->time_base    = {1, fps};
    vCodec_->framerate    = {fps, 1};
    vCodec_->pix_fmt      = AV_PIX_FMT_YUV420P;
    vCodec_->bit_rate     = videoBitrate;
    vCodec_->gop_size     = fps * 2;
    vCodec_->max_b_frames = 2;

    av_opt_set(vCodec_->priv_data, "preset", "fast",   0);
    av_opt_set(vCodec_->priv_data, "tune",   "zerolatency", 0);
    av_opt_set(vCodec_->priv_data, "crf",    "23",     0);

    if (fmtCtx_->oformat->flags & AVFMT_GLOBALHEADER)
        vCodec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(vCodec_, codec, nullptr) < 0) {
        LOG_ERROR("Cannot open H.264 encoder");
        return false;
    }

    avcodec_parameters_from_context(vStream_->codecpar, vCodec_);
    vStream_->time_base = vCodec_->time_base;

    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmtCtx_->pb, outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
            LOG_ERROR("Cannot open output file: " + outputPath);
            return false;
        }
    }

    if (avformat_write_header(fmtCtx_, nullptr) < 0) {
        LOG_ERROR("Cannot write output header");
        return false;
    }

    LOG_INFO("Encoding: " + std::to_string(width) + "x" + std::to_string(height) +
             " @ " + std::to_string(fps) + "fps → " + outputPath);
    return true;
}

bool Muxer::writeRawFrame(const uint8_t* data, int width, int height,
                           int64_t ptsUs) {
    if (!fmtCtx_ || !vCodec_) return false;

    // Convert BGR24/BGR0 → YUV420P via swscale
    struct SwsContext* sws = sws_getContext(
        width, height, AV_PIX_FMT_BGR24,
        vCodec_->width, vCodec_->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    AVFrame* frame = av_frame_alloc();
    frame->format  = AV_PIX_FMT_YUV420P;
    frame->width   = vCodec_->width;
    frame->height  = vCodec_->height;
    av_frame_get_buffer(frame, 0);

    const uint8_t* srcData[4] = { data, nullptr, nullptr, nullptr };
    int srcStride[4] = { width * 3, 0, 0, 0 };

    sws_scale(sws, srcData, srcStride, 0, height,
              frame->data, frame->linesize);
    sws_freeContext(sws);

    // PTS
    frame->pts = av_rescale_q(ptsUs,
                              {1, 1'000'000},
                              vCodec_->time_base);

    // Send frame to encoder
    int ret = avcodec_send_frame(vCodec_, frame);
    av_frame_free(&frame);

    if (ret < 0) {
        char err[128];
        av_strerror(ret, err, sizeof(err));
        LOG_ERROR("Encode error: " + std::string(err));
        return false;
    }

    // Drain packets
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(vCodec_, pkt) == 0) {
        av_packet_rescale_ts(pkt, vCodec_->time_base, vStream_->time_base);
        pkt->stream_index = vStream_->index;
        av_interleaved_write_frame(fmtCtx_, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    ++frameIdx_;
    return true;
}

bool Muxer::flushEncoder() {
    if (!vCodec_) return true;
    avcodec_send_frame(vCodec_, nullptr); // flush

    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(vCodec_, pkt) == 0) {
        av_packet_rescale_ts(pkt, vCodec_->time_base, vStream_->time_base);
        pkt->stream_index = vStream_->index;
        av_interleaved_write_frame(fmtCtx_, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return true;
}

void Muxer::closeEncoding() {
    if (!fmtCtx_) return;
    flushEncoder();
    av_write_trailer(fmtCtx_);

    if (vCodec_) { avcodec_free_context(&vCodec_); vCodec_ = nullptr; }

    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE))
        avio_closep(&fmtCtx_->pb);

    avformat_free_context(fmtCtx_);
    fmtCtx_  = nullptr;
    vStream_ = nullptr;
    frameIdx_ = 0;
}

} // namespace vcap
