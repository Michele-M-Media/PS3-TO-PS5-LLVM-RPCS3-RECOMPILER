#pragma once

#include "recompiler_boot_pcm.h"

#include <QAudioSink>
#include <QIODevice>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

class recompiler_pcm_loop final : public QIODevice
{
public:
    int load(const std::string& path)
    {
        const int result = recompiler_decode_boot_pcm(path.c_str(), bytes);
        if (result < 0)
            return result;
        position = 0;
        return open(QIODevice::ReadOnly | QIODevice::Unbuffered) ? 0 : -1;
    }

    bool isSequential() const override { return true; }
    std::size_t byte_count() const { return bytes.size(); }

protected:
    qint64 readData(char* buffer, qint64 count) override
    {
        if (bytes.empty())
            return -1;
        qint64 written = 0;
        while (written < count)
        {
            const auto amount = std::min<qint64>(count - written, bytes.size() - position);
            std::memcpy(buffer + written, bytes.data() + position, static_cast<std::size_t>(amount));
            written += amount;
            position += static_cast<std::size_t>(amount);
            if (position == bytes.size())
                position = 0;
        }
        return written;
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    std::vector<char> bytes;
    std::size_t position = 0;
};

class recompiler_boot_pcm_player
{
public:
    int start(const std::string& path, float volume)
    {
        const int result = source.load(path);
        if (result < 0)
            return result;
        std::fprintf(stderr, "RECOMPILER_BOOT_PCM decoded bytes=%zu rate=48000 channels=2\n", source.byte_count());
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);
        sink = std::make_unique<QAudioSink>(format);
        sink->setVolume(volume);
        sink->start(&source);
        if (sink->error() != QAudio::NoError || sink->state() == QAudio::StoppedState)
        {
            std::fprintf(stderr, "RECOMPILER_BOOT_PCM sink_failed error=%d\n", static_cast<int>(sink->error()));
            return -1;
        }
        std::fputs("RECOMPILER_BOOT_PCM sink_started\n", stderr);
        return 0;
    }

private:
    // Destruction stops the native sink before releasing the device it pulls from.
    recompiler_pcm_loop source;
    std::unique_ptr<QAudioSink> sink;
};
