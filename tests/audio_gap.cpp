#include "../overlay/ps5/audio/bidirectional-gap.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

int main()
{
    using recompiler_audio::delayed_stereo_grain;
    using frame = std::array<float, 2>;
    delayed_stereo_grain delay;
    std::array<frame, delayed_stereo_grain::frames> pcm{};
    auto valid = [&](unsigned first)
    {
        for (unsigned i = 0; i < pcm.size(); ++i)
        {
            const float phase = static_cast<float>(i + first) * (6.2831853f * 440 / 48000);
            pcm[i] = {0.57f * std::sin(phase), 0.47f * std::cos(phase)};
        }
    };
    auto push = [&](unsigned count) { return delay.push(reinterpret_cast<std::uint8_t*>(pcm.data()), count); };
    valid(0);
    const auto first = pcm;
    assert(push(pcm.size()).priming);
    valid(pcm.size());
    const auto partial = pcm;
    assert(push(120).valid == pcm.size() && pcm == first);
    valid(2 * pcm.size());
    const auto repaired = push(pcm.size());
    assert(repaired.valid == 120 && repaired.repaired == 136);
    for (unsigned i = 0; i < repaired.valid; ++i)
        assert(pcm[i] == partial[i]);
    assert(push(0).valid == pcm.size());
    assert(push(0).repaired == 0);
    delay.reset();
    assert(push(0).priming);
}
