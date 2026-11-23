#pragma once

#include <algorithm>
#include <cstdint>

class mix_deadline
{
    using time = std::uint64_t;
    time phase = 0;

    static time elapsed(time now, time begin)
    {
        return now >= begin ? now - begin : 0;
    }

    static time remaining(time interval, time passed)
    {
        return interval - std::min(interval, passed);
    }

public:
    void reset() { phase = 0; }

    time wait_us(time now, time actual, time period, time minimum) const
    {
        const time scheduled = phase && phase <= actual ? phase : actual;
        return std::max(remaining(period, elapsed(now, scheduled)), remaining(minimum, elapsed(now, actual)));
    }

    void commit(time now, time actual, time period, time maximum_carry)
    {
        const time scheduled = phase && phase <= actual ? phase : actual;
        const time passed = elapsed(now, scheduled);
        const time overdue = passed - std::min(period, passed);
        phase = now - std::min({overdue, maximum_carry, now});
    }
};
