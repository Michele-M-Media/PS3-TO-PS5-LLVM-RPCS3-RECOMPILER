#pragma once

#include <chrono>

namespace recompiler_texture {
struct retry {
    using clock = std::chrono::steady_clock;
    clock::time_point next{};

    bool due(clock::time_point now) const
    {
        return next != clock::time_point{} && now >= next;
    }

    void failed(clock::time_point now)
    {
        next = now + std::chrono::milliseconds(125);
    }

    void clear()
    {
        next = {};
    }
};
}
