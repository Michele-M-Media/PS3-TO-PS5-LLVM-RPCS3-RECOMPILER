#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "SoundTouch.h"

namespace recompiler_audio
{
// Consume native mixer PCM and keep output at 48 kHz. The tempo correction
// operates on overlaps of real samples and preserves stereo phase and pitch.
class elastic_stereo
{
    static constexpr std::size_t frame_bytes = sizeof(float) * 2;
    static constexpr unsigned grain = 256;
    static constexpr float minimum_tempo = 0.75f;
    soundtouch::SoundTouch processor;
    std::array<float, grain * 2> input{};
    std::array<float, grain * 2> output{};
    float current = 1.0f;
    unsigned low_ticks = 0;
    bool started = false;

    void set_tempo(float next)
    {
        if (next != current)
        {
            current = next;
            processor.setTempo(next);
        }
    }

public:
    elastic_stereo()
    {
        processor.setChannels(2);
        processor.setSampleRate(48'000);
        processor.setSetting(SETTING_SEQUENCE_MS, 20);
        processor.setSetting(SETTING_SEEKWINDOW_MS, 8);
        processor.setSetting(SETTING_OVERLAP_MS, 6);
        processor.setSetting(SETTING_USE_QUICKSEEK, 1);
    }

    void reset()
    {
        processor.clear();
        set_tempo(1.0f);
        low_ticks = 0;
        started = false;
    }

    float tempo() const { return current; }

    void update(std::size_t reserve_frames, std::size_t target_frames)
    {
        started |= reserve_frames >= target_frames;
        if (!started)
            return;

        const auto ready = reserve_frames + processor.numSamples();
        if (ready < target_frames * 3 / 4)
        {
            low_ticks = std::min(low_ticks + 1, 3u);
            if (low_ticks == 3)
                set_tempo(std::max(minimum_tempo, current - 0.025f));
        }
        else
        {
            low_ticks = 0;
            if (ready >= target_frames)
                set_tempo(std::min(1.0f, current + 0.002f));
        }
    }

    template <typename Reader>
    std::size_t read(std::uint8_t* destination, std::size_t requested, Reader&& source)
    {
        const auto target_frames = requested / frame_bytes;
        for (unsigned attempt = 0; processor.numSamples() < target_frames && attempt < 16; ++attempt)
        {
            const auto frames = std::min(sizeof(input),
                static_cast<std::size_t>(source(reinterpret_cast<std::uint8_t*>(input.data()), sizeof(input)))) / frame_bytes;
            if (!frames)
                break;
            processor.putSamples(input.data(), static_cast<unsigned>(frames));
        }
        std::size_t delivered = 0;
        while (delivered < target_frames)
        {
            const auto count = processor.receiveSamples(output.data(),
                static_cast<unsigned>(std::min<std::size_t>(grain, target_frames - delivered)));
            if (!count)
                break;
            std::memcpy(destination + delivered * frame_bytes, output.data(), count * frame_bytes);
            delivered += count;
        }
        return delivered * frame_bytes;
    }
};
}
