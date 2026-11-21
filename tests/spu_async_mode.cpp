#include "../overlay/rpcs3/Emu/Cell/recompiler_spu_async.h"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace recompiler::spu_async;

int main()
{
    assert(select(false, false) == mode::standard);
    assert(select(false, true) == mode::hybrid);
    assert(select(true, false) == mode::background);
    assert(select(true, true) == mode::background);

    assert(workers(mode::standard, 4) == 4);
    assert(workers(mode::hybrid, 2) == 2);
    assert(workers(mode::background, 8) == 1);
    assert(workers(mode::background, 1) == 1);

    constexpr std::int64_t lower = std::numeric_limits<std::int32_t>::min();
    constexpr std::int64_t upper = std::numeric_limits<std::int32_t>::max();
    assert(valid_redirect(lower));
    assert(valid_redirect(upper));
    assert(valid_redirect(0));
    assert(!valid_redirect(lower - 1));
    assert(!valid_redirect(upper + 1));
}
