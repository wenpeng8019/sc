
#include "internal.h"

#if defined(GLFW_BUILD_MACOS_TIMER)

#include <mach/mach_time.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void _glfwPlatformInitTimer(void)
{
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);

    _glfw.timer.macos.frequency = (info.denom * 1e9) / info.numer;
}

uint64_t _glfwPlatformGetTimerValue(void)
{
    return mach_absolute_time();
}

uint64_t _glfwPlatformGetTimerFrequency(void)
{
    return _glfw.timer.macos.frequency;
}

#endif // GLFW_BUILD_MACOS_TIMER

