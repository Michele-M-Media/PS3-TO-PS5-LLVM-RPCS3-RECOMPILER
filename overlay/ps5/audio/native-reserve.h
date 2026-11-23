#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace recompiler_audio
{
// Single-threaded queue between the guest mixer callback and the native 48 kHz port.
class native_reserve
{
public:
    static constexpr std::size_t frame_bytes = sizeof(float) * 2;
    static constexpr unsigned grain_frames = 256;
    static constexpr unsigned initial_grains = 8;
    static constexpr unsigned maximum_grains = 16;
    struct output
    {
        std::size_t valid_bytes = 0;
        bool initial_wait = false;
        bool starved = false;
    };

private:
    static constexpr std::size_t grain_bytes = frame_bytes * grain_frames;
    static constexpr std::size_t capacity = grain_bytes * (maximum_grains + 1);
    std::array<std::uint8_t, capacity> data{};
    std::size_t head = 0;
    std::size_t available = 0;
    unsigned target_grains = initial_grains;
    bool ready = false;
    bool was_starved = false;

public:
    void reset()
    {
        head = available = 0;
        target_grains = initial_grains;
        ready = was_starved = false;
    }

    unsigned reserve_frames() const { return target_grains * grain_frames; }
    std::size_t buffered_frames() const { return available / frame_bytes; }

    // Reader returns the number of bytes actually produced by the guest mixer.
    template <typename Reader>
    output process(std::uint8_t* destination, Reader&& reader)
    {
        const std::size_t target = (target_grains + 1) * grain_bytes;
        while (available < target)
        {
            const std::size_t tail = (head + available) % capacity;
            const std::size_t request = std::min(capacity - tail, target - available);
            auto read = std::min(request, static_cast<std::size_t>(reader(data.data() + tail, request)));
            read -= read % frame_bytes;
            available += read;
            if (read != request)
                break;
        }
        if (!ready)
        {
            if (available < target)
                return {.initial_wait = true};
            ready = true;
        }

        const std::size_t produced = std::min(available, grain_bytes);
        const std::size_t first = std::min(produced, capacity - head);
        std::memcpy(destination, data.data() + head, first);
        std::memcpy(destination + first, data.data(), produced - first);
        head = (head + produced) % capacity;
        available -= produced;
        const bool starved = produced < grain_bytes;
        if (starved && !was_starved)
            target_grains = std::min(target_grains + 4, maximum_grains);
        was_starved = starved;
        // Once started, pass through real samples even if the reserve is still low.
        return {.valid_bytes = produced, .starved = starved};
    }
};
}
