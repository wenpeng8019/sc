
#include "internal.h"

#if defined(GLFW_BUILD_WIN32_TIMER)

//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void impl_platform_init_timer(void)
{
    QueryPerformanceFrequency((LARGE_INTEGER*) &g_wsi.timer.win32.frequency);
}

uint64_t impl_platform_get_timer_value(void)
{
    uint64_t value;
    QueryPerformanceCounter((LARGE_INTEGER*) &value);
    return value;
}

uint64_t impl_platform_get_timer_frequency(void)
{
    return g_wsi.timer.win32.frequency;
}

#endif // GLFW_BUILD_WIN32_TIMER

