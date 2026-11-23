#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace recompiler_audio
{
struct output_levels
{
    float input_peak = 0.0f;
    float output_peak = 0.0f;
    std::uint64_t over_full_scale = 0;
    std::uint64_t non_finite = 0;
    std::uint64_t limited_frames = 0;
};

class floating_stereo
{
    using frame = std::array<float, 2>;
    static constexpr unsigned fade_length = 48; // 1 ms at the native 48 kHz output rate.
    static constexpr float release_step = 1.0f / 1024.0f;
    float gain = 1.0f;
    frame last{};
    frame fade_from{};
    unsigned fade_remaining = 0;
    bool gap = false;

    void start_fade(bool silence)
    {
        gap = silence;
        fade_from = last;
        fade_remaining = fade_length;
    }

    void write(std::uint8_t* data, frame sample, output_levels& levels)
    {
        const float target = fade_remaining ? 1.0f - static_cast<float>(--fade_remaining) / fade_length : 1.0f;
        for (std::size_t channel = 0; channel < sample.size(); ++channel)
        {
            last[channel] = std::clamp(fade_from[channel] * (1.0f - target) + sample[channel] * target, -1.0f, 1.0f);
            levels.output_peak = std::max(levels.output_peak, std::abs(last[channel]));
        }
        std::memcpy(data, last.data(), sizeof(last));
    }

public:
    void process(std::uint8_t* data, std::size_t valid_bytes, std::size_t total_bytes, output_levels& levels)
    {
        if (valid_bytes && gap)
            start_fade(false);

        for (std::size_t offset = 0; offset < valid_bytes; offset += sizeof(frame))
        {
            frame sample{};
            std::memcpy(sample.data(), data + offset, sizeof(sample));
            float peak = 0.0f;
            for (float& value : sample)
            {
                if (!std::isfinite(value))
                {
                    ++levels.non_finite;
                    value = 0.0f;
                }
                const float magnitude = std::abs(value);
                peak = std::max(peak, magnitude);
                levels.over_full_scale += magnitude > 1.0f;
            }
            levels.input_peak = std::max(levels.input_peak, peak);
            // Link both channels to preserve the stereo balance. Recover gain gradually.
            const float required = peak > 1.0f ? 0.98f / peak : 1.0f;
            gain = std::min(required, gain + (1.0f - gain) * release_step);
            if (required == 1.0f && gain > 0.999f)
                gain = 1.0f;
            levels.limited_frames += gain < 1.0f;
            write(data + offset, {sample[0] * gain, sample[1] * gain}, levels);
        }

        if (valid_bytes < total_bytes && !gap)
            start_fade(true);
        for (std::size_t offset = valid_bytes; offset < total_bytes; offset += sizeof(frame))
            write(data + offset, {}, levels);
    }
};
}
