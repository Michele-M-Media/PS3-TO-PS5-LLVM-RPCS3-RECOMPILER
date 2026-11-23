#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace recompiler_audio
{
// Repair one short gap when real PCM on both sides is already available.
class bidirectional_gap
{
    static constexpr unsigned order = 20;
    static constexpr unsigned context = 192;
    static constexpr unsigned maximum_gap = 256;
    using track = std::array<float, context + maximum_gap>;

    static bool forecast(track& wave, unsigned available, unsigned missing)
    {
        std::array<double, order + 1> correlation{};
        float peak = 0.0f;
        for (unsigned i = 0; i < available; ++i)
        {
            if (!std::isfinite(wave[i]))
                return false;
            peak = std::max(peak, std::abs(wave[i]));
        }
        if (peak > 1.0f)
            return false;
        for (unsigned lag = 0; lag <= order; ++lag)
            for (unsigned i = lag; i < available; ++i)
                correlation[lag] += static_cast<double>(wave[i]) * wave[i - lag];
        if (correlation[0] < 0.000001)
            return false;

        // Levinson–Durbin with diagonal loading to suppress unstable tails.
        std::array<double, order + 1> coeff{};
        double variance = correlation[0] * 1.00005;
        for (unsigned depth = 1; depth <= order; ++depth)
        {
            double remainder = correlation[depth];
            for (unsigned lag = 1; lag < depth; ++lag)
                remainder -= coeff[lag] * correlation[depth - lag];
            const double reflection = std::clamp(remainder / variance, -0.999, 0.999);
            auto next = coeff;
            for (unsigned lag = 1; lag < depth; ++lag)
                next[lag] = coeff[lag] - reflection * coeff[depth - lag];
            next[depth] = reflection;
            coeff = next;
            variance *= 1.0 - reflection * reflection;
        }
        for (unsigned i = available; i < available + missing; ++i)
        {
            double value = 0;
            for (unsigned lag = 1; lag <= order; ++lag)
                value += coeff[lag] * wave[i - lag];
            wave[i] = static_cast<float>(std::clamp(value, -static_cast<double>(peak), static_cast<double>(peak)));
        }
        return true;
    }

public:
    static unsigned process(std::uint8_t* before, unsigned valid_before, unsigned total,
        const std::uint8_t* after, unsigned valid_after)
    {
        if (valid_before >= total || total - valid_before > maximum_gap || valid_before < order * 2 || valid_after < order * 2)
            return 0;
        const unsigned missing = total - valid_before;
        const unsigned left = std::min(valid_before, context);
        const unsigned right = std::min(valid_after, context);
        std::array<std::array<float, 2>, maximum_gap> repaired{};
        for (unsigned ch = 0; ch < 2; ++ch)
        {
            track forward{}, backward{};
            for (unsigned i = 0; i < left; ++i)
                std::memcpy(&forward[i], before + ((valid_before - left + i) * 2 + ch) * sizeof(float), sizeof(float));
            for (unsigned i = 0; i < right; ++i)
                std::memcpy(&backward[i], after + ((right - 1 - i) * 2 + ch) * sizeof(float), sizeof(float));
            if (!forecast(forward, left, missing) || !forecast(backward, right, missing))
                return 0;
            for (unsigned i = 0; i < missing; ++i)
            {
                const float blend = static_cast<float>(i + 1) / (missing + 1);
                repaired[i][ch] = forward[left + i] * (1.0f - blend) + backward[right + missing - 1 - i] * blend;
            }
        }
        std::memcpy(before + valid_before * sizeof(repaired[0]), repaired.data(), missing * sizeof(repaired[0]));
        return missing;
    }
};

class delayed_stereo_grain
{
public:
    static constexpr unsigned frames = 256;
    static constexpr unsigned frame_bytes = sizeof(float) * 2;
    static constexpr unsigned bytes = frames * frame_bytes;

    struct playback
    {
        unsigned valid = 0;
        unsigned repaired = 0;
        bool priming = false;
    };

    void reset() { started = false; }

    playback push(std::uint8_t* buffer, unsigned valid_frames)
    {
        if (!started)
        {
            std::memcpy(previous.data(), buffer, valid_frames * frame_bytes);
            previous_valid = valid_frames;
            started = true;
            return {.priming = true};
        }
        const unsigned repaired = bidirectional_gap::process(previous.data(), previous_valid, frames, buffer, valid_frames);
        const playback result{previous_valid, repaired};
        std::swap_ranges(previous.begin(), previous.end(), buffer);
        previous_valid = valid_frames;
        return result;
    }

private:
    std::array<std::uint8_t, bytes> previous{};
    unsigned previous_valid = 0;
    bool started = false;
};
}
