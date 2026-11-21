#include "../overlay/ps5/input/ps5_motion.h"
#include <cassert>

#ifdef WITH_MOTION_PROVIDER
static int calls;
extern "C" int scePadSetMotionSensorState(int32_t handle, bool enable)
{
    ++calls;
    assert(enable);
    return handle == 3 ? 0 : -7;
}
#endif

int main()
{
#ifdef WITH_MOTION_PROVIDER
    assert(ps5::enable_motion(3) == 0);
    assert(ps5::enable_motion(4) == -7);
    assert(calls == 2);
#else
    assert(!ps5::enable_motion(3).has_value());
#endif
}
