
#include "internal.h"

#if defined(GLFW_BUILD_MACOS_TIMER)

#include <mach/mach_time.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void impl_platform_init_timer(void)
{
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);

    g_wsi.timer.macos.frequency = (info.denom * 1e9) / info.numer;
}

uint64_t impl_platform_get_timer_value(void)
{
    return mach_absolute_time();
}

uint64_t impl_platform_get_timer_frequency(void)
{
    return g_wsi.timer.macos.frequency;
}

#endif // GLFW_BUILD_MACOS_TIMER

