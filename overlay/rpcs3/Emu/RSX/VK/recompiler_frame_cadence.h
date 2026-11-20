#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace recompiler
{
    struct cadence_sample
    {
        std::int64_t elapsed_us = 0;
        std::int64_t max_guest_gap_us = 0;
        std::uint32_t guest_flips = 0;
        std::uint32_t skipped = 0;
        std::uint32_t guest_presents = 0;
        std::uint32_t other_presents = 0;
        std::uint32_t failed_presents = 0;
    };

    class frame_cadence
    {
        std::optional<std::int64_t> m_begin;
        std::optional<std::int64_t> m_previous_guest;
        cadence_sample m_counts;
        bool m_guest = false;

    public:
        // Called once on entry to every renderer flip, including skip and overlay paths.
        std::optional<cadence_sample> flip(std::int64_t now, bool guest, bool skipped)
        {
            if (!m_begin)
                m_begin = now;

            if (guest && m_previous_guest)
                m_counts.max_guest_gap_us = std::max(m_counts.max_guest_gap_us, now - *m_previous_guest);

            std::optional<cadence_sample> completed;
            const auto elapsed = now - *m_begin;
            if (elapsed >= 4'000'000)
            {
                // Counts cover [begin, now). A gap observed at the closing boundary belongs
                // to this report, including long pauses across multiple nominal windows.
                m_counts.elapsed_us = elapsed;
                completed = m_counts;
                m_counts = {};
                m_begin = now;
            }

            m_guest = guest;
            if (guest)
            {
                ++m_counts.guest_flips;
                m_counts.skipped += skipped;
                m_previous_guest = now;
            }
            return completed;
        }

        // The current flip calls swapchain->present synchronously on the RSX thread.
        void present(bool accepted)
        {
            if (!accepted)
                ++m_counts.failed_presents;
            else if (m_guest)
                ++m_counts.guest_presents;
            else
                ++m_counts.other_presents;
        }
    };
}
