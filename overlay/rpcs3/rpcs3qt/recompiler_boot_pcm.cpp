#include "recompiler_boot_pcm.h"

#include <cerrno>
#include <cstdint>
#include <utility>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace
{
constexpr int sample_rate = 48000;
constexpr int frame_bytes = 2 * sizeof(std::int16_t);
constexpr std::size_t max_pcm_bytes = 24 * 1024 * 1024;

struct decoder_resources
{
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* converter = nullptr;

    ~decoder_resources()
    {
        swr_free(&converter);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
};
}

int recompiler_decode_boot_pcm(const char* path, std::vector<char>& pcm)
{
    if (!path || path[0] != '/')
        return AVERROR(EINVAL);
    const auto* wav = av_find_input_format("wav");
    if (!wav)
        return AVERROR_DEMUXER_NOT_FOUND;

    decoder_resources io;
    AVDictionary* options = nullptr;
    int code = av_dict_set(&options, "protocol_whitelist", "file", 0);
    if (code >= 0)
        code = avformat_open_input(&io.format, path, wav, &options);
    av_dict_free(&options);
    if (code < 0)
        return code;
    if (io.format->nb_streams != 1)
        return AVERROR_INVALIDDATA;
    const auto* parameters = io.format->streams[0]->codecpar;
    if (parameters->codec_id != AV_CODEC_ID_ATRAC3P || parameters->codec_type != AVMEDIA_TYPE_AUDIO ||
        parameters->sample_rate != sample_rate || parameters->ch_layout.nb_channels != 2)
        return AVERROR_INVALIDDATA;

    const auto* implementation = avcodec_find_decoder(parameters->codec_id);
    if (!implementation)
        return AVERROR_DECODER_NOT_FOUND;
    io.codec = avcodec_alloc_context3(implementation);
    io.frame = av_frame_alloc();
    io.packet = av_packet_alloc();
    if (!io.codec || !io.frame || !io.packet)
        return AVERROR(ENOMEM);
    code = avcodec_parameters_to_context(io.codec, parameters);
    if (code < 0)
        return code;
    code = avcodec_open2(io.codec, implementation, nullptr);
    if (code < 0)
        return code;

    std::vector<char> decoded;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    AVSampleFormat initialized_format = AV_SAMPLE_FMT_NONE;
    auto pull_frames = [&]() -> int
    {
        for (;;)
        {
            const int result = avcodec_receive_frame(io.codec, io.frame);
            if (result < 0)
                return result;
            const auto* frame = io.frame;
            const auto sample_format = static_cast<AVSampleFormat>(frame->format);
            if (frame->sample_rate != sample_rate || frame->nb_samples <= 0 ||
                av_channel_layout_compare(&frame->ch_layout, &stereo) != 0)
                return AVERROR_INVALIDDATA;
            if (!io.converter)
            {
                int error = swr_alloc_set_opts2(&io.converter, &stereo, AV_SAMPLE_FMT_S16, sample_rate,
                                               &stereo, sample_format, sample_rate, 0, nullptr);
                if (error < 0)
                    return error;
                error = swr_init(io.converter);
                if (error < 0)
                    return error;
                initialized_format = sample_format;
            }
            if (sample_format != initialized_format ||
                static_cast<std::size_t>(frame->nb_samples) > (max_pcm_bytes - decoded.size()) / frame_bytes)
                return AVERROR_INVALIDDATA;
            const auto offset = decoded.size();
            decoded.resize(offset + static_cast<std::size_t>(frame->nb_samples) * frame_bytes);
            auto* samples = reinterpret_cast<std::uint8_t*>(decoded.data() + offset);
            const std::uint8_t* planes[2] = {frame->extended_data[0], nullptr};
            if (av_sample_fmt_is_planar(sample_format))
                planes[1] = frame->extended_data[1];
            const int count = swr_convert(io.converter, &samples, frame->nb_samples, planes, frame->nb_samples);
            if (count < 0)
                return count;
            if (count != frame->nb_samples)
                return AVERROR_INVALIDDATA;
            av_frame_unref(io.frame);
        }
    };

    while ((code = av_read_frame(io.format, io.packet)) >= 0)
    {
        if (io.packet->stream_index != 0)
            return AVERROR_INVALIDDATA;
        code = avcodec_send_packet(io.codec, io.packet);
        av_packet_unref(io.packet);
        if (code < 0)
            return code;
        code = pull_frames();
        if (code != AVERROR(EAGAIN))
            return code;
    }
    if (code != AVERROR_EOF)
        return code;
    code = avcodec_send_packet(io.codec, nullptr);
    if (code < 0)
        return code;
    code = pull_frames();
    if (code != AVERROR_EOF)
        return code < 0 ? code : AVERROR_INVALIDDATA;
    if (decoded.empty())
        return AVERROR_INVALIDDATA;
    pcm = std::move(decoded);
    return 0;
}
